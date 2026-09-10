#ifndef VIRTUAL_FACTORY_ICP_POLL_SCHEDULER_HH_
#define VIRTUAL_FACTORY_ICP_POLL_SCHEDULER_HH_

#include <atomic>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>

namespace virtual_factory
{
namespace icp
{

/// Bounded poll loop over AdapterManager adapters (ICP-1A).
///
/// One scheduler thread. No per-PLC threads.
/// Connected adapters are polled; Faulted adapters refresh stale/comms markers
/// without inventing machine faults.
/// Optional after-poll hook lets ApplicationService observe transitions and run
/// ICP-owned bounded auto-reconnect (not open62541 silent reconnect).
class PollScheduler
{
public:
  PollScheduler(
      AdapterManager &manager,
      LiveStateCache &cache,
      std::chrono::milliseconds interval = std::chrono::milliseconds(100));

  ~PollScheduler();

  PollScheduler(const PollScheduler &) = delete;
  PollScheduler &operator=(const PollScheduler &) = delete;

  void start();
  void stop();

  bool running() const;

  /// Run one poll cycle synchronously (useful for tests).
  void pollOnce();

  void setInterval(std::chrono::milliseconds interval);
  std::chrono::milliseconds interval() const;

  /// Invoked after each pollOnce() (scheduler thread or test). Keep short;
  /// long work must not block forever. Empty by default.
  void setAfterPollHook(std::function<void()> hook);

private:
  void threadMain();
  void pollAdapterLocked(IndustrialAdapter &adapter);

  AdapterManager &manager_;
  LiveStateCache &cache_;
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
