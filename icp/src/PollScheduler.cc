#include <virtual_factory/icp/PollScheduler.hh>

namespace virtual_factory
{
namespace icp
{

PollScheduler::PollScheduler(
    AdapterManager &manager,
    LiveStateCache &cache,
    std::chrono::milliseconds interval)
    : interval_(interval)
{
  // Test convenience: wrap raw refs in non-owning shared_ptr aliases so the
  // executor keep-alive graph stays shared_ptr-based. Callers must outlive
  // this scheduler (same contract as the previous raw-reference API).
  this->manager_ = std::shared_ptr<AdapterManager>(
      &manager, [](AdapterManager *) {});
  this->cache_ = std::shared_ptr<LiveStateCache>(
      &cache, [](LiveStateCache *) {});
  this->executor_ = std::make_shared<PollExecutor>(this->manager_, this->cache_);
  this->owns_executor_ = true;
}

PollScheduler::PollScheduler(
    std::shared_ptr<AdapterManager> manager,
    std::shared_ptr<LiveStateCache> cache,
    std::shared_ptr<PollExecutor> executor,
    std::chrono::milliseconds interval)
    : manager_(std::move(manager)),
      cache_(std::move(cache)),
      executor_(std::move(executor)),
      owns_executor_(false),
      interval_(interval)
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
  if (this->executor_ && !this->executor_->running())
  {
    this->executor_->start(kDefaultPollWorkerCount);
  }
  this->thread_ = std::thread([this] { this->threadMain(); });
}

void PollScheduler::stop()
{
  if (!this->running_.exchange(false))
  {
    if (this->thread_.joinable())
    {
      this->thread_.join();
    }
    if (this->owns_executor_ && this->executor_)
    {
      this->executor_->stop();
    }
    return;
  }
  this->wake_cv_.notify_all();
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }
  // Tick thread never runs protocol I/O — join is bounded by wait_for wake.
  // Owned executor (unit-test path) stops here; ApplicationService stops the
  // shared PollExecutor explicitly after the tick joins (P3 shutdown order).
  if (this->owns_executor_ && this->executor_)
  {
    this->executor_->stop();
  }
}

bool PollScheduler::running() const
{
  return this->running_.load();
}

void PollScheduler::dispatchDue()
{
  if (!this->manager_ || !this->cache_ || !this->executor_)
  {
    return;
  }
  if (!this->executor_->running())
  {
    this->executor_->start(kDefaultPollWorkerCount);
  }

  const std::vector<std::string> ids = this->manager_->snapshotAdapterIds();
  for (const std::string &id : ids)
  {
    AdapterManager::IoHandle handle = this->manager_->resolveIoHandle(id);
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    if (handle.enrolled && !handle.enrolled->load())
    {
      continue;
    }

    ConnectionState state = ConnectionState::Disconnected;
    bool sawState = false;
    {
      std::unique_lock<std::mutex> io(*handle.io_mutex, std::try_to_lock);
      if (io.owns_lock())
      {
        state = handle.adapter->connectionState();
        sawState = true;
      }
    }

    if (sawState && state == ConnectionState::Disconnected)
    {
      // Cache-only cleanup — no protocol I/O, no held io_mutex.
      this->cache_->removeAdapterEquipment(id);
      continue;
    }

    // Connected / Faulted / lock busy (lifecycle or peer poll): enqueue.
    // PollExecutor coalesces and serializes on io_mutex.
    (void)this->executor_->enqueue(id);
  }
}

void PollScheduler::runControlPlaneTick()
{
  std::function<void()> hook;
  {
    std::lock_guard<std::mutex> lock(this->hook_mutex_);
    hook = this->after_poll_hook_;
  }
  if (hook)
  {
    hook();
  }
}

void PollScheduler::pollOnce()
{
  this->dispatchDue();
  if (this->executor_)
  {
    // Synchronous helper for unit tests — not used by the production tick.
    (void)this->executor_->waitUntilIdle(std::chrono::seconds(30));
  }
  this->runControlPlaneTick();
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

void PollScheduler::setAfterPollHook(std::function<void()> hook)
{
  std::lock_guard<std::mutex> lock(this->hook_mutex_);
  this->after_poll_hook_ = std::move(hook);
}

PollExecutor &PollScheduler::executor()
{
  return *this->executor_;
}

const PollExecutor &PollScheduler::executor() const
{
  return *this->executor_;
}

void PollScheduler::threadMain()
{
  while (this->running_.load())
  {
    // Production tick: dispatch + control plane. Never wait for poll I/O.
    this->dispatchDue();
    this->runControlPlaneTick();

    std::chrono::milliseconds wait = this->interval();
    std::unique_lock<std::mutex> lock(this->wake_mutex_);
    this->wake_cv_.wait_for(lock, wait, [this] {
      return !this->running_.load();
    });
  }
}

}  // namespace icp
}  // namespace virtual_factory
