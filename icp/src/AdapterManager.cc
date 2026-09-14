#include <virtual_factory/icp/AdapterManager.hh>

#include <mutex>
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
  this->adapters_.emplace(id, std::move(entry));
  return {true, "added"};
}

AdapterManagerResult AdapterManager::removeAdapter(const std::string &adapterId)
{
  Handle doomed;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->adapters_.find(adapterId);
    if (it == this->adapters_.end())
    {
      return {false, "adapter not found: " + adapterId};
    }
    doomed.adapter = std::move(it->second.adapter);
    doomed.io_mutex = std::move(it->second.io_mutex);
    this->adapters_.erase(it);
  }

  if (doomed.adapter == nullptr)
  {
    return {true, "removed"};
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

  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    if (handle.adapter->connectionState() == ConnectionState::Connected)
    {
      return {true, "already connected"};
    }

    // Protocol I/O without manager lock — OPC UA connect must not block HTTP/UI.
    // Per-adapter io_mutex serializes against poll/disconnect on this adapter.
    if (!handle.adapter->connect())
    {
      return {false, handle.adapter->lastError().empty()
                         ? "connect failed"
                         : handle.adapter->lastError()};
    }
  }

  bool removed = false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    Entry *entry = this->findEntry(adapterId);
    if (entry == nullptr || entry->adapter.get() != handle.adapter.get())
    {
      removed = true;
    }
  }
  if (removed)
  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    handle.adapter->disconnect();
    return {false, "adapter removed during connect: " + adapterId};
  }

  // Collision check without the manager map lock held across equipment walks.
  // Serialize each peer via its I/O mutex so poll cannot mutate bound_ mid-check.
  AdapterManagerResult collision{true, "ok"};
  {
    std::vector<std::string> candidateIds;
    {
      std::lock_guard<std::mutex> io(*handle.io_mutex);
      for (Equipment *equipment : handle.adapter->equipment())
      {
        if (equipment != nullptr)
        {
          candidateIds.push_back(equipment->id());
        }
      }
    }

    const std::vector<Handle> peers = this->snapshotHandles();
    for (const std::string &eqId : candidateIds)
    {
      for (const Handle &peer : peers)
      {
        if (peer.adapter == nullptr || peer.io_mutex == nullptr)
        {
          continue;
        }
        if (peer.adapter.get() == handle.adapter.get())
        {
          continue;
        }
        // try_lock: never wait on a peer mid-connect/teardown (unreachable OPC UA
        // must not stall an unrelated adapter's connect completion).
        std::unique_lock<std::mutex> io(*peer.io_mutex, std::try_to_lock);
        if (!io.owns_lock())
        {
          continue;
        }
        if (peer.adapter->equipmentById(eqId) != nullptr)
        {
          collision = {false,
                       "equipment id collision: " + eqId + " also on adapter " +
                           peer.adapter->id()};
          break;
        }
      }
      if (!collision.ok)
      {
        break;
      }
    }
  }

  if (!collision.ok)
  {
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    handle.adapter->disconnect();
    return collision;
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
  std::lock_guard<std::mutex> io(*handle.io_mutex);
  handle.adapter->disconnect();
  return {true, "disconnected"};
}

void AdapterManager::disconnectAll()
{
  const std::vector<Handle> handles = this->snapshotHandles();
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

Equipment *AdapterManager::equipmentById(const std::string &equipmentId)
{
  const std::vector<Handle> handles = this->snapshotHandles();
  for (const Handle &handle : handles)
  {
    if (handle.adapter == nullptr || handle.io_mutex == nullptr)
    {
      continue;
    }
    std::lock_guard<std::mutex> io(*handle.io_mutex);
    Equipment *equipment = handle.adapter->equipmentById(equipmentId);
    if (equipment != nullptr)
    {
      return equipment;
    }
  }
  return nullptr;
}

std::vector<Equipment *> AdapterManager::allEquipment()
{
  const std::vector<Handle> handles = this->snapshotHandles();
  std::vector<Equipment *> out;
  for (const Handle &handle : handles)
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
  // try_lock: a slow connect/teardown on one adapter must not stall poll of
  // unrelated adapters (startup independence / multi-adapter isolation).
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

AdapterManager::Handle AdapterManager::handleFor(const std::string &adapterId)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  Entry *entry = this->findEntry(adapterId);
  if (entry == nullptr || entry->adapter == nullptr)
  {
    return {};
  }
  return {entry->adapter, entry->io_mutex};
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
      out.push_back({entry.second.adapter, entry.second.io_mutex});
    }
  }
  return out;
}

AdapterManagerResult AdapterManager::checkEquipmentIdCollisions(
    IndustrialAdapter &candidate) const
{
  // Caller must hold mutex_ (connect post-check). Only inspects in-memory
  // equipment lists; protocol I/O is not performed here.
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
