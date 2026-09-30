#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>

#include <utility>

namespace virtual_factory
{
namespace icp
{

std::unique_ptr<MockIndustrialAdapter> AdapterFactory::createMock(
    std::string id)
{
  return std::make_unique<MockIndustrialAdapter>(std::move(id));
}

std::unique_ptr<OpcUaIndustrialAdapter> AdapterFactory::createOpcUa(
    std::string id, OpcUaAdapterConfig config)
{
  return std::make_unique<OpcUaIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<ModbusIndustrialAdapter> AdapterFactory::createModbus(
    std::string id, ModbusAdapterConfig config)
{
  return std::make_unique<ModbusIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<RestIndustrialAdapter> AdapterFactory::createRest(
    std::string id, RestAdapterConfig config)
{
  return std::make_unique<RestIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<MqttIndustrialAdapter> AdapterFactory::createMqtt(
    std::string id, MqttAdapterConfig config)
{
  return std::make_unique<MqttIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<EtherNetIpIndustrialAdapter> AdapterFactory::createEtherNetIp(
    std::string id, EtherNetIpAdapterConfig config)
{
  return std::make_unique<EtherNetIpIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<ProfinetIndustrialAdapter> AdapterFactory::createProfinet(
    std::string id, ProfinetIndustrialAdapter::AdapterConfig config)
{
  return std::make_unique<ProfinetIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<ProfibusIndustrialAdapter> AdapterFactory::createProfibus(
    std::string id, ProfibusIndustrialAdapter::AdapterConfig config)
{
  return std::make_unique<ProfibusIndustrialAdapter>(
      std::move(id), std::move(config));
}

std::unique_ptr<ProfinetIndustrialAdapter> AdapterFactory::createProfinetFromRecord(
    const AdapterConfigRecord &record, ConfigResult *result)
{
  ProfinetIndustrialAdapter::AdapterConfig config;
  ConfigResult mapped = NativeFieldbusConfigMapper::toProfinet(record, &config);
  if (result != nullptr)
  {
    *result = mapped;
  }
  if (!mapped.ok)
  {
    return nullptr;
  }
  std::string id = record.adapterId;
  return createProfinet(std::move(id), std::move(config));
}

std::unique_ptr<ProfibusIndustrialAdapter> AdapterFactory::createProfibusFromRecord(
    const AdapterConfigRecord &record, ConfigResult *result)
{
  ProfibusIndustrialAdapter::AdapterConfig config;
  ConfigResult mapped = NativeFieldbusConfigMapper::toProfibus(record, &config);
  if (result != nullptr)
  {
    *result = mapped;
  }
  if (!mapped.ok)
  {
    return nullptr;
  }
  std::string id = record.adapterId;
  return createProfibus(std::move(id), std::move(config));
}

}  // namespace icp
}  // namespace virtual_factory
