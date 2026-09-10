#ifndef VIRTUAL_FACTORY_ICP_CONFIGURATION_VALIDATOR_HH_
#define VIRTUAL_FACTORY_ICP_CONFIGURATION_VALIDATOR_HH_

#include <virtual_factory/icp/config/ConfigurationModel.hh>

namespace virtual_factory
{
namespace icp
{

/// Protocol-neutral + protocol-specific validation. No MES types.
class ConfigurationValidator
{
public:
  static bool isSupportedProtocol(const std::string &protocol);

  static ConfigResult validate(const IcpConfigurationDocument &document);
  static ConfigResult validateAdapter(const AdapterConfigRecord &adapter);
};

}  // namespace icp
}  // namespace virtual_factory

#endif
