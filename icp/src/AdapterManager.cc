#include <virtual_factory/icp/AdapterManager.hh>

#include <utility>

namespace virtual_factory
{
namespace icp
{

AdapterManager::~AdapterManager()
{
  this->disconnectAll();
}

AdapterManagerResult AdapterManager::addAdapter(
    std::unique_ptr<IndustrialAdapter> adapter)
{
  if (adapter == nullptr)
  {
    return {false, "adapter is null"};
  }

  std::lock_guard<std::mutex> lock(this->mutex_);
  const std::string id = adapter->id();
  if (id.empty())
  {
    return {false, "adapter id is empty"};
  }
  if (this->adapters_.find(id) != this->adapters_.end())
  {
    return {false, "duplicate adapter id: " + id};
  }

  Entry entry;
  entry.adapter = std::move(adapter);
  this->adapters_.emplace(id, std::move(entry));
  return {true, "added"};
}

AdapterManagerResult AdapterManager::removeAdapter(const std::string &adapterId)
{
  std::unique_ptr<IndustrialAdapter> doomed;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->adapters_.find(adapterId);
    if (it == this->adapters_.end())
    {
      return {false, "adapter not found: " + adapterId};
    }
    doomed = std::move(it->second.adapter);
    this->adapters_.erase(it);
  }

  if (doomed != nullptr &&
      doomed->connectionState() != ConnectionState::Disconnected)
  {
    doomed->disconnect();
  }
  return {true, "removed"};
}

AdapterManagerResult AdapterManager::connectAdapter(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  if (entry == nullptr || entry->adapter == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }

  IndustrialAdapter &adapter = *entry->adapter;
  if (adapter.connectionState() == ConnectionState::Connected)
  {
    return {true, "already connected"};
  }

  if (!adapter.connect())
  {
    return {false, adapter.lastError().empty()
                       ? "connect failed"
                       : adapter.lastError()};
  }

  const AdapterManagerResult collision = this->checkEquipmentIdCollisions(adapter);
  if (!collision.ok)
  {
    adapter.disconnect();
    return collision;
  }

  return {true, "connected"};
}

AdapterManagerResult AdapterManager::disconnectAdapter(
    const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  if (entry == nullptr || entry->adapter == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }
  entry->adapter->disconnect();
  return {true, "disconnected"};
}

void AdapterManager::disconnectAll()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter != nullptr)
    {
      entry.second.adapter->disconnect();
    }
  }
}

IndustrialAdapter *AdapterManager::adapter(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  return entry == nullptr ? nullptr : entry->adapter.get();
}

const IndustrialAdapter *AdapterManager::adapter(
    const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const Entry *entry = this->findEntry(adapterId);
  return entry == nullptr ? nullptr : entry->adapter.get();
}

std::vector<std::string> AdapterManager::adapterIds() const
{
  return this->snapshotAdapterIds();
}

std::size_t AdapterManager::adapterCount() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->adapters_.size();
}

Equipment *AdapterManager::equipmentById(const std::string &equipmentId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter == nullptr)
    {
      continue;
    }
    Equipment *equipment = entry.second.adapter->equipmentById(equipmentId);
    if (equipment != nullptr)
    {
      return equipment;
    }
  }
  return nullptr;
}

std::vector<Equipment *> AdapterManager::allEquipment()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<Equipment *> out;
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter == nullptr)
    {
      continue;
    }
    for (Equipment *equipment : entry.second.adapter->equipment())
    {
      if (equipment != nullptr)
      {
        out.push_back(equipment);
      }
    }
  }
  return out;
}

std::vector<std::string> AdapterManager::snapshotAdapterIds() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<std::string> ids;
  ids.reserve(this->adapters_.size());
  for (const auto &entry : this->adapters_)
  {
    ids.push_back(entry.first);
  }
  return ids;
}

void AdapterManager::forEachAdapter(
    const std::function<void(IndustrialAdapter &)> &fn)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter != nullptr)
    {
      fn(*entry.second.adapter);
    }
  }
}

AdapterManagerResult AdapterManager::checkEquipmentIdCollisions(
    IndustrialAdapter &candidate) const
{
  for (Equipment *equipment : candidate.equipment())
  {
    if (equipment == nullptr)
    {
      continue;
    }
    const std::string &eqId = equipment->id();
    for (const auto &entry : this->adapters_)
    {
      if (entry.second.adapter.get() == &candidate)
      {
        continue;
      }
      if (entry.second.adapter == nullptr)
      {
        continue;
      }
      if (entry.second.adapter->equipmentById(eqId) != nullptr)
      {
        return {false,
                "equipment id collision: " + eqId + " also on adapter " +
                    entry.second.adapter->id()};
      }
    }
  }
  return {true, "ok"};
}

AdapterManager::Entry *AdapterManager::findEntry(const std::string &adapterId)
{
  auto it = this->adapters_.find(adapterId);
  if (it == this->adapters_.end())
  {
    return nullptr;
  }
  return &it->second;
}

const AdapterManager::Entry *AdapterManager::findEntry(
    const std::string &adapterId) const
{
  const auto it = this->adapters_.find(adapterId);
  if (it == this->adapters_.end())
  {
    return nullptr;
  }
  return &it->second;
}

}  // namespace icp
}  // namespace virtual_factory
