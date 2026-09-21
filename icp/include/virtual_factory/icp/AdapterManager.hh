#ifndef VIRTUAL_FACTORY_ICP_ADAPTER_MANAGER_HH_
#define VIRTUAL_FACTORY_ICP_ADAPTER_MANAGER_HH_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// Result of an AdapterManager mutation/connect attempt.
struct AdapterManagerResult
{
  bool ok{false};
  std::string message;
  /// True when ICP accepted lifecycle work for async execution (connect /
  /// disconnect / reconnect). Callers observe outcomes via adapter state.
  bool accepted{false};
};

/// Outcome of a managed equipment command (protocol I/O under owner io_mutex).
struct ManagedEquipmentCommandResult
{
  /// False when equipment is missing, extracted, or not in the ownership index.
  bool equipmentFound{false};
  /// False when the owning adapter is present but not Connected.
  bool adapterConnected{false};
  std::string adapterId;
  std::string message;
  CommandResult command;
};

/// Owns IndustrialAdapter instances for the ICP runtime (ICP-1A).
///
/// One adapter = one industrial source/session (ADR-026 family). Not a
/// mega-adapter. Not MES. Not CIC. Not persistent config (ICP-1B).
///
/// Ownership: shared_ptr so connect/poll/disconnect I/O can run without holding
/// the manager mutex (prevents HTTP/scheduler deadlocks on slow OPC UA peers).
/// Each adapter also has a dedicated I/O mutex so poll cannot race connect /
/// disconnect / remove / command on the same protocol client.
///
/// Equipment id collisions and cross-adapter ownership lookups use a
/// manager-level index (guarded by mutex_), never another adapter's io_mutex.
/// Equipment* from adapters remain non-owning views — command I/O must use
/// executeEquipmentCommand() so execute() runs while holding the owner lock.
class AdapterManager
{
public:
  AdapterManager() = default;
  ~AdapterManager();

  AdapterManager(const AdapterManager &) = delete;
  AdapterManager &operator=(const AdapterManager &) = delete;

  /// Take ownership of an adapter. Rejects duplicate adapter ids.
  /// Does not connect. Equipment id collisions are checked on connect().
  AdapterManagerResult addAdapter(std::unique_ptr<IndustrialAdapter> adapter);

  /// Runtime handle extracted from the manager without protocol disconnect.
  /// Caller must disconnect under io_mutex before dropping the shared_ptr
  /// (typically via LifecycleOp::Teardown).
  struct ExtractedAdapter
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    std::shared_ptr<std::mutex> io_mutex;
  };

  /// Remove from the manager map and clear equipment ownership without waiting
  /// on protocol I/O. Empty adapter if not found.
  ExtractedAdapter extractAdapter(const std::string &adapterId);

  /// Extract, then disconnect synchronously under io_mutex, then destroy.
  /// Prefer extractAdapter + LifecycleOp::Teardown on HTTP/config paths.
  AdapterManagerResult removeAdapter(const std::string &adapterId);

  AdapterManagerResult connectAdapter(const std::string &adapterId);
  AdapterManagerResult disconnectAdapter(const std::string &adapterId);

  /// Disconnect every enrolled adapter serially (unit-test helper). Prefer
  /// disconnectAllBounded on application shutdown paths.
  void disconnectAll();

  /// Disconnect every enrolled adapter without holding mutex_ during I/O.
  /// Adapters are disconnected in parallel. After grace, unfinished disconnect
  /// threads are detached; their adapter shared_ptrs remain in keepAliveOut so
  /// protocol I/O cannot use-after-free. Returns the number of adapters that
  /// had not finished disconnect when grace expired.
  std::size_t disconnectAllBounded(
      std::chrono::milliseconds grace,
      std::vector<std::shared_ptr<IndustrialAdapter>> *keepAliveOut = nullptr);

  /// Extract every enrolled adapter (clear ownership, mark unenrolled) without
  /// disconnecting. Used when shutdown abandons in-flight I/O.
  std::vector<ExtractedAdapter> extractAllAdapters();

  IndustrialAdapter *adapter(const std::string &adapterId);
  const IndustrialAdapter *adapter(const std::string &adapterId) const;

  std::vector<std::string> adapterIds() const;
  std::size_t adapterCount() const;

  /// Adapter id that currently owns equipmentId in the manager index (empty if
  /// none). Does not take any adapter io_mutex.
  std::string ownerAdapterId(const std::string &equipmentId) const;

  /// Non-owning equipment lookup via ownership index; locks only the owning
  /// adapter's io_mutex (never a foreign adapter's).
  /// Do not call Equipment::execute() on the returned pointer after this
  /// returns — use executeEquipmentCommand() for protocol command I/O.
  Equipment *equipmentById(const std::string &equipmentId);
  std::vector<Equipment *> allEquipment();

  /// Resolve ownership, lock ONLY the owning adapter's io_mutex, execute the
  /// command, then optionally refresh cache/state via underLockAfterExecute
  /// while still holding that same io_mutex. Never takes a foreign io_mutex
  /// and never holds mutex_ during protocol I/O.
  ManagedEquipmentCommandResult executeEquipmentCommand(
      const std::string &equipmentId,
      const std::string &command,
      double parameter,
      const std::function<void(IndustrialAdapter &)> &underLockAfterExecute = {});

  /// Snapshot of registered adapter ids for the poller (thread-safe copy).
  std::vector<std::string> snapshotAdapterIds() const;

  /// Shared ownership handle for one adapter (empty if missing).
  std::shared_ptr<IndustrialAdapter> sharedAdapter(const std::string &adapterId);
  std::shared_ptr<const IndustrialAdapter> sharedAdapter(
      const std::string &adapterId) const;

  /// Snapshot of adapter shared_ptrs (lock released before caller uses them).
  std::vector<std::shared_ptr<IndustrialAdapter>> snapshotAdapters();

  /// Invoke fn for each adapter WITHOUT holding the manager lock during fn.
  /// Uses a shared_ptr snapshot so removeAdapter cannot destroy under fn.
  /// Serializes each adapter's I/O against connect/disconnect for that adapter.
  void forEachAdapter(const std::function<void(IndustrialAdapter &)> &fn);

  /// Like forEachAdapter, but skips adapters whose I/O mutex is held (e.g. by
  /// lifecycle connect). Used when a caller must observe without waiting.
  void forEachAdapterNonBlocking(
      const std::function<void(IndustrialAdapter &)> &fn);

  /// Shared ownership handle for poll/lifecycle I/O (empty if missing).
  /// Caller must drop any manager mutex before protocol work and check
  /// enrolled before publishing cache updates (P0/P3).
  struct IoHandle
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    std::shared_ptr<std::mutex> io_mutex;
    std::shared_ptr<std::atomic<bool>> enrolled;
  };
  IoHandle resolveIoHandle(const std::string &adapterId);

private:
  struct Entry
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    /// Shared so poll/connect/disconnect/remove can serialize the same adapter
    /// after the manager map lock is released.
    std::shared_ptr<std::mutex> io_mutex;
    /// Cleared on extract/remove so command I/O can detect unenrollment without
    /// taking mutex_ while holding io_mutex (avoids deadlock with disconnect).
    std::shared_ptr<std::atomic<bool>> enrolled;
  };

  struct Handle
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    std::shared_ptr<std::mutex> io_mutex;
    std::shared_ptr<std::atomic<bool>> enrolled;
  };

  /// Claim equipment ids for adapterId in equipment_owner_. Caller holds mutex_.
  AdapterManagerResult claimEquipmentOwnershipLocked(
      const std::string &adapterId, const std::vector<std::string> &equipmentIds);
  /// Drop all ownership rows for adapterId. Caller holds mutex_.
  void clearEquipmentOwnershipLocked(const std::string &adapterId);

  Entry *findEntry(const std::string &adapterId);
  const Entry *findEntry(const std::string &adapterId) const;
  Handle handleFor(const std::string &adapterId);
  std::vector<Handle> snapshotHandles();

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> adapters_;
  /// equipmentId → adapterId. Updated only under mutex_ at connect/disconnect
  /// boundaries — never requires a peer adapter's io_mutex.
  std::unordered_map<std::string, std::string> equipment_owner_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
