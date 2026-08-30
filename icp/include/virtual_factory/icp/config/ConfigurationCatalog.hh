#ifndef VIRTUAL_FACTORY_ICP_CONFIGURATION_CATALOG_HH_
#define VIRTUAL_FACTORY_ICP_CONFIGURATION_CATALOG_HH_

#include <cstddef>
#include <string>
#include <vector>

#include <virtual_factory/icp/config/ConfigurationModel.hh>
#include <virtual_factory/icp/config/ConfigurationRepository.hh>

namespace virtual_factory
{
namespace icp
{

/// In-memory configuration workspace for ICP Designer (ADD/CONFIGURE/VALIDATE/SAVE).
/// Does not connect adapters. Does not depend on MES or CIC.
class ConfigurationCatalog
{
public:
  ConfigurationCatalog() = default;

  const IcpConfigurationDocument &document() const;
  void setName(std::string name);

  ConfigResult load(ConfigurationRepository &repository);
  ConfigResult save(ConfigurationRepository &repository) const;
  ConfigResult validate() const;

  ConfigResult upsertAdapter(AdapterConfigRecord adapter);
  ConfigResult removeAdapter(const std::string &adapterId);

  const AdapterConfigRecord *adapter(const std::string &adapterId) const;
  std::vector<std::string> adapterIds() const;
  std::vector<std::string> equipmentIds() const;

  struct EquipmentIndexEntry
  {
    std::string equipmentId;
    std::string adapterId;
    std::string type;
    std::string protocol;
  };

  std::vector<EquipmentIndexEntry> equipmentIndex() const;

  std::size_t adapterCount() const;

private:
  IcpConfigurationDocument document_;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
