#include <virtual_factory/icp/LifecycleExecutor.hh>

#include <chrono>
#include <utility>

namespace virtual_factory
{
namespace icp
{

LifecycleExecutor::LifecycleExecutor() = default;

LifecycleExecutor::~LifecycleExecutor()
{
  this->stop();
}

void LifecycleExecutor::setHandler(Handler handler)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->handler_ = std::move(handler);
}

void LifecycleExecutor::start(std::size_t workerCount)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->running_)
  {
    return;
  }
  if (workerCount == 0)
  {
    workerCount = 1;
  }
  this->stopping_ = false;
  this->running_ = true;
  this->workers_.reserve(workerCount);
  for (std::size_t i = 0; i < workerCount; ++i)
  {
    this->workers_.emplace_back([this]() { this->workerMain(); });
  }
}

void LifecycleExecutor::stop()
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->running_ && this->workers_.empty())
    {
      return;
    }
    this->stopping_ = true;
    this->running_ = false;
    // Drop queued connect/disconnect/reconnect — shutdown must not start new
    // connects. Preserve Teardown so extracted adapters still disconnect.
    for (auto &entry : this->slots_)
    {
      clearInvalidatablePendingLocked(entry.second);
    }
  }
  this->cv_.notify_all();

  // Bound drain: in-flight industrial I/O should finish via adapter timeouts.
  {
    std::unique_lock<std::mutex> lock(this->mutex_);
    this->cv_.wait_for(lock, std::chrono::seconds(30), [this]() {
      return this->inFlightCount_ == 0;
    });
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
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->stopping_ = false;
    this->inFlightCount_ = 0;
    for (auto &entry : this->slots_)
    {
      entry.second.inFlight = false;
      entry.second.pending.clear();
    }
  }
}

bool LifecycleExecutor::running() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->running_;
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
  std::lock_guard<std::mutex> lock(this->mutex_);
  AdapterSlot &slot = this->slots_[adapterId];
  ++slot.generation;
  // Drop queued connect/disconnect/reconnect; in-flight work checks generation
  // on exit paths in ApplicationService. Preserve Teardown — extracted adapters
  // must still disconnect under their io_mutex before destruction.
  clearInvalidatablePendingLocked(slot);
  return slot.generation;
}

std::uint64_t LifecycleExecutor::generation(const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->slots_.find(adapterId);
  if (it == this->slots_.end())
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
    const AdapterSlot &slot, const LifecycleJob &job) const
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
    // Connect and RecoveryConnect are interchangeable for coalescing: both
    // perform the same connect I/O for this generation.
    if (isConnectStyle(job.op))
    {
      return isConnectStyle(existingOp);
    }
    return existingOp == job.op;
  };

  if (slot.inFlight && matches(slot.inFlightOp, slot.inFlightGeneration))
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
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->slots_.find(adapterId);
  if (it == this->slots_.end())
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
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->running_ || this->stopping_)
    {
      return false;
    }
    AdapterSlot &slot = this->slots_[job.adapterId];
    // Teardown is not generation-gated: it owns an already-extracted instance.
    if (job.op != LifecycleOp::Teardown && job.generation != slot.generation)
    {
      return false;
    }
    if (this->shouldCoalesceLocked(slot, job))
    {
      return true;  // accepted / no-op duplicate
    }
    slot.pending.push_back(std::move(job));
  }
  this->cv_.notify_one();
  return true;
}

bool LifecycleExecutor::takeNextJobLocked(LifecycleJob *out)
{
  for (auto &entry : this->slots_)
  {
    AdapterSlot &slot = entry.second;
    if (slot.inFlight || slot.pending.empty())
    {
      continue;
    }
    // Drop stale non-Teardown jobs at the front. Teardown always runs.
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
    ++this->inFlightCount_;
    return true;
  }
  return false;
}

void LifecycleExecutor::completeJob(const std::string &adapterId)
{
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    AdapterSlot &slot = this->slots_[adapterId];
    slot.inFlight = false;
    if (this->inFlightCount_ > 0)
    {
      --this->inFlightCount_;
    }
    notify = !slot.pending.empty() || this->stopping_;
  }
  if (notify)
  {
    this->cv_.notify_all();
  }
}

void LifecycleExecutor::workerMain()
{
  for (;;)
  {
    LifecycleJob job;
    {
      std::unique_lock<std::mutex> lock(this->mutex_);
      this->cv_.wait(lock, [this]() {
        if (this->stopping_ && this->inFlightCount_ == 0)
        {
          // Check for any remaining assignable work before exit.
          for (const auto &entry : this->slots_)
          {
            if (!entry.second.inFlight && !entry.second.pending.empty())
            {
              return true;
            }
          }
          return !this->running_;
        }
        if (!this->running_ && !this->stopping_)
        {
          return true;
        }
        for (const auto &entry : this->slots_)
        {
          if (!entry.second.inFlight && !entry.second.pending.empty())
          {
            return true;
          }
        }
        return false;
      });

      if (!this->running_ && !this->stopping_)
      {
        return;
      }
      if (!this->takeNextJobLocked(&job))
      {
        if (this->stopping_ && this->inFlightCount_ == 0)
        {
          return;
        }
        continue;
      }
    }

    Handler handler;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      handler = this->handler_;
    }
    if (handler)
    {
      handler(job);
    }
    this->completeJob(job.adapterId);
  }
}

}  // namespace icp
}  // namespace virtual_factory
