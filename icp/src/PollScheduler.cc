#include <virtual_factory/icp/PollScheduler.hh>

namespace virtual_factory
{
namespace icp
{

PollScheduler::PollScheduler(
    AdapterManager &manager,
    LiveStateCache &cache,
    std::chrono::milliseconds interval)
    : manager_(manager), cache_(cache), interval_(interval)
{
}

PollScheduler::~PollScheduler()
{
  this->stop();
}

void PollScheduler::start()
{
  if (this->running_.exchange(true))
  {
    return;
  }
  this->thread_ = std::thread([this] { this->threadMain(); });
}

void PollScheduler::stop()
{
  if (!this->running_.exchange(false))
  {
    // Still join if a prior start left a joinable thread that already exited.
    if (this->thread_.joinable())
    {
      this->thread_.join();
    }
    return;
  }
  this->wake_cv_.notify_all();
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }
}

bool PollScheduler::running() const
{
  return this->running_.load();
}

void PollScheduler::pollOnce()
{
  this->manager_.forEachAdapter(
      [this](IndustrialAdapter &adapter) { this->pollAdapterLocked(adapter); });
}

void PollScheduler::setInterval(std::chrono::milliseconds interval)
{
  std::lock_guard<std::mutex> lock(this->interval_mutex_);
  this->interval_ = interval;
  this->wake_cv_.notify_all();
}

std::chrono::milliseconds PollScheduler::interval() const
{
  std::lock_guard<std::mutex> lock(this->interval_mutex_);
  return this->interval_;
}

void PollScheduler::threadMain()
{
  while (this->running_.load())
  {
    this->pollOnce();

    std::chrono::milliseconds wait = this->interval();
    std::unique_lock<std::mutex> lock(this->wake_mutex_);
    this->wake_cv_.wait_for(lock, wait, [this] {
      return !this->running_.load();
    });
  }
}

void PollScheduler::pollAdapterLocked(IndustrialAdapter &adapter)
{
  const ConnectionState state = adapter.connectionState();
  if (state == ConnectionState::Disconnected)
  {
    this->cache_.removeAdapterEquipment(adapter.id());
    return;
  }

  if (state == ConnectionState::Connected)
  {
    // Bounded by each adapter's own poll() contract (e.g. MQTT pollTimeoutMs).
    adapter.poll();
    this->cache_.updateFromAdapter(adapter);
    return;
  }

  // Faulted: keep last-known equipment; refresh process fields if still listed;
  // mark communication Faulted / stale. Do not invent machineFault.
  if (!adapter.equipment().empty())
  {
    this->cache_.updateFromAdapter(adapter);
  }
  this->cache_.markAdapterCommunication(
      adapter.id(), ConnectionState::Faulted, adapter.lastError());
}

}  // namespace icp
}  // namespace virtual_factory
