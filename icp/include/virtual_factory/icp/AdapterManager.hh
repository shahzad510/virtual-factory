#ifndef VIRTUAL_FACTORY_ICP_ADAPTER_MANAGER_HH_
#define VIRTUAL_FACTORY_ICP_ADAPTER_MANAGER_HH_

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

/// Owns IndustrialAdapter instances for the ICP runtime (ICP-1A).
///
/// One adapter = one industrial source/session (ADR-026 family). Not a
/// mega-adapter. Not MES. Not CIC. Not persistent config (ICP-1B).
///
/// Ownership: shared_ptr so connect/poll/disconnect I/O can run without holding
/// the manager mutex (prevents HTTP/scheduler deadlocks on slow OPC UA peers).
/// Each adapter also has a dedicated I/O mutex so poll cannot race connect /
/// disconnect / remove on the same protocol client.
/// Equipment* from adapters remain non-owning views.
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

  /// Disconnect (if needed) and destroy the adapter.
  AdapterManagerResult removeAdapter(const std::string &adapterId);

  AdapterManagerResult connectAdapter(const std::string &adapterId);
  AdapterManagerResult disconnectAdapter(const std::string &adapterId);

  void disconnectAll();

  IndustrialAdapter *adapter(const std::string &adapterId);
  const IndustrialAdapter *adapter(const std::string &adapterId) const;

  std::vector<std::string> adapterIds() const;
  std::size_t adapterCount() const;

  /// Non-owning equipment lookup across connected (or Faulted) adapters.
  Equipment *equipmentById(const std::string &equipmentId);
  std::vector<Equipment *> allEquipment();

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
  /// lifecycle connect). Used by PollScheduler so sibling polling continues.
  void forEachAdapterNonBlocking(
      const std::function<void(IndustrialAdapter &)> &fn);

private:
  struct Entry
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    /// Shared so poll/connect/disconnect/remove can serialize the same adapter
    /// after the manager map lock is released.
    std::shared_ptr<std::mutex> io_mutex;
  };

  struct Handle
  {
    std::shared_ptr<IndustrialAdapter> adapter;
    std::shared_ptr<std::mutex> io_mutex;
  };

  AdapterManagerResult checkEquipmentIdCollisions(
      IndustrialAdapter &candidate) const;
  Entry *findEntry(const std::string &adapterId);
  const Entry *findEntry(const std::string &adapterId) const;
  Handle handleFor(const std::string &adapterId);
  std::vector<Handle> snapshotHandles();

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> adapters_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
