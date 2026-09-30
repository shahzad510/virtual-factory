#include <virtual_factory/icp/PollExecutor.hh>

#include <deque>
#include <utility>

namespace virtual_factory
{
namespace icp
{

struct PollExecutor::State
{
  std::mutex mutex;
  std::condition_variable cv;
  bool running{false};
  bool stopping{false};
  std::shared_ptr<AdapterManager> manager;
  std::shared_ptr<LiveStateCache> cache;
  std::unordered_map<std::string, AdapterSlot> slots;
  std::deque<std::string> ready;
  std::size_t inFlightCount{0};
};

PollExecutor::PollExecutor(
    std::shared_ptr<AdapterManager> manager,
    std::shared_ptr<LiveStateCache> cache)
    : state_(std::make_shared<State>())
{
  this->state_->manager = std::move(manager);
  this->state_->cache = std::move(cache);
}

PollExecutor::~PollExecutor()
{
  this->stop();
}

void PollExecutor::start(std::size_t workerCount)
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
  this->abandoned_.reset();
  this->state_->stopping = false;
  this->state_->running = true;
  this->workers_.reserve(workerCount);
  for (std::size_t i = 0; i < workerCount; ++i)
  {
    std::shared_ptr<State> state = this->state_;
    this->workers_.emplace_back([state]() { workerMain(state); });
  }
}

bool PollExecutor::enqueue(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(this->state_->mutex);
    if (!this->state_->running || this->state_->stopping)
    {
      return false;
    }
    AdapterSlot &slot = this->state_->slots[adapterId];
    if (slot.inFlight)
    {
      slot.pending = true;
      return true;
    }
    if (slot.pending)
    {
      return true;
    }
    slot.inFlight = true;
    ++this->state_->inFlightCount;
    this->state_->ready.push_back(adapterId);
  }
  this->state_->cv.notify_one();
  return true;
}

void PollExecutor::stop(
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
    // Drop coalesced follow-ups; in-flight polls may still finish.
    for (auto &entry : state->slots)
    {
      entry.second.pending = false;
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
      return state->inFlightCount == 0 && state->ready.empty();
    });
  }

  const bool busy = [&]() {
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->inFlightCount != 0 || !state->ready.empty();
  }();

  if (busy)
  {
    auto cohort = std::make_shared<AbandonedCohort>();
    cohort->workers = std::move(this->workers_);
    cohort->keepAlive = std::move(keepAlive);
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
    // Fresh State so dtor/start never destroys abandoned workers' CV.
    auto fresh = std::make_shared<State>();
    fresh->manager = state->manager;
    fresh->cache = state->cache;
    this->state_ = std::move(fresh);
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
    state->ready.clear();
    for (auto &entry : state->slots)
    {
      entry.second.inFlight = false;
      entry.second.pending = false;
    }
  }
}

bool PollExecutor::running() const
{
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  return this->state_->running;
}

bool PollExecutor::hasAbandonedWorkers() const
{
  return this->abandoned_ != nullptr;
}

bool PollExecutor::hasInFlight(const std::string &adapterId) const
{
  if (adapterId.empty())
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(this->state_->mutex);
  const auto it = this->state_->slots.find(adapterId);
  if (it == this->state_->slots.end())
  {
    return false;
  }
  return it->second.inFlight;
}

bool PollExecutor::waitUntilIdle(std::chrono::milliseconds timeout)
{
  std::unique_lock<std::mutex> lock(this->state_->mutex);
  return this->state_->cv.wait_for(lock, timeout, [&]() {
    if (this->state_->inFlightCount != 0 || !this->state_->ready.empty())
    {
      return false;
    }
    for (const auto &entry : this->state_->slots)
    {
      if (entry.second.inFlight || entry.second.pending)
      {
        return false;
      }
    }
    return true;
  });
}

bool PollExecutor::takeNextLocked(State &state, std::string *adapterId)
{
  if (state.ready.empty())
  {
    return false;
  }
  *adapterId = state.ready.front();
  state.ready.pop_front();
  return true;
}

void PollExecutor::completeJob(State &state, const std::string &adapterId)
{
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    AdapterSlot &slot = state.slots[adapterId];
    if (slot.pending && state.running && !state.stopping)
    {
      slot.pending = false;
      // Keep inFlight / inFlightCount; schedule exactly one follow-up.
      state.ready.push_back(adapterId);
    }
    else
    {
      slot.pending = false;
      slot.inFlight = false;
      if (state.inFlightCount > 0)
      {
        --state.inFlightCount;
      }
    }
  }
  // Always wake waiters (waitUntilIdle / stop drain / workers).
  state.cv.notify_all();
}

void PollExecutor::runPollJob(State &state, const std::string &adapterId)
{
  if (!state.manager || !state.cache)
  {
    return;
  }

  AdapterManager::IoHandle handle = state.manager->resolveIoHandle(adapterId);
  if (handle.adapter == nullptr || handle.io_mutex == nullptr)
  {
    return;
  }

  if (handle.enrolled && !handle.enrolled->load())
  {
    return;
  }

  std::lock_guard<std::mutex> io(*handle.io_mutex);

  if (handle.enrolled && !handle.enrolled->load())
  {
    return;
  }

  IndustrialAdapter &adapter = *handle.adapter;
  const ConnectionState conn = adapter.connectionState();
  if (conn == ConnectionState::Disconnected)
  {
    // Cache-only cleanup under io_mutex for consistency with prior semantics.
    if (!handle.enrolled || handle.enrolled->load())
    {
      state.cache->removeAdapterEquipment(adapter.id());
    }
    return;
  }

  if (conn == ConnectionState::Connected)
  {
    adapter.poll();
    if (!handle.enrolled || handle.enrolled->load())
    {
      state.cache->updateFromAdapter(adapter);
    }
    return;
  }

  // Faulted: refresh last-known equipment markers; no invented machineFault.
  if (!handle.enrolled || handle.enrolled->load())
  {
    if (!adapter.equipment().empty())
    {
      state.cache->updateFromAdapter(adapter);
    }
    state.cache->markAdapterCommunication(
        adapter.id(), ConnectionState::Faulted, adapter.lastError());
  }
}

void PollExecutor::workerMain(const std::shared_ptr<State> &state)
{
  for (;;)
  {
    std::string adapterId;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->cv.wait(lock, [&]() {
        if (!state->running && !state->stopping)
        {
          return true;
        }
        if (!state->ready.empty())
        {
          return true;
        }
        if (state->stopping && state->inFlightCount == 0)
        {
          return true;
        }
        return false;
      });

      if (!state->running && !state->stopping)
      {
        return;
      }
      if (!takeNextLocked(*state, &adapterId))
      {
        if (state->stopping && state->inFlightCount == 0)
        {
          return;
        }
        continue;
      }
    }

    runPollJob(*state, adapterId);
    completeJob(*state, adapterId);
  }
}

}  // namespace icp
}  // namespace virtual_factory
