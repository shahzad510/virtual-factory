#ifndef VIRTUAL_FACTORY_ICP_NATIVE_FIELDBUS_CONFIG_MAPPER_HH_
#define VIRTUAL_FACTORY_ICP_NATIVE_FIELDBUS_CONFIG_MAPPER_HH_

#include <virtual_factory/icp/config/ConfigurationModel.hh>
#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// Maps ICP-1B catalog records onto native Hilscher AdapterConfig.
///
/// Configuration represents intended topology. Runtime (cifX + firmware +
/// hardware) determines whether the bus is actually available. This mapper
/// does not talk to Hilscher, does not invent DCP/AR/DP packets, and does
/// not require hardware.
class NativeFieldbusConfigMapper
{
public:
  static ConfigResult toProfinet(
      const AdapterConfigRecord &record,
      ProfinetIndustrialAdapter::AdapterConfig *out);

  static ConfigResult toProfibus(
      const AdapterConfigRecord &record,
      ProfibusIndustrialAdapter::AdapterConfig *out);
};

}  // namespace icp
}  // namespace virtual_factory

#endif
