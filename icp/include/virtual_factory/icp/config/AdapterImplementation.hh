#ifndef VIRTUAL_FACTORY_ICP_ADAPTER_IMPLEMENTATION_HH_
#define VIRTUAL_FACTORY_ICP_ADAPTER_IMPLEMENTATION_HH_

#include <virtual_factory/icp/config/ConfigurationModel.hh>

#include <string>

namespace virtual_factory
{
namespace icp
{

inline bool isKnownImplementation(const std::string &implementation)
{
  return implementation == "gateway" || implementation == "hilscher_native"
         || implementation == "softing_native" || implementation == "simulated";
}

/// Resolves the active adapter stack from configured implementation + connection shape.
/// gateway | hilscher_native | softing_native | simulated
inline std::string resolveAdapterImplementation(const AdapterConfigRecord &record)
{
  if (!record.implementation.empty())
  {
    if (isKnownImplementation(record.implementation))
    {
      return record.implementation;
    }
    return record.implementation;
  }

  if (record.protocol == "mock")
  {
    return "simulated";
  }

  if (record.protocol == "opcua" || record.protocol == "modbus"
      || record.protocol == "mqtt" || record.protocol == "rest"
      || record.protocol == "ethernetip")
  {
    return "gateway";
  }

  if (record.protocol == "profinet" || record.protocol == "profibus")
  {
    const AdapterConnectionRecord &c = record.connection;
    if (!c.boardId.empty() || !c.configArtifactPath.empty())
    {
      return "hilscher_native";
    }
    if (!c.endpointUrl.empty() || !c.host.empty())
    {
      return "gateway";
    }
    return "hilscher_native";
  }

  return "gateway";
}

/// Backward compatibility: legacy native PN/PB configs without implementation field.
inline void normalizeLegacyAdapterImplementation(AdapterConfigRecord *record)
{
  if (record == nullptr || !record->implementation.empty())
  {
    return;
  }
  if (record->protocol != "profinet" && record->protocol != "profibus")
  {
    return;
  }
  const AdapterConnectionRecord &c = record->connection;
  if (!c.boardId.empty() || !c.configArtifactPath.empty())
  {
    record->implementation = "hilscher_native";
  }
}

}  // namespace icp
}  // namespace virtual_factory

#endif
