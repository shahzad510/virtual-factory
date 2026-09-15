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
    // Drop queued (not in-flight) work — shutdown must not start new connects.
    for (auto &entry : this->slots_)
    {
      entry.second.pending.clear();
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

std::uint64_t LifecycleExecutor::bumpGeneration(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  AdapterSlot &slot = this->slots_[adapterId];
  ++slot.generation;
  // Drop queued jobs for this adapter; in-flight work checks generation on exit
  // paths in ApplicationService and will not schedule follow-on recovery.
  slot.pending.clear();
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
    if (job.generation != slot.generation)
    {
      return false;
    }
    if (job.op == LifecycleOp::RecoveryConnect)
    {
      for (const LifecycleJob &pending : slot.pending)
      {
        if (pending.op == LifecycleOp::RecoveryConnect
            && pending.generation == job.generation)
        {
          return true;  // coalesced
        }
      }
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
    // Drop stale jobs at the front.
    while (!slot.pending.empty()
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
