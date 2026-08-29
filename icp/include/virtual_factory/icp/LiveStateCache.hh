#ifndef VIRTUAL_FACTORY_ICP_LIVE_STATE_CACHE_HH_
#define VIRTUAL_FACTORY_ICP_LIVE_STATE_CACHE_HH_

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// Telemetry point as stored in the ICP LiveStateCache.
/// Timestamps belong on cache DTOs, not on Equipment.hh.
struct CachedTelemetryPoint
{
  std::string name;
  double value{0.0};
  std::string unit;
};

/// Latest-value snapshot of one equipment instance across adapters.
/// Communication state is distinct from machine fault (Phase 6 invariant).
struct EquipmentSnapshot
{
  std::string equipmentId;
  std::string type;
  OperationalState operationalState{OperationalState::Stopped};
  bool machineFault{false};
  std::vector<CachedTelemetryPoint> telemetry;
  std::string adapterId;
  std::string protocol;
  ConnectionState communicationState{ConnectionState::Disconnected};
  std::string lastError;
  /// True when the owning adapter is not Connected (data may be outdated).
  bool stale{true};
  std::chrono::system_clock::time_point observedAtUtc{};
};

/// Thread-safe latest-value cache of normalized equipment state (ICP-1A).
///
/// Does not own Equipment instances. Snapshots are copies for consumers.
/// Does not implement CIC, historian, or event publishing.
class LiveStateCache
{
public:
  LiveStateCache() = default;

  LiveStateCache(const LiveStateCache &) = delete;
  LiveStateCache &operator=(const LiveStateCache &) = delete;

  /// Refresh all equipment exposed by a connected adapter.
  void updateFromAdapter(IndustrialAdapter &adapter);

  /// Mark all equipment from an adapter as stale (e.g. Faulted / Disconnected).
  /// Preserves last-known process values and machineFault; does not invent faults.
  void markAdapterCommunication(
      const std::string &adapterId,
      ConnectionState communicationState,
      const std::string &lastError);

  /// Drop all equipment entries owned by an adapter (e.g. after disconnect/remove).
  void removeAdapterEquipment(const std::string &adapterId);

  void clear();

  std::optional<EquipmentSnapshot> equipmentById(const std::string &id) const;
  std::vector<EquipmentSnapshot> equipment() const;
  std::size_t size() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, EquipmentSnapshot> by_id_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
