#include <virtual_factory/icp/LiveStateCache.hh>

#include <utility>

namespace virtual_factory
{
namespace icp
{

namespace
{

EquipmentSnapshot makeSnapshot(
    Equipment &equipment,
    IndustrialAdapter &adapter,
    bool stale)
{
  EquipmentSnapshot snap;
  snap.equipmentId = equipment.id();
  snap.type = equipment.type();
  snap.operationalState = equipment.operationalState();
  snap.machineFault = equipment.fault();
  snap.telemetry.clear();
  for (const auto &point : equipment.telemetry())
  {
    CachedTelemetryPoint cached;
    cached.name = point.name;
    cached.value = point.value;
    cached.unit = point.unit;
    snap.telemetry.push_back(std::move(cached));
  }
  snap.adapterId = adapter.id();
  snap.protocol = adapter.protocol();
  snap.communicationState = adapter.connectionState();
  snap.lastError = adapter.lastError();
  snap.stale = stale;
  snap.observedAtUtc = std::chrono::system_clock::now();
  return snap;
}

}  // namespace

void LiveStateCache::updateFromAdapter(IndustrialAdapter &adapter)
{
  const bool stale = adapter.connectionState() != ConnectionState::Connected;
  std::lock_guard<std::mutex> lock(this->mutex_);

  // Remove prior entries for this adapter that are no longer exposed.
  for (auto it = this->by_id_.begin(); it != this->by_id_.end();)
  {
    if (it->second.adapterId == adapter.id())
    {
      bool stillPresent = false;
      for (Equipment *equipment : adapter.equipment())
      {
        if (equipment != nullptr && equipment->id() == it->first)
        {
          stillPresent = true;
          break;
        }
      }
      if (!stillPresent)
      {
        it = this->by_id_.erase(it);
        continue;
      }
    }
    ++it;
  }

  for (Equipment *equipment : adapter.equipment())
  {
    if (equipment == nullptr)
    {
      continue;
    }
    this->by_id_[equipment->id()] = makeSnapshot(*equipment, adapter, stale);
  }
}

void LiveStateCache::markAdapterCommunication(
    const std::string &adapterId,
    ConnectionState communicationState,
    const std::string &lastError)
{
  const auto now = std::chrono::system_clock::now();
  const bool stale = communicationState != ConnectionState::Connected;
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (auto &entry : this->by_id_)
  {
    if (entry.second.adapterId != adapterId)
    {
      continue;
    }
    entry.second.communicationState = communicationState;
    entry.second.lastError = lastError;
    entry.second.stale = stale;
    entry.second.observedAtUtc = now;
    // machineFault and telemetry intentionally preserved.
  }
}

void LiveStateCache::removeAdapterEquipment(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (auto it = this->by_id_.begin(); it != this->by_id_.end();)
  {
    if (it->second.adapterId == adapterId)
    {
      it = this->by_id_.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void LiveStateCache::clear()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->by_id_.clear();
}

std::optional<EquipmentSnapshot> LiveStateCache::equipmentById(
    const std::string &id) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->by_id_.find(id);
  if (it == this->by_id_.end())
  {
    return std::nullopt;
  }
  return it->second;
}

std::vector<EquipmentSnapshot> LiveStateCache::equipment() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<EquipmentSnapshot> out;
  out.reserve(this->by_id_.size());
  for (const auto &entry : this->by_id_)
  {
    out.push_back(entry.second);
  }
  return out;
}

std::size_t LiveStateCache::size() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->by_id_.size();
}

}  // namespace icp
}  // namespace virtual_factory
