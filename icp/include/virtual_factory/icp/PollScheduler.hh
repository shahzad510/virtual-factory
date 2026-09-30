#ifndef VIRTUAL_FACTORY_ICP_POLL_SCHEDULER_HH_
#define VIRTUAL_FACTORY_ICP_POLL_SCHEDULER_HH_

#include <atomic>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollExecutor.hh>

namespace virtual_factory
{
namespace icp
{

/// Tick/dispatch loop for ICP polling (P3).
///
/// One lightweight scheduler thread. Does NOT call adapter.poll() or connect().
/// Dispatches due poll work to PollExecutor and runs an independent
/// ControlPlaneTick hook (recovery enqueue / observations / history sync).
class PollScheduler
{
public:
  /// Owns a PollExecutor when the caller does not supply one (unit tests).
  PollScheduler(
      AdapterManager &manager,
      LiveStateCache &cache,
      std::chrono::milliseconds interval = std::chrono::milliseconds(100));

  /// ApplicationService path: shared manager/cache + external PollExecutor.
  PollScheduler(
      std::shared_ptr<AdapterManager> manager,
      std::shared_ptr<LiveStateCache> cache,
      std::shared_ptr<PollExecutor> executor,
      std::chrono::milliseconds interval = std::chrono::milliseconds(100));

  ~PollScheduler();

  PollScheduler(const PollScheduler &) = delete;
  PollScheduler &operator=(const PollScheduler &) = delete;

  void start();
  void stop();

  bool running() const;

  /// Dispatch due polls, wait until the executor is idle (test helper), then
  /// run the control-plane hook. Production ticks never wait for polls.
  void pollOnce();

  void setInterval(std::chrono::milliseconds interval);
  std::chrono::milliseconds interval() const;

  /// Invoked after each tick dispatch (scheduler thread or pollOnce). Keep
  /// free of blocking protocol I/O. Empty by default.
  void setAfterPollHook(std::function<void()> hook);

  PollExecutor &executor();
  const PollExecutor &executor() const;

private:
  void threadMain();
  void dispatchDue();
  void runControlPlaneTick();

  std::shared_ptr<AdapterManager> manager_;
  std::shared_ptr<LiveStateCache> cache_;
  std::shared_ptr<PollExecutor> executor_;
  bool owns_executor_{false};
  mutable std::mutex interval_mutex_;
  std::chrono::milliseconds interval_;
  std::atomic<bool> running_{false};
  std::mutex wake_mutex_;
  std::condition_variable wake_cv_;
  std::thread thread_;
  std::mutex hook_mutex_;
  std::function<void()> after_poll_hook_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
