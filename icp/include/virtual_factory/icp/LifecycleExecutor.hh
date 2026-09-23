#ifndef VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_
#define VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// Default bound for LifecycleExecutor::stop() / ApplicationService::stop().
/// Chosen above typical default protocol timeoutMs (2000) and test blackhole
/// windows (2500) so well-behaved adapters finish cleanly; hung adapters are
/// abandoned after this grace with shared_ptr lifetime keep-alive.
inline constexpr std::chrono::milliseconds kLifecycleShutdownGrace{5000};

/// Lifecycle operations that may block on industrial I/O (connect/teardown).
/// Serialized per adapter; different adapters may run concurrently.
enum class LifecycleOp
{
  Connect,
  Disconnect,
  Reconnect,
  RecoveryConnect,
  /// Disconnect + release an already-extracted runtime adapter. Not generation-
  /// gated: config DELETE/disable/rematerialize must always finish teardown.
  Teardown
};

struct LifecycleJob
{
  std::string adapterId;
  LifecycleOp op{LifecycleOp::Connect};
  /// Must match the adapter's current generation when the job runs, otherwise
  /// the job is dropped (e.g. after explicit Disconnect bumped the generation).
  /// Ignored for Teardown (extracted instances are always torn down).
  std::uint64_t generation{0};
  /// Teardown only: runtime removed from AdapterManager, held until disconnect
  /// under teardownIoMutex completes (invariant: no destroy during protocol I/O).
  std::shared_ptr<IndustrialAdapter> teardownAdapter;
  std::shared_ptr<std::mutex> teardownIoMutex;
};

/// Owned worker pool for adapter lifecycle I/O.
///
/// - Per-adapter FIFO: at most one in-flight job per adapterId.
/// - Cross-adapter parallelism: workers pull independent adapters concurrently.
/// - Generation tokens invalidate stale recovery/connect after Disconnect.
/// - Shutdown is bounded: after the grace period, workers may be detached while
///   keepAlive + shared State keep referenced adapters / condition_variables alive.
/// - Not PollScheduler: poll must never call blocking connect().
class LifecycleExecutor
{
public:
  using Handler = std::function<void(const LifecycleJob &)>;

  LifecycleExecutor();
  ~LifecycleExecutor();

  LifecycleExecutor(const LifecycleExecutor &) = delete;
  LifecycleExecutor &operator=(const LifecycleExecutor &) = delete;

  void setHandler(Handler handler);

  /// Start worker pool (default sized for concurrent multi-adapter recovery).
  void start(std::size_t workerCount = 4);

  /// Stop accepting work, drain in-flight jobs up to grace, then join or
  /// abandon. keepAlive is retained if workers are detached so protocol I/O
  /// can finish against still-living adapter/runtime state.
  void stop(std::chrono::milliseconds grace = kLifecycleShutdownGrace,
            std::shared_ptr<void> keepAlive = {});

  bool running() const;

  /// True when the last stop() detached workers that have not yet finished.
  bool hasAbandonedWorkers() const;

  /// Invalidate queued/stale work for this adapter; returns the new generation.
  /// Pending Teardown jobs are preserved (extracted adapters must still disconnect).
  std::uint64_t bumpGeneration(const std::string &adapterId);

  std::uint64_t generation(const std::string &adapterId) const;

  /// Enqueue a job. Duplicate connect-style / Reconnect work for the same
  /// adapter+generation is coalesced (including against an in-flight job of the
  /// same kind). Disconnect and Teardown are never coalesced. Teardown ignores
  /// generation staleness. Returns false if stopped or (non-Teardown) generation
  /// is stale.
  bool enqueue(LifecycleJob job);

  /// True when this adapter already has the given op in-flight or pending
  /// (any generation for in-flight; pending matches current slot generation).
  /// Used by ApplicationService to avoid bumpGeneration on redundant Reconnect
  /// while Connect, RecoveryConnect, or Reconnect is already running.
  bool hasInFlightOrPending(const std::string &adapterId, LifecycleOp op) const;

private:
  struct AdapterSlot
  {
    std::uint64_t generation{0};
    bool inFlight{false};
    LifecycleOp inFlightOp{LifecycleOp::Connect};
    std::uint64_t inFlightGeneration{0};
    std::deque<LifecycleJob> pending;
  };

  struct State;

  /// Detached workers + keepAlive + State retained until workers finish.
  struct AbandonedCohort
  {
    std::vector<std::thread> workers;
    std::shared_ptr<void> keepAlive;
    std::shared_ptr<State> state;
  };

  static void workerMain(const std::shared_ptr<State> &state);
  static bool takeNextJobLocked(State &state, LifecycleJob *out);
  static void completeJob(State &state, const std::string &adapterId);
  static bool isConnectStyle(LifecycleOp op);
  static void clearInvalidatablePendingLocked(AdapterSlot &slot);
  bool shouldCoalesceLocked(const AdapterSlot &slot, const LifecycleJob &job) const;
  static void runTeardown(const LifecycleJob &job);

  std::shared_ptr<State> state_;
  std::vector<std::thread> workers_;
  std::shared_ptr<AbandonedCohort> abandoned_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
