#include <virtual_factory/icp/config/ConfigurationCatalog.hh>

#include <virtual_factory/icp/config/ConfigurationValidator.hh>

#include <utility>

namespace virtual_factory
{
namespace icp
{

const IcpConfigurationDocument &ConfigurationCatalog::document() const
{
  return document_;
}

void ConfigurationCatalog::setName(std::string name)
{
  document_.name = std::move(name);
}

ConfigResult ConfigurationCatalog::load(ConfigurationRepository &repository)
{
  IcpConfigurationDocument loaded;
  const ConfigResult parsed = repository.load(&loaded);
  if (!parsed.ok)
  {
    return parsed;
  }
  document_ = std::move(loaded);
  return ConfigurationValidator::validate(document_);
}

ConfigResult ConfigurationCatalog::save(ConfigurationRepository &repository) const
{
  const ConfigResult validated = ConfigurationValidator::validate(document_);
  if (!validated.ok)
  {
    return validated;
  }
  return repository.save(document_);
}

ConfigResult ConfigurationCatalog::validate() const
{
  return ConfigurationValidator::validate(document_);
}

ConfigResult ConfigurationCatalog::upsertAdapter(AdapterConfigRecord adapter)
{
  if (adapter.adapterId.empty())
  {
    ConfigResult result;
    result.ok = false;
    result.message = "adapterId is required";
    result.issues.push_back({"/adapterId", result.message});
    return result;
  }

  IcpConfigurationDocument candidate = document_;
  bool replaced = false;
  for (AdapterConfigRecord &existing : candidate.adapters)
  {
    if (existing.adapterId == adapter.adapterId)
    {
      existing = std::move(adapter);
      replaced = true;
      break;
    }
  }
  if (!replaced)
  {
    candidate.adapters.push_back(std::move(adapter));
  }

  const ConfigResult validated = ConfigurationValidator::validate(candidate);
  if (!validated.ok)
  {
    return validated;
  }
  document_ = std::move(candidate);
  return validated;
}

ConfigResult ConfigurationCatalog::replaceDocument(IcpConfigurationDocument document)
{
  if (document.schema.empty())
  {
    document.schema = IcpConfigurationDocument::kSchemaId;
  }
  if (document.version == 0)
  {
    document.version = IcpConfigurationDocument::kCurrentVersion;
  }
  const ConfigResult validated = ConfigurationValidator::validate(document);
  if (!validated.ok)
  {
    return validated;
  }
  document_ = std::move(document);
  return validated;
}

ConfigResult ConfigurationCatalog::removeAdapter(const std::string &adapterId)
{
  if (adapterId.empty())
  {
    ConfigResult result;
    result.ok = false;
    result.message = "adapterId is required";
    result.issues.push_back({"/adapterId", result.message});
    return result;
  }

  IcpConfigurationDocument candidate = document_;
  for (auto it = candidate.adapters.begin(); it != candidate.adapters.end(); ++it)
  {
    if (it->adapterId == adapterId)
    {
      candidate.adapters.erase(it);
      document_ = std::move(candidate);
      ConfigResult result;
      result.ok = true;
      result.message = "ok";
      return result;
    }
  }

  ConfigResult result;
  result.ok = false;
  result.message = "adapter '" + adapterId + "' not found";
  result.issues.push_back({"/adapters", result.message});
  return result;
}

const AdapterConfigRecord *ConfigurationCatalog::adapter(const std::string &adapterId) const
{
  for (const AdapterConfigRecord &item : document_.adapters)
  {
    if (item.adapterId == adapterId)
    {
      return &item;
    }
  }
  return nullptr;
}

std::vector<std::string> ConfigurationCatalog::adapterIds() const
{
  std::vector<std::string> ids;
  ids.reserve(document_.adapters.size());
  for (const AdapterConfigRecord &item : document_.adapters)
  {
    ids.push_back(item.adapterId);
  }
  return ids;
}

std::vector<std::string> ConfigurationCatalog::equipmentIds() const
{
  std::vector<std::string> ids;
  for (const AdapterConfigRecord &adapter : document_.adapters)
  {
    for (const EquipmentMappingRecord &equipment : adapter.equipment)
    {
      ids.push_back(equipment.equipmentId);
    }
  }
  return ids;
}

std::vector<ConfigurationCatalog::EquipmentIndexEntry>
ConfigurationCatalog::equipmentIndex() const
{
  std::vector<EquipmentIndexEntry> index;
  for (const AdapterConfigRecord &adapter : document_.adapters)
  {
    for (const EquipmentMappingRecord &equipment : adapter.equipment)
    {
      EquipmentIndexEntry entry;
      entry.equipmentId = equipment.equipmentId;
      entry.adapterId =
          equipment.adapterId.empty() ? adapter.adapterId : equipment.adapterId;
      entry.type = equipment.type;
      entry.protocol = adapter.protocol;
      index.push_back(entry);
    }
  }
  return index;
}

std::size_t ConfigurationCatalog::adapterCount() const
{
  return document_.adapters.size();
}

}  // namespace icp
}  // namespace virtual_factory
