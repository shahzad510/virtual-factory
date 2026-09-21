#ifndef VIRTUAL_FACTORY_ICP_POLL_EXECUTOR_HH_
#define VIRTUAL_FACTORY_ICP_POLL_EXECUTOR_HH_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/LifecycleExecutor.hh>

namespace virtual_factory
{
namespace icp
{

/// Default poll worker count (P3). Fixed; not configuration-driven in this slice.
inline constexpr std::size_t kDefaultPollWorkerCount{4};

/// Alias: poll shutdown grace matches P2 lifecycle grace.
inline constexpr std::chrono::milliseconds kPollShutdownGrace =
    kLifecycleShutdownGrace;

/// Bounded worker pool for protocol-blind adapter poll() I/O (P3).
///
/// - Cross-adapter parallelism: different adapters may poll concurrently.
/// - Same-adapter: at most one in-flight poll; at most one coalesced pending.
/// - Protocol serialization remains adapter io_mutex (shared with lifecycle/
///   command/Teardown).
/// - Shutdown: drain up to grace, then abandon workers with shared State +
///   keepAlive (P2 pattern). Never destroys a CV while workers may wait on it.
class PollExecutor
{
public:
  PollExecutor(
      std::shared_ptr<AdapterManager> manager,
      std::shared_ptr<LiveStateCache> cache);
  ~PollExecutor();

  PollExecutor(const PollExecutor &) = delete;
  PollExecutor &operator=(const PollExecutor &) = delete;

  void start(std::size_t workerCount = kDefaultPollWorkerCount);

  /// Coalescing enqueue. Returns false if stopped or adapterId empty.
  bool enqueue(const std::string &adapterId);

  /// Stop accepting work, drop pending coalesces, wait up to grace for
  /// in-flight polls, then join or abandon with keepAlive.
  void stop(
      std::chrono::milliseconds grace = kPollShutdownGrace,
      std::shared_ptr<void> keepAlive = {});

  bool running() const;
  bool hasAbandonedWorkers() const;

  /// Test helper: wait until no in-flight and no pending polls.
  bool waitUntilIdle(std::chrono::milliseconds timeout);

private:
  struct AdapterSlot
  {
    bool inFlight{false};
    bool pending{false};
  };

  struct State;
  struct AbandonedCohort
  {
    std::vector<std::thread> workers;
    std::shared_ptr<void> keepAlive;
    std::shared_ptr<State> state;
  };

  static void workerMain(const std::shared_ptr<State> &state);
  static bool takeNextLocked(State &state, std::string *adapterId);
  static void completeJob(State &state, const std::string &adapterId);
  static void runPollJob(State &state, const std::string &adapterId);

  std::shared_ptr<State> state_;
  std::vector<std::thread> workers_;
  std::shared_ptr<AbandonedCohort> abandoned_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
