#ifndef VIRTUAL_FACTORY_ICP_PROBE_EXECUTOR_HH_
#define VIRTUAL_FACTORY_ICP_PROBE_EXECUTOR_HH_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <virtual_factory/icp/LifecycleExecutor.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// Default bounded worker count for automatic-recovery probes only.
inline constexpr std::size_t kDefaultProbeWorkerCount{4};
inline constexpr std::size_t kMinProbeWorkerCount{1};
inline constexpr std::size_t kMaxProbeWorkerCount{32};

/// Hard bound on joinable quarantined probe workers (runtime, not shutdown).
inline constexpr std::size_t kMaxQuarantinedProbeWorkers = 8;
/// Per-adapter fairness bound. Not an admission stop for new logical probes.
inline constexpr std::size_t kMaxQuarantinedProbesPerAdapter = 2;

/// Probe shutdown grace matches lifecycle/poll so well-behaved connects finish.
inline constexpr std::chrono::milliseconds kProbeShutdownGrace =
    kLifecycleShutdownGrace;

enum class ProbeOutcome
{
  Success,
  Failure,
  IoBusy,
  Cancelled,
  Timeout,
  Stale
};

/// Result published only after IndustrialAdapter::connect() has returned, or
/// after a logical timeout/cancel that never entered connect().
struct ProbeCompletion
{
  std::string adapterId;
  std::uint64_t probeId{0};
  std::uint64_t runtimeEpoch{0};
  std::uint64_t recoverySequence{0};
  ProbeOutcome outcome{ProbeOutcome::Failure};
  bool ok{false};
  bool ioBusy{false};
  std::string message;
  std::vector<std::string> equipmentIds;
  std::shared_ptr<IndustrialAdapter> adapter;
};

/// Shared completion endpoint. ProbeSession retains this so a late physical
/// return cannot dereference ApplicationService / RecoveryService after
/// shutdown. Implementations must be harmless when shutdown has started.
class ProbeCompletionSink
{
public:
  virtual ~ProbeCompletionSink() = default;
  virtual void onProbeCompleted(const ProbeCompletion &completion) = 0;
};

/// Everything required for one recovery probe to outlive the logical runtime
/// that requested it. The session stays alive until connect() returns.
///
/// Logical cancellation != physical cancellation: cancel() forbids this
/// session from controlling the current runtime; it does not interrupt SDK I/O.
class ProbeSession
{
public:
  ProbeSession(std::string adapterId,
               std::uint64_t probeId,
               std::uint64_t runtimeEpoch,
               std::uint64_t recoverySequence,
               std::shared_ptr<IndustrialAdapter> adapter,
               std::shared_ptr<std::mutex> ioMutex,
               std::shared_ptr<std::atomic<bool>> enrolled,
               std::shared_ptr<ProbeCompletionSink> sink,
               std::chrono::milliseconds timeout);

  const std::string &adapterId() const { return this->adapterId_; }
  std::uint64_t probeId() const { return this->probeId_; }
  std::uint64_t runtimeEpoch() const { return this->runtimeEpoch_; }
  std::uint64_t recoverySequence() const { return this->recoverySequence_; }
  const std::shared_ptr<IndustrialAdapter> &adapter() const
  {
    return this->adapter_;
  }
  const std::shared_ptr<std::mutex> &ioMutex() const { return this->ioMutex_; }
  const std::shared_ptr<std::atomic<bool>> &enrolled() const
  {
    return this->enrolled_;
  }
  const std::shared_ptr<ProbeCompletionSink> &sink() const
  {
    return this->sink_;
  }
  std::chrono::milliseconds timeout() const { return this->timeout_; }
  std::chrono::steady_clock::time_point startedAt() const
  {
    return this->startedAt_;
  }

  void cancel() { this->cancelled_.store(true, std::memory_order_release); }
  bool cancelled() const
  {
    return this->cancelled_.load(std::memory_order_acquire);
  }
  bool finished() const
  {
    return this->finished_.load(std::memory_order_acquire);
  }
  bool enteredConnect() const
  {
    return this->enteredConnect_.load(std::memory_order_acquire);
  }
  bool timeoutNotified() const
  {
    return this->timeoutNotified_.load(std::memory_order_acquire);
  }
  bool markTimeoutNotified()
  {
    bool expected = false;
    return this->timeoutNotified_.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel);
  }
  void markEnteredConnect()
  {
    this->enteredConnect_.store(true, std::memory_order_release);
  }
  void markFinished()
  {
    this->finished_.store(true, std::memory_order_release);
  }
  void markStarted(std::chrono::steady_clock::time_point when)
  {
    this->startedAt_ = when;
  }
  void setWorkerId(std::thread::id id) { this->workerId_ = id; }
  std::thread::id workerId() const { return this->workerId_; }

private:
  std::string adapterId_;
  std::uint64_t probeId_{0};
  std::uint64_t runtimeEpoch_{0};
  std::uint64_t recoverySequence_{0};
  std::shared_ptr<IndustrialAdapter> adapter_;
  std::shared_ptr<std::mutex> ioMutex_;
  std::shared_ptr<std::atomic<bool>> enrolled_;
  std::shared_ptr<ProbeCompletionSink> sink_;
  std::chrono::milliseconds timeout_{0};
  std::chrono::steady_clock::time_point startedAt_{};
  std::thread::id workerId_{};
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> finished_{false};
  std::atomic<bool> enteredConnect_{false};
  std::atomic<bool> timeoutNotified_{false};
};

/// Submit description for one logical recovery probe.
struct ProbeRequest
{
  std::string adapterId;
  std::uint64_t probeId{0};
  std::uint64_t runtimeEpoch{0};
  std::uint64_t recoverySequence{0};
  std::shared_ptr<IndustrialAdapter> adapter;
  std::shared_ptr<std::mutex> ioMutex;
  std::shared_ptr<std::atomic<bool>> enrolled;
  std::chrono::milliseconds timeout{std::chrono::milliseconds{2000}};
};

/// Bounded executor for automatic-recovery IndustrialAdapter::connect() only.
///
/// - Not LifecycleExecutor. Not PollExecutor.
/// - At most one logical probe per adapterId.
/// - No retry-job queue: pending occupancy is one slot per adapter.
/// - Hung SDK calls are logically cancelled; the ProbeSession keeps the
///   physical call alive. Timed-out workers may be quarantined (joinable)
///   and replaced 1-for-1 so future logical probes keep capacity.
/// - Runtime quarantine never detaches. Shutdown after grace may detach a
///   cohort with shared State + keepAlive (same P2 pin model as lifecycle).
class ProbeExecutor
{
public:
  ProbeExecutor();
  ~ProbeExecutor();

  ProbeExecutor(const ProbeExecutor &) = delete;
  ProbeExecutor &operator=(const ProbeExecutor &) = delete;

  void setSink(std::shared_ptr<ProbeCompletionSink> sink);

  void start(std::size_t workerCount = kDefaultProbeWorkerCount);

  /// Idempotent for the same adapterId+probeId. A newer probeId supersedes
  /// the previous logical identity (old session is cancelled, physical call
  /// may continue). Returns false if stopped or the request is incomplete.
  bool submit(ProbeRequest request);

  /// Logical cancel. Does not call disconnect() or destroy the adapter.
  void cancel(const std::string &adapterId);
  void cancel(const std::string &adapterId, std::uint64_t probeId);
  void cancelAll();

  /// Stop accepting work, cancel logical probes, wait up to grace for
  /// physical calls, join finished workers, then abandon remaining hung
  /// workers with keepAlive.
  void stop(std::chrono::milliseconds grace = kProbeShutdownGrace,
            std::shared_ptr<void> keepAlive = {});

  bool running() const;
  bool hasAbandonedWorkers() const;

  bool hasLogicalProbe(const std::string &adapterId) const;
  bool hasLogicalProbe(const std::string &adapterId, std::uint64_t probeId) const;
  /// True when a physical connect() for this adapterId is inside connect()
  /// on the given adapter pointer (current runtime). Stale extracted sessions
  /// do not match.
  bool hasPhysicalOn(
      const std::string &adapterId, const IndustrialAdapter *expected) const;
  /// True when any physical connect() for this adapterId is still executing,
  /// including a cancelled/extracted session. Used so operator Connect
  /// coalesces across isolation the same way any-generation RecoveryConnect did.
  bool hasPhysicalFor(const std::string &adapterId) const;

  std::uint64_t logicalProbeId(const std::string &adapterId) const;
  std::size_t logicalProbeCount() const;
  std::size_t pendingCount() const;
  std::size_t inFlightCount() const;
  std::size_t liveWorkerCount() const;
  std::size_t idleLiveWorkerCount() const;
  std::size_t quarantinedWorkerCount() const;
  std::size_t quarantinedWorkerCount(const std::string &adapterId) const;
  /// Peak pending+in-flight+logical observed (test bound for long outages).
  std::size_t peakOccupancy() const;

private:
  struct State;
  struct WorkerControl
  {
    std::atomic<bool> stayLive{true};
    std::atomic<bool> decrementOnExit{false};
    std::atomic<bool> exited{false};
    std::string quarantinedAdapterId;
  };
  struct WorkerHandle
  {
    std::thread thread;
    std::shared_ptr<WorkerControl> control;
  };
  struct AbandonedCohort
  {
    std::vector<WorkerHandle> workers;
    std::shared_ptr<void> keepAlive;
    std::shared_ptr<State> state;
  };

  static std::size_t clampWorkerCount(std::size_t workerCount);
  static void workerMain(
      const std::shared_ptr<State> &state,
      const std::shared_ptr<WorkerControl> &control);
  static bool takeNextLocked(
      State &state, std::shared_ptr<ProbeSession> *out);
  static void runProbe(
      State &state, const std::shared_ptr<ProbeSession> &session);
  static void publish(
      const std::shared_ptr<ProbeSession> &session, ProbeCompletion completion);
  static void completeSessionLocked(
      State &state, const std::shared_ptr<ProbeSession> &session);
  static void accountQuarantineExit(
      State &state, const std::string &adapterId);
  static bool canQuarantineLocked(
      const State &state, const std::string &adapterId);
  void spawnWorkerLocked();
  WorkerHandle createWorkerHandle();
  void discardUninstalledWorker(WorkerHandle &handle);
  bool tryQuarantine(const std::shared_ptr<ProbeSession> &session);
  void reapFinishedQuarantine();
  void watchdogLoop();
  static void recordOccupancyLocked(State &state);

  std::shared_ptr<State> state_;
  mutable std::mutex workers_mutex_;
  std::vector<WorkerHandle> workers_;
  std::vector<WorkerHandle> quarantined_;
  std::thread watchdog_;
  std::shared_ptr<AbandonedCohort> abandoned_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
