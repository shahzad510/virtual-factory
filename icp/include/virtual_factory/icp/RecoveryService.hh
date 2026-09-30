#ifndef VIRTUAL_FACTORY_ICP_RECOVERY_SERVICE_HH_
#define VIRTUAL_FACTORY_ICP_RECOVERY_SERVICE_HH_

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

namespace virtual_factory
{
namespace icp
{

/// Existing automatic-retry policy (do not redesign in Phase 1):
/// backoff field starts at 1s; after a failed logical attempt it doubles
/// (2s, 4s, 8s) then caps at 10s. The wait after a failure uses the doubled
/// value (same as ApplicationService::applyConnectOutcome).
inline constexpr std::chrono::milliseconds kRecoveryInitialBackoff{1000};
inline constexpr std::chrono::milliseconds kRecoveryMaxBackoff{10000};

enum class RecoveryState
{
  Disabled,
  Connected,
  RecoveryWait,
  Probing
};

const char *recoveryStateName(RecoveryState state);

/// Injectable clock so RecoveryControl tests are deterministic.
class RecoveryClock
{
public:
  virtual ~RecoveryClock() = default;
  virtual std::chrono::steady_clock::time_point now() const = 0;
};

class SteadyRecoveryClock : public RecoveryClock
{
public:
  std::chrono::steady_clock::time_point now() const override;
};

/// Test clock. start() of RecoveryService is not used with this clock —
/// tests call tick() after set/advance.
class FakeRecoveryClock : public RecoveryClock
{
public:
  FakeRecoveryClock();
  explicit FakeRecoveryClock(std::chrono::steady_clock::time_point start);
  void set(std::chrono::steady_clock::time_point tp);
  void advance(std::chrono::milliseconds delta);
  std::chrono::steady_clock::time_point now() const override;

private:
  std::chrono::steady_clock::time_point current_;
};

/// Authoritative per-adapter automatic recovery record (Phase 1).
/// Not a retry-job queue: one control, one nextAttemptAt.
struct RecoveryControl
{
  bool desired{false};
  RecoveryState state{RecoveryState::Disabled};
  std::chrono::steady_clock::time_point nextAttemptAt{};
  std::chrono::milliseconds backoffMs{kRecoveryInitialBackoff};
  std::size_t consecutiveFailures{0};
  std::uint64_t recoverySequence{0};
  std::uint64_t runtimeEpoch{0};
  std::uint64_t activeProbeId{0};
};

struct RecoveryControlSnapshot
{
  std::string adapterId;
  bool desired{false};
  RecoveryState state{RecoveryState::Disabled};
  std::chrono::steady_clock::time_point nextAttemptAt{};
  std::chrono::milliseconds backoffMs{kRecoveryInitialBackoff};
  std::size_t consecutiveFailures{0};
  std::uint64_t recoverySequence{0};
  std::uint64_t runtimeEpoch{0};
  std::uint64_t activeProbeId{0};
};

/// Phase 1 ProbeExecutor boundary: a logical recovery attempt is due.
/// RecoveryService does not call connect() or enqueue protocol work.
struct RecoveryDueEvent
{
  std::string adapterId;
  std::uint64_t probeId{0};
  std::uint64_t runtimeEpoch{0};
  std::uint64_t recoverySequence{0};
};

/// ICP Recovery V2 Phase 1: state + timer ownership for automatic recovery.
///
/// - One RecoveryControl per adapterId (map, not a growing retry queue).
/// - Automatic retry is nextAttemptAt, not queued RecoveryConnect jobs.
/// - Control thread never performs protocol I/O or waits on io_mutex.
/// - ProbeExecutor / ProbeSession / quarantine are NOT implemented here.
/// - LifecycleExecutor RecoveryConnect remains the TRANSITIONAL I/O path
///   consumed by ApplicationService from dueProbes(); this service does not
///   enqueue those jobs itself.
class RecoveryService
{
public:
  RecoveryService();
  explicit RecoveryService(std::shared_ptr<RecoveryClock> clock);
  ~RecoveryService();

  RecoveryService(const RecoveryService &) = delete;
  RecoveryService &operator=(const RecoveryService &) = delete;

  /// Start the lightweight control thread (real clock). No-op if already
  /// running. Tests with FakeRecoveryClock should call tick() instead.
  void start();

  /// Stop the control thread. Does not wait for protocol I/O (none in Phase 1).
  /// Returns after the thread joins. Prevents new due transitions.
  void stop();

  bool running() const;

  /// desired=true, RECOVERY_WAIT. immediate: nextAttemptAt=now (startup /
  /// rematerialize). Otherwise nextAttemptAt=now+backoff.
  void armDesired(const std::string &adapterId, bool immediate);

  /// Sticky operator/config intent. false clears the schedule (DISABLED).
  void setDesired(const std::string &adapterId, bool desired);

  /// Disable automatic recovery and clear the timer. Control remains so a
  /// later armDesired can reuse the id; desired=false.
  void disable(const std::string &adapterId);

  /// Drop the control entirely so no stale timer can resurrect recovery.
  void remove(const std::string &adapterId);

  /// New runtime generation. Cancels an active logical probe. Keeps desired
  /// and backoff. Old runtimeEpoch values must not schedule the new runtime.
  void rematerialize(const std::string &adapterId);

  /// ProbeExecutor (future) / transitional I/O reports success.
  void noteSuccess(const std::string &adapterId);

  /// ProbeExecutor (future) / transitional I/O reports failure: increment
  /// consecutiveFailures, double backoff (cap 10s), RECOVERY_WAIT.
  void noteFailure(const std::string &adapterId);

  /// Runtime became CONNECTED (poll/lifecycle observation). Idempotent.
  void notifyConnected(const std::string &adapterId);

  /// Dependency unavailable (FAULTED / DISCONNECTED while desired).
  /// From CONNECTED/DISABLED(with desired): enter RECOVERY_WAIT.
  /// Already waiting/probing: no timer reset (no duplicate schedule).
  void notifyUnavailable(const std::string &adapterId);

  /// ioBusy / non-protocol defer: wait current backoff, do not double.
  void deferRetry(const std::string &adapterId);

  /// Transitional: a RecoveryConnect was dropped (generation bump) while
  /// still desired. Return to RECOVERY_WAIT with nextAttemptAt=now so a
  /// later tick can emit a new logical due without a retry queue.
  void releaseLogicalProbe(const std::string &adapterId);

  /// Drive due timers. Tests call this after FakeRecoveryClock::advance.
  /// The control thread also calls it. Returns newly due events this tick
  /// (at most one per adapter). Does not accumulate a retry queue.
  std::vector<RecoveryDueEvent> tick();

  RecoveryControlSnapshot snapshot(const std::string &adapterId) const;
  std::vector<RecoveryControlSnapshot> snapshots() const;
  std::size_t controlCount() const;

  /// Current PROBING adapters (size ≤ controlCount, never grows with time).
  std::vector<RecoveryDueEvent> dueProbes() const;

  /// Test/diagnostics: number of due events emitted since construct/reset.
  std::uint64_t dueEventCount() const;
  std::uint64_t dueEventCount(const std::string &adapterId) const;

  /// Optional observer invoked without holding the service mutex after a
  /// new due event. Must not perform protocol I/O or wait on io_mutex.
  void setDueObserver(std::function<void(const RecoveryDueEvent &)> observer);

private:
  struct ControlEntry
  {
    RecoveryControl control;
    std::uint64_t dueEvents{0};
  };

  RecoveryControlSnapshot snapshotOf(
      const std::string &adapterId, const RecoveryControl &control) const;
  ControlEntry &ensureLocked(const std::string &adapterId);
  std::vector<RecoveryDueEvent> collectDueLocked(
      std::chrono::steady_clock::time_point now);
  void enterWaitLocked(
      ControlEntry &entry, std::chrono::steady_clock::time_point when);
  void clearScheduleLocked(ControlEntry &entry);
  void resetBackoffLocked(ControlEntry &entry);
  static std::chrono::milliseconds doubledBackoff(
      std::chrono::milliseconds current);
  std::chrono::steady_clock::time_point earliestDueLocked() const;
  void notifyThreadLocked();
  void threadMain();
  std::chrono::steady_clock::time_point nowLocked() const;

  std::shared_ptr<RecoveryClock> clock_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool running_{false};
  bool stopping_{false};
  std::thread thread_;
  std::unordered_map<std::string, ControlEntry> controls_;
  std::uint64_t nextProbeId_{1};
  std::uint64_t dueEventCount_{0};
  std::function<void(const RecoveryDueEvent &)> dueObserver_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
