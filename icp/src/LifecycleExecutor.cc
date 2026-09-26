#include <virtual_factory/icp/LifecycleExecutor.hh>

#include <chrono>
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

void LifecycleExecutor::start(std::size_t workerCount)
{
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
    std::shared_ptr<State> state = this->state_;
    this->workers_.emplace_back([state]() { workerMain(state); });
  }
}

void LifecycleExecutor::runTeardown(const LifecycleJob &job)
{
  if (job.teardownAdapter == nullptr)
  {
    return;
  }
  if (job.teardownIoMutex != nullptr)
  {
    std::lock_guard<std::mutex> io(*job.teardownIoMutex);
    if (job.teardownAdapter->connectionState() != ConnectionState::Disconnected)
    {
      job.teardownAdapter->disconnect();
    }
  }
  else if (job.teardownAdapter->connectionState()
           != ConnectionState::Disconnected)
  {
    job.teardownAdapter->disconnect();
  }
}

void LifecycleExecutor::stop(
    std::chrono::milliseconds grace, std::shared_ptr<void> keepAlive)
{
  std::shared_ptr<State> state = this->state_;
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
  state->cv.notify_all();

  if (grace.count() < 0)
  {
    grace = std::chrono::milliseconds{0};
  }

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait_for(lock, grace, [&]() {
      if (state->inFlightCount != 0)
      {
        return false;
      }
      for (const auto &entry : state->slots)
      {
        if (!entry.second.pending.empty())
        {
          return false;
        }
      }
      return true;
    });
  }

  bool busy = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    busy = state->inFlightCount != 0;
    if (!busy)
    {
      for (const auto &entry : state->slots)
      {
        if (!entry.second.pending.empty())
        {
          busy = true;
          break;
        }
      }
    }
  }

  if (busy)
  {
    auto cohort = std::make_shared<AbandonedCohort>();
    cohort->workers = std::move(this->workers_);
    cohort->keepAlive = std::move(keepAlive);
    // Keep State alive for detached workers still in protocol I/O or cv.wait.
    cohort->state = state;
    this->workers_.clear();
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      this->abandoned_ = cohort;
      state->stopping = false;
    }
    for (std::thread &worker : cohort->workers)
    {
      if (worker.joinable())
      {
        worker.detach();
      }
    }
    // Replace state so a subsequent start()/dtor uses a fresh control block and
    // never destroys the abandoned workers' condition_variable.
    this->state_ = std::make_shared<State>();
    return;
  }

  for (std::thread &worker : this->workers_)
  {
    if (worker.joinable())
    {
      worker.join();
    }
  }
  this->workers_.clear();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopping = false;
    state->inFlightCount = 0;
    for (auto &entry : state->slots)
    {
      entry.second.inFlight = false;
      entry.second.pending.clear();
    }
  }
}

bool LifecycleExecutor::hasAbandonedWorkers() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  // After abandon, state_ is replaced; abandoned_ lives on the old path via
  // the cohort pointer stored before replacement — keep a side flag.
  return this->abandoned_ != nullptr;
}

bool LifecycleExecutor::running() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->running;
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
  if (slot.inFlight && slot.inFlightOp == op)
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
    }
    if (state.inFlightCount > 0)
    {
      --state.inFlightCount;
    }
    notify = !slot.pending.empty() || state.stopping;
  }
  if (notify)
  {
    state.cv.notify_all();
  }
}

void LifecycleExecutor::workerMain(const std::shared_ptr<State> &state)
{
  for (;;)
  {
    LifecycleJob job;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->cv.wait(lock, [&]() {
        if (state->stopping && state->inFlightCount == 0)
        {
          for (const auto &entry : state->slots)
          {
            if (!currentGenerationBusy(entry.second)
                && !entry.second.pending.empty())
            {
              return true;
            }
          }
          return !state->running;
        }
        if (!state->running && !state->stopping)
        {
          return true;
        }
        for (const auto &entry : state->slots)
        {
          if (!currentGenerationBusy(entry.second)
              && !entry.second.pending.empty())
          {
            return true;
          }
        }
        return false;
      });

      if (!state->running && !state->stopping)
      {
        return;
      }
      if (!takeNextJobLocked(*state, &job))
      {
        if (state->stopping && state->inFlightCount == 0)
        {
          return;
        }
        continue;
      }
    }

    if (job.op == LifecycleOp::Teardown)
    {
      runTeardown(job);
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
