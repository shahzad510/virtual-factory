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
};

/// Owns IndustrialAdapter instances for the ICP runtime (ICP-1A).
///
/// One adapter = one industrial source/session (ADR-026 family). Not a
/// mega-adapter. Not MES. Not CIC. Not persistent config (ICP-1B).
///
/// Ownership: unique_ptr. Equipment* from adapters remain non-owning views.
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

  /// Invoke fn for each adapter while holding the manager lock.
  /// Prefer for short operations; poll() is bounded by adapter contracts.
  void forEachAdapter(const std::function<void(IndustrialAdapter &)> &fn);

private:
  struct Entry
  {
    std::unique_ptr<IndustrialAdapter> adapter;
  };

  AdapterManagerResult checkEquipmentIdCollisions(
      IndustrialAdapter &candidate) const;
  Entry *findEntry(const std::string &adapterId);
  const Entry *findEntry(const std::string &adapterId) const;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> adapters_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
