#ifndef VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_
#define VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_

#include <atomic>
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

/// Hard bound on live detached runtime workers (not the shutdown cohort).
inline constexpr std::size_t kMaxAbandonedLifecycleWorkers = 8;
/// Hard bound on live detached runtime workers for one adapterId.
inline constexpr std::size_t kMaxAbandonedPerAdapter = 2;
/// Parked Teardown retry interval. Not a busy-spin: workers sleep on cv.
inline constexpr std::chrono::milliseconds kDeferredTeardownRetry{100};

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
/// - Runtime isolation may detach a single stale in-flight worker and replace it
///   1-for-1 on the same State (live joinable count stays at the start() size).
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

  /// Queue a job behind the current in-flight work for this adapter. Does not
  /// coalesce with the in-flight job (so a follow-up Connect after a failing
  /// RecoveryConnect actually runs). Still coalesces with an existing pending
  /// job of the same kind+generation. Same generation/stop rules as enqueue().
  bool enqueueFollowUp(LifecycleJob job);

  /// True when this adapter already has the given op in-flight or pending
  /// for the current generation. A stale in-flight job after bumpGeneration
  /// is not current-generation work.
  bool hasInFlightOrPending(const std::string &adapterId, LifecycleOp op) const;

  /// True when this adapter has the given op in-flight or pending on any
  /// generation (including a stale isolated RecoveryConnect). Used for operator
  /// Connect/Reconnect coalescing so a generation bump cannot hide in-flight work.
  bool hasInFlightOrPendingAnyGeneration(
      const std::string &adapterId, LifecycleOp op) const;

  /// True when this adapter currently has the given op running on the current
  /// generation (not merely pending, not a stale isolated generation).
  bool hasInFlight(const std::string &adapterId, LifecycleOp op) const;

  /// True when the given generation currently owns an in-flight job of this op.
  bool hasInFlight(
      const std::string &adapterId, LifecycleOp op, std::uint64_t generation) const;

  /// True when this adapter currently has the given op running on any generation.
  bool hasInFlightAnyGeneration(const std::string &adapterId, LifecycleOp op) const;

  /// Construct a replacement worker first, then detach the stale in-flight
  /// worker (spawn-before-detach). If replacement construction fails the
  /// executor is unchanged: the old worker stays joinable in the pool, budgets
  /// are not incremented, and workers_.size() stays at the start() size.
  /// Only RecoveryConnect is eligible. Fails when budgets are exhausted or the
  /// identified job is no longer in-flight.
  bool abandonStaleInFlight(const std::string &adapterId, std::uint64_t generation);

  /// True when abandonStaleInFlight would succeed for this adapter/generation.
  bool canAbandonStaleInFlight(
      const std::string &adapterId, std::uint64_t generation) const;

  /// True when starting one more blocking lifecycle job would still leave at
  /// least one idle live worker reserved for control / other adapters.
  bool canAdmitBlockingIsolation() const;

  /// Idle joinable workers (not currently running an in-flight slot job).
  std::size_t idleLiveWorkerCount() const;

  /// Joinable workerMain threads owned by this executor (4 while running).
  std::size_t liveWorkerCount() const;

  /// Live detached runtime workers (not the P2 shutdown cohort).
  std::size_t abandonedWorkerCount() const;
  std::size_t abandonedWorkerCount(const std::string &adapterId) const;

private:
  struct AdapterSlot
  {
    std::uint64_t generation{0};
    bool inFlight{false};
    LifecycleOp inFlightOp{LifecycleOp::Connect};
    std::uint64_t inFlightGeneration{0};
    std::thread::id inFlightWorker{};
    std::deque<LifecycleJob> pending;
  };

  struct State;

  struct WorkerControl
  {
    std::atomic<bool> stayLive{true};
    /// When stayLive becomes false, decrement abandoned accounting only if this
    /// worker was actually installed as a runtime-abandoned replacement victim.
    /// Discarded never-installed threads must not decrement abandonedCount.
    std::atomic<bool> decrementOnExit{false};
    std::string abandonedAdapterId;
  };

  struct WorkerHandle
  {
    std::thread thread;
    std::shared_ptr<WorkerControl> control;
  };

  /// Detached workers + keepAlive + State retained until workers finish.
  struct AbandonedCohort
  {
    std::vector<WorkerHandle> workers;
    std::shared_ptr<void> keepAlive;
    std::shared_ptr<State> state;
  };

  static void workerMain(
      const std::shared_ptr<State> &state,
      const std::shared_ptr<WorkerControl> &control);
  static bool takeNextJobLocked(State &state, LifecycleJob *out);
  static void completeJob(State &state, const LifecycleJob &job);
  /// True when the current generation already has a running job. A stale
  /// in-flight job (generation bumped while connect() is blocked) must not
  /// occupy the replacement runtime's FIFO.
  static bool currentGenerationBusy(const AdapterSlot &slot);
  static bool isConnectStyle(LifecycleOp op);
  static void clearInvalidatablePendingLocked(AdapterSlot &slot);
  static bool hasFifoRunnableLocked(const State &state);
  static bool drainCompleteLocked(const State &state);
  static void accountAbandonedExit(State &state, const std::string &adapterId);
  bool shouldCoalesceLocked(
      const AdapterSlot &slot, const LifecycleJob &job, bool includeInFlight) const;
  static bool tryRunTeardown(const LifecycleJob &job);
  void spawnWorkerLocked();
  WorkerHandle createWorkerHandle();
  void discardUninstalledWorker(WorkerHandle &handle);
  bool canAbandonStaleInFlightLocked(
      const AdapterSlot &slot, std::uint64_t generation) const;
  std::size_t idleLiveWorkersLocked() const;

  std::shared_ptr<State> state_;
  mutable std::mutex workers_mutex_;
  std::vector<WorkerHandle> workers_;
  std::shared_ptr<AbandonedCohort> abandoned_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
