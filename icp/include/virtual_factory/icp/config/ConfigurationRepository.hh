#ifndef VIRTUAL_FACTORY_ICP_CONFIGURATION_REPOSITORY_HH_
#define VIRTUAL_FACTORY_ICP_CONFIGURATION_REPOSITORY_HH_

#include <string>

#include <virtual_factory/icp/config/ConfigurationModel.hh>

namespace virtual_factory
{
namespace icp
{

/// Replaceable persistence backend (file today, database later).
class ConfigurationRepository
{
public:
  virtual ~ConfigurationRepository() = default;

  virtual ConfigResult load(IcpConfigurationDocument *out) = 0;
  virtual ConfigResult save(const IcpConfigurationDocument &document) = 0;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
