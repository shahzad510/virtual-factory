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
  entry.adapter = std::shared_ptr<IndustrialAdapter>(std::move(adapter));
  entry.io_mutex = std::make_shared<std::mutex>();
  entry.enrolled = std::make_shared<std::atomic<bool>>(true);
  this->adapters_.emplace(id, std::move(entry));
  return {true, "added"};
}

AdapterManager::ExtractedAdapter AdapterManager::extractAdapter(
    const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto it = this->adapters_.find(adapterId);
  if (it == this->adapters_.end())
  {
    return {};
  }
  ExtractedAdapter out;
  out.adapter = std::move(it->second.adapter);
  out.io_mutex = std::move(it->second.io_mutex);
  // Mark unenrolled before erase so in-flight command paths that already hold
  // shared_ptr+io_mutex can refuse execute without taking mutex_ (lock order).
  if (it->second.enrolled)
  {
    it->second.enrolled->store(false);
  }
  this->adapters_.erase(it);
  // Release equipment ownership immediately so a rematerialized peer can claim
  // the same ids without racing the old instance's disconnect.
  this->clearEquipmentOwnershipLocked(adapterId);
  return out;
}

AdapterManagerResult AdapterManager::removeAdapter(const std::string &adapterId)
{
  ExtractedAdapter doomed = this->extractAdapter(adapterId);
  if (doomed.adapter == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }

  if (doomed.io_mutex)
  {
    std::lock_guard<std::mutex> io(*doomed.io_mutex);
    if (doomed.adapter->connectionState() != ConnectionState::Disconnected)
    {
      doomed.adapter->disconnect();
    }
  }
  else if (doomed.adapter->connectionState() != ConnectionState::Disconnected)
  {
    doomed.adapter->disconnect();
  }
  return {true, "removed"};
}

AdapterManagerResult AdapterManager::connectAdapter(const std::string &adapterId)
{
  Handle handle = this->handleFor(adapterId);
  if (handle.adapter == nullptr || handle.io_mutex == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }

  std::vector<std::string> candidateIds;
  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    if (handle.adapter->connectionState() == ConnectionState::Connected)
    {
      for (Equipment *equipment : handle.adapter->equipment())
      {
        if (equipment != nullptr)
        {
          candidateIds.push_back(equipment->id());
        }
      }
    }
    else
    {
      // Protocol I/O without manager lock — OPC UA connect must not block
      // HTTP/UI. Per-adapter io_mutex serializes against poll/disconnect on
      // this adapter only.
      if (!handle.adapter->connect())
      {
        return {false, handle.adapter->lastError().empty()
                           ? "connect failed"
                           : handle.adapter->lastError()};
      }
      for (Equipment *equipment : handle.adapter->equipment())
      {
        if (equipment != nullptr)
        {
          candidateIds.push_back(equipment->id());
        }
      }
    }
  }

  bool removed = false;
  AdapterManagerResult claim{true, "ok"};
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    Entry *entry = this->findEntry(adapterId);
    if (entry == nullptr || entry->adapter.get() != handle.adapter.get())
    {
      removed = true;
    }
    else
    {
      // Ownership / collision via manager index — never wait on a peer io_mutex.
      claim = this->claimEquipmentOwnershipLocked(adapterId, candidateIds);
    }
  }
  if (removed)
  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    handle.adapter->disconnect();
    return {false, "adapter removed during connect: " + adapterId};
  }
  if (!claim.ok)
  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    handle.adapter->disconnect();
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->clearEquipmentOwnershipLocked(adapterId);
    }
    return claim;
  }

  return {true, "connected"};
}

AdapterManagerResult AdapterManager::disconnectAdapter(
    const std::string &adapterId)
{
  Handle handle = this->handleFor(adapterId);
  if (handle.adapter == nullptr || handle.io_mutex == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->clearEquipmentOwnershipLocked(adapterId);
  }
  std::lock_guard<std::mutex> io(*handle.io_mutex);
  handle.adapter->disconnect();
  return {true, "disconnected"};
}

void AdapterManager::disconnectAll()
{
  const std::vector<Handle> handles = this->snapshotHandles();
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->equipment_owner_.clear();
  }
  for (const Handle &handle : handles)
  {
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    handle.adapter->disconnect();
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

std::string AdapterManager::ownerAdapterId(const std::string &equipmentId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const auto it = this->equipment_owner_.find(equipmentId);
  if (it == this->equipment_owner_.end())
  {
    return {};
  }
  return it->second;
}

Equipment *AdapterManager::equipmentById(const std::string &equipmentId)
{
  Handle handle;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    const auto it = this->equipment_owner_.find(equipmentId);
    if (it == this->equipment_owner_.end())
    {
      return nullptr;
    }
    Entry *entry = this->findEntry(it->second);
    if (entry == nullptr || entry->adapter == nullptr)
    {
      return nullptr;
    }
    handle = {entry->adapter, entry->io_mutex, entry->enrolled};
  }
  if (handle.adapter == nullptr || handle.io_mutex == nullptr)
  {
    return nullptr;
  }
  // Only the owning adapter's io_mutex — never a foreign adapter's.
  std::lock_guard<std::mutex> io(*handle.io_mutex);
  if (handle.enrolled && !handle.enrolled->load())
  {
    return nullptr;
  }
  return handle.adapter->equipmentById(equipmentId);
}

ManagedEquipmentCommandResult AdapterManager::executeEquipmentCommand(
    const std::string &equipmentId,
    const std::string &command,
    double parameter,
    const std::function<void(IndustrialAdapter &)> &underLockAfterExecute)
{
  ManagedEquipmentCommandResult out;

  Handle handle;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    const auto it = this->equipment_owner_.find(equipmentId);
    if (it == this->equipment_owner_.end())
    {
      out.message =
          "equipment '" + equipmentId + "' not found or not connected";
      return out;
    }
    out.adapterId = it->second;
    Entry *entry = this->findEntry(it->second);
    if (entry == nullptr || entry->adapter == nullptr
        || entry->io_mutex == nullptr)
    {
      out.message =
          "equipment '" + equipmentId + "' not found or not connected";
      return out;
    }
    handle = {entry->adapter, entry->io_mutex, entry->enrolled};
  }

  // Protocol I/O / equipment access under owner io_mutex only. Do not take
  // mutex_ here — disconnectAdapter locks manager then io (would deadlock).
  std::lock_guard<std::mutex> io(*handle.io_mutex);

  if (handle.enrolled && !handle.enrolled->load())
  {
    out.message =
        "equipment '" + equipmentId + "' not found or not connected";
    return out;
  }

  out.equipmentFound = true;
  out.adapterId = handle.adapter->id();

  if (handle.adapter->connectionState() != ConnectionState::Connected)
  {
    out.message = "adapter '" + handle.adapter->id() + "' is not connected";
    return out;
  }
  out.adapterConnected = true;

  Equipment *equipment = handle.adapter->equipmentById(equipmentId);
  if (equipment == nullptr)
  {
    out.equipmentFound = false;
    out.message =
        "equipment '" + equipmentId + "' not found or not connected";
    return out;
  }

  out.command = equipment->execute(command, parameter);
  if (underLockAfterExecute)
  {
    underLockAfterExecute(*handle.adapter);
  }
  out.message = out.command.message;
  return out;
}

std::vector<Equipment *> AdapterManager::allEquipment()
{
  std::vector<Handle> owners;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    std::unordered_map<std::string, Handle> byAdapter;
    for (const auto &owned : this->equipment_owner_)
    {
      if (byAdapter.count(owned.second) != 0)
      {
        continue;
      }
      Entry *entry = this->findEntry(owned.second);
      if (entry == nullptr || entry->adapter == nullptr)
      {
        continue;
      }
      byAdapter.emplace(
          owned.second,
          Handle{entry->adapter, entry->io_mutex, entry->enrolled});
    }
    owners.reserve(byAdapter.size());
    for (auto &entry : byAdapter)
    {
      owners.push_back(std::move(entry.second));
    }
  }

  std::vector<Equipment *> out;
  for (const Handle &handle : owners)
  {
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    for (Equipment *equipment : handle.adapter->equipment())
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

std::shared_ptr<IndustrialAdapter> AdapterManager::sharedAdapter(
    const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  return entry == nullptr ? nullptr : entry->adapter;
}

std::shared_ptr<const IndustrialAdapter> AdapterManager::sharedAdapter(
    const std::string &adapterId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const Entry *entry = this->findEntry(adapterId);
  return entry == nullptr ? nullptr : entry->adapter;
}

std::vector<std::shared_ptr<IndustrialAdapter>> AdapterManager::snapshotAdapters()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<std::shared_ptr<IndustrialAdapter>> out;
  out.reserve(this->adapters_.size());
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter != nullptr)
    {
      out.push_back(entry.second.adapter);
    }
  }
  return out;
}

void AdapterManager::forEachAdapter(
    const std::function<void(IndustrialAdapter &)> &fn)
{
  // Snapshot under manager lock; invoke under per-adapter I/O lock so poll
  // cannot race connect/disconnect on the same UA_Client / protocol session.
  const std::vector<Handle> handles = this->snapshotHandles();
  for (const Handle &handle : handles)
  {
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    fn(*handle.adapter);
  }
}

void AdapterManager::forEachAdapterNonBlocking(
    const std::function<void(IndustrialAdapter &)> &fn)
{
  // try_lock: skip adapters busy with lifecycle I/O so sibling polling continues.
  const std::vector<Handle> handles = this->snapshotHandles();
  for (const Handle &handle : handles)
  {
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    std::unique_lock<std::mutex> io(*handle.io_mutex, std::try_to_lock);
    if (!io.owns_lock())
    {
      continue;
    }
    fn(*handle.adapter);
  }
}

AdapterManagerResult AdapterManager::claimEquipmentOwnershipLocked(
    const std::string &adapterId, const std::vector<std::string> &equipmentIds)
{
  for (const std::string &eqId : equipmentIds)
  {
    if (eqId.empty())
    {
      continue;
    }
    const auto it = this->equipment_owner_.find(eqId);
    if (it != this->equipment_owner_.end() && it->second != adapterId)
    {
      return {false,
              "equipment id collision: " + eqId + " also on adapter " +
                  it->second};
    }
  }
  // Drop prior claims for this adapter, then install the current set. Reconnect
  // / already-connected refresh must not leave stale equipment ids behind.
  this->clearEquipmentOwnershipLocked(adapterId);
  for (const std::string &eqId : equipmentIds)
  {
    if (eqId.empty())
    {
      continue;
    }
    this->equipment_owner_[eqId] = adapterId;
  }
  return {true, "ok"};
}

void AdapterManager::clearEquipmentOwnershipLocked(const std::string &adapterId)
{
  for (auto it = this->equipment_owner_.begin();
       it != this->equipment_owner_.end();)
  {
    if (it->second == adapterId)
    {
      it = this->equipment_owner_.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

AdapterManager::Handle AdapterManager::handleFor(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  if (entry == nullptr || entry->adapter == nullptr)
  {
    return {};
  }
  return {entry->adapter, entry->io_mutex, entry->enrolled};
}

std::vector<AdapterManager::Handle> AdapterManager::snapshotHandles()
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<Handle> out;
  out.reserve(this->adapters_.size());
  for (auto &entry : this->adapters_)
  {
    if (entry.second.adapter != nullptr)
    {
      out.push_back(
          {entry.second.adapter, entry.second.io_mutex, entry.second.enrolled});
    }
  }
  return out;
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
