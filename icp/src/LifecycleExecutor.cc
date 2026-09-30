#include <virtual_factory/icp/LifecycleExecutor.hh>

#include <utility>

namespace virtual_factory
{
namespace icp
{

struct LifecycleExecutor::State
{
  std::mutex mutex;
  std::condition_variable cv;
  bool running{false};
  bool stopping{false};
  Handler handler;
  std::unordered_map<std::string, AdapterSlot> slots;
  std::size_t inFlightCount{0};
  std::size_t abandonedCount{0};
  std::unordered_map<std::string, std::size_t> abandonedPerAdapter;
  std::deque<LifecycleJob> deferredTeardowns;
};

LifecycleExecutor::LifecycleExecutor()
    : state_(std::make_shared<State>())
{
}

LifecycleExecutor::~LifecycleExecutor()
{
  this->stop();
}

void LifecycleExecutor::setHandler(Handler handler)
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  this->state_->handler = std::move(handler);
}

LifecycleExecutor::WorkerHandle LifecycleExecutor::createWorkerHandle()
{
  auto control = std::make_shared<WorkerControl>();
  std::shared_ptr<State> state = this->state_;
  WorkerHandle handle;
  handle.control = control;
  handle.thread = std::thread([state, control]() { workerMain(state, control); });
  return handle;
}

void LifecycleExecutor::discardUninstalledWorker(WorkerHandle &handle)
{
  if (!handle.thread.joinable())
  {
    return;
  }
  if (handle.control)
  {
    handle.control->decrementOnExit.store(false, std::memory_order_relaxed);
    handle.control->stayLive.store(false, std::memory_order_release);
  }
  handle.thread.detach();
  if (this->state_)
  {
    this->state_->cv.notify_all();
  }
}

void LifecycleExecutor::spawnWorkerLocked()
{
  this->workers_.push_back(this->createWorkerHandle());
}

void LifecycleExecutor::start(std::size_t workerCount)
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  if (this->state_->running)
  {
    return;
  }
  if (workerCount == 0)
  {
    workerCount = 1;
  }
  this->state_->stopping = false;
  this->state_->running = true;
  this->workers_.reserve(workerCount);
  for (std::size_t i = 0; i < workerCount; ++i)
  {
    // Capture state_ by shared_ptr so abandoned/detached workers never wait on
    // a destroyed condition_variable (pthread_cond_destroy would hang).
    this->spawnWorkerLocked();
  }
}

bool LifecycleExecutor::tryRunTeardown(const LifecycleJob &job)
{
  if (job.teardownAdapter == nullptr)
  {
    return true;
  }
  if (job.teardownIoMutex != nullptr)
  {
    std::unique_lock<std::mutex> io(*job.teardownIoMutex, std::try_to_lock);
    if (!io.owns_lock())
    {
      return false;
    }
    if (job.teardownAdapter->connectionState() != ConnectionState::Disconnected)
    {
      job.teardownAdapter->disconnect();
    }
    return true;
  }
  if (job.teardownAdapter->connectionState() != ConnectionState::Disconnected)
  {
    job.teardownAdapter->disconnect();
  }
  return true;
}

void LifecycleExecutor::stop(
    std::chrono::milliseconds grace, std::shared_ptr<void> keepAlive)
{
  std::shared_ptr<State> state;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    state = this->state_;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->running && this->workers_.empty())
      {
        return;
      }
      state->stopping = true;
      state->running = false;
      for (auto &entry : state->slots)
      {
        clearInvalidatablePendingLocked(entry.second);
      }
    }
  }
  state->cv.notify_all();

  if (grace.count() < 0)
  {
    grace = std::chrono::milliseconds{0};
  }

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait_for(lock, grace, [&]() { return drainCompleteLocked(*state); });
  }

  bool busy = false;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    std::lock_guard<std::mutex> lock(state->mutex);
    busy = !drainCompleteLocked(*state);

    if (busy)
    {
      auto cohort = std::make_shared<AbandonedCohort>();
      cohort->workers = std::move(this->workers_);
      cohort->keepAlive = std::move(keepAlive);
      // Keep State alive for detached workers still in protocol I/O or cv.wait.
      cohort->state = state;
      this->workers_.clear();
      this->abandoned_ = cohort;
      state->stopping = false;
      for (WorkerHandle &worker : cohort->workers)
      {
        if (worker.thread.joinable())
        {
          worker.thread.detach();
        }
      }
      // Replace state so a subsequent start()/dtor uses a fresh control block and
      // never destroys the abandoned workers' condition_variable.
      this->state_ = std::make_shared<State>();
      return;
    }
  }

  std::vector<WorkerHandle> joining;
  {
    std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
    joining = std::move(this->workers_);
    this->workers_.clear();
  }
  for (WorkerHandle &worker : joining)
  {
    if (worker.thread.joinable())
    {
      worker.thread.join();
    }
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopping = false;
    state->inFlightCount = 0;
    state->deferredTeardowns.clear();
    for (auto &entry : state->slots)
    {
      entry.second.inFlight = false;
      entry.second.inFlightWorker = std::thread::id{};
      entry.second.pending.clear();
    }
  }
}

bool LifecycleExecutor::hasAbandonedWorkers() const
{
  std::lock_guard<std::mutex> lock(this->workers_mutex_);
  return this->abandoned_ != nullptr;
}

bool LifecycleExecutor::running() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->running;
}

std::size_t LifecycleExecutor::liveWorkerCount() const
{
  std::lock_guard<std::mutex> lock(this->workers_mutex_);
  return this->workers_.size();
}

std::size_t LifecycleExecutor::abandonedWorkerCount() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->abandonedCount;
}

std::size_t LifecycleExecutor::abandonedWorkerCount(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->abandonedPerAdapter.find(adapterId);
  if (it == this->state_->abandonedPerAdapter.end())
  {
    return 0;
  }
  return it->second;
}

void LifecycleExecutor::clearInvalidatablePendingLocked(AdapterSlot &slot)
{
  std::deque<LifecycleJob> keep;
  for (LifecycleJob &job : slot.pending)
  {
    if (job.op == LifecycleOp::Teardown)
    {
      keep.push_back(std::move(job));
    }
  }
  slot.pending = std::move(keep);
}

bool LifecycleExecutor::hasFifoRunnableLocked(const State &state)
{
  for (const auto &entry : state.slots)
  {
    if (!currentGenerationBusy(entry.second) && !entry.second.pending.empty())
    {
      return true;
    }
  }
  return false;
}

bool LifecycleExecutor::drainCompleteLocked(const State &state)
{
  if (state.inFlightCount != 0)
  {
    return false;
  }
  if (!state.deferredTeardowns.empty())
  {
    return false;
  }
  for (const auto &entry : state.slots)
  {
    if (!entry.second.pending.empty())
    {
      return false;
    }
  }
  return true;
}

void LifecycleExecutor::accountAbandonedExit(
    State &state, const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.abandonedCount > 0)
  {
    --state.abandonedCount;
  }
  const auto it = state.abandonedPerAdapter.find(adapterId);
  if (it != state.abandonedPerAdapter.end())
  {
    if (it->second > 0)
    {
      --it->second;
    }
    if (it->second == 0)
    {
      state.abandonedPerAdapter.erase(it);
    }
  }
}

std::uint64_t LifecycleExecutor::bumpGeneration(const std::string &adapterId)
{
  {
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    AdapterSlot &slot = this->state_->slots[adapterId];
    ++slot.generation;
    clearInvalidatablePendingLocked(slot);
  }
  // Wake workers: a stale in-flight job must not keep the current generation
  // from starting (obstructed RecoveryConnect isolation).
  this->state_->cv.notify_all();
  return this->generation(adapterId);
}

std::uint64_t LifecycleExecutor::generation(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return 0;
  }
  return it->second.generation;
}

bool LifecycleExecutor::isConnectStyle(LifecycleOp op)
{
  return op == LifecycleOp::Connect || op == LifecycleOp::RecoveryConnect;
}

bool LifecycleExecutor::shouldCoalesceLocked(
    const AdapterSlot &slot, const LifecycleJob &job, bool includeInFlight) const
{
  if (job.op == LifecycleOp::Disconnect || job.op == LifecycleOp::Teardown)
  {
    return false;
  }

  auto matches = [&](LifecycleOp existingOp, std::uint64_t existingGen) {
    if (existingGen != job.generation)
    {
      return false;
    }
    if (job.op == LifecycleOp::Reconnect)
    {
      return existingOp == LifecycleOp::Reconnect;
    }
    if (isConnectStyle(job.op))
    {
      return isConnectStyle(existingOp);
    }
    return existingOp == job.op;
  };

  if (includeInFlight && slot.inFlight
      && matches(slot.inFlightOp, slot.inFlightGeneration))
  {
    return true;
  }
  for (const LifecycleJob &pending : slot.pending)
  {
    if (matches(pending.op, pending.generation))
    {
      return true;
    }
  }
  return false;
}

bool LifecycleExecutor::hasInFlightOrPending(
    const std::string &adapterId, LifecycleOp op) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  const AdapterSlot &slot = it->second;
  if (slot.inFlight && slot.inFlightOp == op
      && slot.inFlightGeneration == slot.generation)
  {
    return true;
  }
  for (const LifecycleJob &pending : slot.pending)
  {
    if (pending.op == op && pending.generation == slot.generation)
    {
      return true;
    }
  }
  return false;
}

bool LifecycleExecutor::hasInFlightOrPendingAnyGeneration(
    const std::string &adapterId, LifecycleOp op) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  const AdapterSlot &slot = it->second;
  if (slot.inFlight && slot.inFlightOp == op)
  {
    return true;
  }
  for (const LifecycleJob &pending : slot.pending)
  {
    if (pending.op == op)
    {
      return true;
    }
  }
  return false;
}

bool LifecycleExecutor::hasInFlight(
    const std::string &adapterId, LifecycleOp op) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  const AdapterSlot &slot = it->second;
  return slot.inFlight && slot.inFlightOp == op
      && slot.inFlightGeneration == slot.generation;
}

bool LifecycleExecutor::hasInFlight(
    const std::string &adapterId, LifecycleOp op, std::uint64_t generation) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  const AdapterSlot &slot = it->second;
  return slot.inFlight && slot.inFlightOp == op
      && slot.inFlightGeneration == generation;
}

bool LifecycleExecutor::hasInFlightAnyGeneration(
    const std::string &adapterId, LifecycleOp op) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  const AdapterSlot &slot = it->second;
  return slot.inFlight && slot.inFlightOp == op;
}

bool LifecycleExecutor::canAbandonStaleInFlightLocked(
    const AdapterSlot &slot, std::uint64_t generation) const
{
  if (!slot.inFlight || slot.inFlightOp != LifecycleOp::RecoveryConnect
      || slot.inFlightGeneration != generation)
  {
    return false;
  }
  return true;
}

bool LifecycleExecutor::canAbandonStaleInFlight(
    const std::string &adapterId, std::uint64_t generation) const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  if (!this->state_->running || this->state_->stopping)
  {
    return false;
  }
  if (this->state_->abandonedCount >= kMaxAbandonedLifecycleWorkers)
  {
    return false;
  }
  const auto per = this->state_->abandonedPerAdapter.find(adapterId);
  if (per != this->state_->abandonedPerAdapter.end()
      && per->second >= kMaxAbandonedPerAdapter)
  {
    return false;
  }
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  return this->canAbandonStaleInFlightLocked(it->second, generation);
}

std::size_t LifecycleExecutor::idleLiveWorkersLocked() const
{
  std::size_t busy = 0;
  for (const WorkerHandle &handle : this->workers_)
  {
    if (!handle.thread.joinable())
    {
      continue;
    }
    const std::thread::id tid = handle.thread.get_id();
    for (const auto &entry : this->state_->slots)
    {
      const AdapterSlot &slot = entry.second;
      if (slot.inFlight && slot.inFlightWorker == tid)
      {
        ++busy;
        break;
      }
    }
  }
  if (busy > this->workers_.size())
  {
    return 0;
  }
  return this->workers_.size() - busy;
}

bool LifecycleExecutor::canAdmitBlockingIsolation() const
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  if (!this->state_->running || this->state_->stopping)
  {
    return false;
  }
  return this->idleLiveWorkersLocked() >= 2;
}

std::size_t LifecycleExecutor::idleLiveWorkerCount() const
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->idleLiveWorkersLocked();
}

bool LifecycleExecutor::abandonStaleInFlight(
    const std::string &adapterId, std::uint64_t generation)
{
  std::lock_guard<std::mutex> workersLock(this->workers_mutex_);
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  if (!this->state_->running || this->state_->stopping)
  {
    return false;
  }
  if (this->state_->abandonedCount >= kMaxAbandonedLifecycleWorkers)
  {
    return false;
  }
  auto &perCount = this->state_->abandonedPerAdapter[adapterId];
  if (perCount >= kMaxAbandonedPerAdapter)
  {
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }
  AdapterSlot &slot = this->state_->slots[adapterId];
  if (!this->canAbandonStaleInFlightLocked(slot, generation))
  {
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }

  const std::thread::id tid = slot.inFlightWorker;
  auto found = this->workers_.end();
  for (auto it = this->workers_.begin(); it != this->workers_.end(); ++it)
  {
    if (it->thread.joinable() && it->thread.get_id() == tid)
    {
      found = it;
      break;
    }
  }
  if (found == this->workers_.end())
  {
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }

  // Spawn-before-detach: construct the replacement while the old worker is
  // still in the pool. workers_.size() stays at the start() size on every
  // exit path. A thrown std::thread constructor leaves the executor unchanged.
  WorkerHandle replacement;
  try
  {
    replacement = this->createWorkerHandle();
  }
  catch (...)
  {
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }
  if (!replacement.thread.joinable() || replacement.control == nullptr)
  {
    this->discardUninstalledWorker(replacement);
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }

  try
  {
    found->control->abandonedAdapterId = adapterId;
  }
  catch (...)
  {
    this->discardUninstalledWorker(replacement);
    if (perCount == 0)
    {
      this->state_->abandonedPerAdapter.erase(adapterId);
    }
    return false;
  }
  found->control->decrementOnExit.store(true, std::memory_order_release);

  // Install the replacement into the same vector slot (size stays 4), then
  // detach the swapped-out victim. Swap of std::thread / shared_ptr is
  // noexcept, so this cannot leak a joinable thread or shrink the pool.
  WorkerHandle old;
  old.control = std::move(found->control);
  old.thread = std::move(found->thread);
  found->control = std::move(replacement.control);
  found->thread = std::move(replacement.thread);

  if (old.control)
  {
    old.control->stayLive.store(false, std::memory_order_release);
  }
  if (old.thread.joinable())
  {
    old.thread.detach();
  }

  ++this->state_->abandonedCount;
  ++perCount;
  this->state_->cv.notify_all();
  return true;
}

bool LifecycleExecutor::enqueue(LifecycleJob job)
{
  if (job.adapterId.empty())
  {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    if (!this->state_->running || this->state_->stopping)
    {
      return false;
    }
    AdapterSlot &slot = this->state_->slots[job.adapterId];
    if (job.op != LifecycleOp::Teardown && job.generation != slot.generation)
    {
      return false;
    }
    if (this->shouldCoalesceLocked(slot, job, true))
    {
      return true;
    }
    slot.pending.push_back(std::move(job));
  }
  this->state_->cv.notify_one();
  return true;
}

bool LifecycleExecutor::enqueueFollowUp(LifecycleJob job)
{
  if (job.adapterId.empty())
  {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    if (!this->state_->running || this->state_->stopping)
    {
      return false;
    }
    AdapterSlot &slot = this->state_->slots[job.adapterId];
    if (job.op != LifecycleOp::Teardown && job.generation != slot.generation)
    {
      return false;
    }
    if (this->shouldCoalesceLocked(slot, job, false))
    {
      return true;
    }
    slot.pending.push_back(std::move(job));
  }
  this->state_->cv.notify_one();
  return true;
}

bool LifecycleExecutor::currentGenerationBusy(const AdapterSlot &slot)
{
  return slot.inFlight && slot.inFlightGeneration == slot.generation;
}

bool LifecycleExecutor::takeNextJobLocked(State &state, LifecycleJob *out)
{
  for (auto &entry : state.slots)
  {
    AdapterSlot &slot = entry.second;
    if (currentGenerationBusy(slot) || slot.pending.empty())
    {
      continue;
    }
    while (!slot.pending.empty()
           && slot.pending.front().op != LifecycleOp::Teardown
           && slot.pending.front().generation != slot.generation)
    {
      slot.pending.pop_front();
    }
    if (slot.pending.empty())
    {
      continue;
    }
    *out = slot.pending.front();
    slot.pending.pop_front();
    slot.inFlight = true;
    slot.inFlightOp = out->op;
    slot.inFlightGeneration = out->generation;
    slot.inFlightWorker = std::this_thread::get_id();
    ++state.inFlightCount;
    return true;
  }
  return false;
}

void LifecycleExecutor::completeJob(State &state, const LifecycleJob &job)
{
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    AdapterSlot &slot = state.slots[job.adapterId];
    // A stale in-flight RecoveryConnect may still be running after isolation
    // replaced inFlightGeneration with the new generation's job. Only the
    // tracked occupant may clear the flag.
    if (slot.inFlight && slot.inFlightGeneration == job.generation)
    {
      slot.inFlight = false;
      slot.inFlightWorker = std::thread::id{};
    }
    if (state.inFlightCount > 0)
    {
      --state.inFlightCount;
    }
    notify = state.stopping || hasFifoRunnableLocked(state)
        || !state.deferredTeardowns.empty();
  }
  if (notify)
  {
    state.cv.notify_all();
  }
}

void LifecycleExecutor::workerMain(
    const std::shared_ptr<State> &state,
    const std::shared_ptr<WorkerControl> &control)
{
  for (;;)
  {
    if (!control->stayLive.load(std::memory_order_acquire))
    {
      if (control->decrementOnExit.load(std::memory_order_acquire))
      {
        accountAbandonedExit(*state, control->abandonedAdapterId);
      }
      return;
    }

    LifecycleJob job;
    bool gotJob = false;
    bool deferredAttempt = false;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      auto pred = [&]() {
        if (!state->running && !state->stopping)
        {
          return true;
        }
        if (hasFifoRunnableLocked(*state))
        {
          return true;
        }
        if (state->stopping && state->inFlightCount == 0
            && state->deferredTeardowns.empty())
        {
          return true;
        }
        return false;
      };

      if (state->deferredTeardowns.empty())
      {
        state->cv.wait(lock, pred);
      }
      else
      {
        state->cv.wait_for(lock, kDeferredTeardownRetry, pred);
      }

      if (!control->stayLive.load(std::memory_order_acquire))
      {
        const bool decrement =
            control->decrementOnExit.load(std::memory_order_acquire);
        const std::string abandonedId = control->abandonedAdapterId;
        lock.unlock();
        if (decrement)
        {
          accountAbandonedExit(*state, abandonedId);
        }
        return;
      }
      if (!state->running && !state->stopping)
      {
        return;
      }
      gotJob = takeNextJobLocked(*state, &job);
      if (!gotJob && !state->deferredTeardowns.empty())
      {
        job = std::move(state->deferredTeardowns.front());
        state->deferredTeardowns.pop_front();
        deferredAttempt = true;
      }
      if (!gotJob && !deferredAttempt)
      {
        if (state->stopping && state->inFlightCount == 0
            && state->deferredTeardowns.empty())
        {
          return;
        }
        continue;
      }
    }

    if (deferredAttempt)
    {
      if (tryRunTeardown(job))
      {
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          AdapterSlot &slot = state->slots[job.adapterId];
          slot.inFlight = true;
          slot.inFlightOp = LifecycleOp::Teardown;
          slot.inFlightGeneration = job.generation;
          slot.inFlightWorker = std::this_thread::get_id();
          ++state->inFlightCount;
        }
        completeJob(*state, job);
      }
      else
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->deferredTeardowns.push_back(std::move(job));
      }
      continue;
    }

    if (job.op == LifecycleOp::Teardown)
    {
      if (!tryRunTeardown(job))
      {
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          state->deferredTeardowns.push_back(job);
        }
        completeJob(*state, job);
        continue;
      }
    }
    else
    {
      Handler handler;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        handler = state->handler;
      }
      if (handler)
      {
        handler(job);
      }
    }
    completeJob(*state, job);
  }
}

}  // namespace icp
}  // namespace virtual_factory
