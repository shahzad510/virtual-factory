#ifndef VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_
#define VIRTUAL_FACTORY_ICP_LIFECYCLE_EXECUTOR_HH_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace virtual_factory
{
namespace icp
{

/// Lifecycle operations that may block on industrial I/O (connect/teardown).
/// Serialized per adapter; different adapters may run concurrently.
enum class LifecycleOp
{
  Connect,
  Disconnect,
  Reconnect,
  RecoveryConnect
};

struct LifecycleJob
{
  std::string adapterId;
  LifecycleOp op{LifecycleOp::Connect};
  /// Must match the adapter's current generation when the job runs, otherwise
  /// the job is dropped (e.g. after explicit Disconnect bumped the generation).
  std::uint64_t generation{0};
};

/// Owned worker pool for adapter lifecycle I/O.
///
/// - Per-adapter FIFO: at most one in-flight job per adapterId.
/// - Cross-adapter parallelism: workers pull independent adapters concurrently.
/// - Generation tokens invalidate stale recovery/connect after Disconnect.
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

  /// Stop accepting work, drain in-flight jobs (bounded wait), join workers.
  void stop();

  bool running() const;

  /// Invalidate queued/stale work for this adapter; returns the new generation.
  std::uint64_t bumpGeneration(const std::string &adapterId);

  std::uint64_t generation(const std::string &adapterId) const;

  /// Enqueue a job. Duplicate connect-style / Reconnect work for the same
  /// adapter+generation is coalesced (including against an in-flight job of the
  /// same kind). Disconnect is never coalesced. Returns false if stopped or the
  /// job generation is stale.
  bool enqueue(LifecycleJob job);

  /// True when this adapter already has the given op in-flight or pending
  /// (any generation for in-flight; pending matches current slot generation).
  /// Used by ApplicationService to avoid bumpGeneration+duplicate Reconnect.
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

  void workerMain();
  bool takeNextJobLocked(LifecycleJob *out);
  void completeJob(const std::string &adapterId);
  static bool isConnectStyle(LifecycleOp op);
  /// Caller holds mutex_.
  bool shouldCoalesceLocked(const AdapterSlot &slot, const LifecycleJob &job) const;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool running_{false};
  bool stopping_{false};
  Handler handler_;
  std::unordered_map<std::string, AdapterSlot> slots_;
  std::vector<std::thread> workers_;
  std::size_t inFlightCount_{0};
};

}  // namespace icp
}  // namespace virtual_factory

#endif
