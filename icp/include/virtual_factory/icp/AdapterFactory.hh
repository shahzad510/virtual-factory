#ifndef VIRTUAL_FACTORY_ICP_ADAPTER_FACTORY_HH_
#define VIRTUAL_FACTORY_ICP_ADAPTER_FACTORY_HH_

#include <memory>
#include <string>

#include <virtual_factory/industrial/EtherNetIpIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>
#include <virtual_factory/industrial/ModbusIndustrialAdapter.hh>
#include <virtual_factory/industrial/MqttIndustrialAdapter.hh>
#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>
#include <virtual_factory/industrial/RestIndustrialAdapter.hh>

namespace virtual_factory
{
namespace icp
{

/// In-memory factory for Phase 6 adapters (ICP-1A).
///
/// Persistent configuration / YAML / DB is ICP-1B. No hard-coded PLC ids.
/// Protocol topology is preserved: one instance = one source/session
/// (MQTT: one broker session with many GenericEquipment mappings).
class AdapterFactory
{
public:
  AdapterFactory() = delete;

  static std::unique_ptr<MockIndustrialAdapter> createMock(std::string id);

  static std::unique_ptr<OpcUaIndustrialAdapter> createOpcUa(
      std::string id, OpcUaAdapterConfig config);

  static std::unique_ptr<ModbusIndustrialAdapter> createModbus(
      std::string id, ModbusAdapterConfig config);

  static std::unique_ptr<RestIndustrialAdapter> createRest(
      std::string id, RestAdapterConfig config);

  static std::unique_ptr<MqttIndustrialAdapter> createMqtt(
      std::string id, MqttAdapterConfig config);

  static std::unique_ptr<EtherNetIpIndustrialAdapter> createEtherNetIp(
      std::string id, EtherNetIpAdapterConfig config);

  static std::unique_ptr<ProfinetIndustrialAdapter> createProfinet(
      std::string id, ProfinetIndustrialAdapter::AdapterConfig config);

  static std::unique_ptr<ProfibusIndustrialAdapter> createProfibus(
      std::string id, ProfibusIndustrialAdapter::AdapterConfig config);
};

}  // namespace icp
}  // namespace virtual_factory

#endif
