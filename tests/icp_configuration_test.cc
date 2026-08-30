#include <virtual_factory/icp/config/ConfigurationCatalog.hh>
#include <virtual_factory/icp/config/ConfigurationValidator.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

void expect(bool condition, const std::string &message)
{
  expect(condition, message.c_str());
}

bool contains(const virtual_factory::icp::ConfigResult &result, const std::string &needle)
{
  if (result.message.find(needle) != std::string::npos)
  {
    return true;
  }
  for (const virtual_factory::icp::ConfigIssue &issue : result.issues)
  {
    if (issue.message.find(needle) != std::string::npos
        || issue.path.find(needle) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

std::string tempDir()
{
  std::ostringstream stream;
  stream << "/tmp/icp-1b-" << ::getpid();
  const std::string path = stream.str();
  ::mkdir(path.c_str(), 0700);
  return path;
}

virtual_factory::icp::AdapterConfigRecord mockAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "mock-a";
  adapter.protocol = "mock";
  adapter.description = "in-process mock";
  virtual_factory::icp::EquipmentMappingRecord pump;
  pump.equipmentId = "PUMP-001";
  pump.adapterId = "mock-a";
  pump.type = "pump";
  pump.capabilities = {"start", "stop"};
  virtual_factory::icp::TelemetryMappingRecord pressure;
  pressure.name = "pressure";
  pressure.unit = "bar";
  pump.telemetry.push_back(pressure);
  adapter.equipment.push_back(pump);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord opcuaAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "opcua-line1";
  adapter.protocol = "opcua";
  adapter.connection.endpointUrl = "opc.tcp://127.0.0.1:4840";
  adapter.connection.host = "127.0.0.1";
  adapter.connection.port = 4840;
  virtual_factory::icp::EquipmentMappingRecord mixer;
  mixer.equipmentId = "MIXER-001";
  mixer.type = "mixer";
  mixer.capabilities = {"start", "stop", "set_speed"};
  virtual_factory::icp::TelemetryMappingRecord speed;
  speed.name = "speed";
  speed.address = "ns=1;s=Mixer.SpeedActual";
  speed.namespaceIndex = 1;
  speed.unit = "rpm";
  mixer.telemetry.push_back(speed);
  virtual_factory::icp::CommandMappingRecord start;
  start.command = "start";
  start.address = "ns=1;s=Mixer.Start";
  mixer.commands.push_back(start);
  mixer.state.mapped = true;
  mixer.state.address = "ns=1;s=Mixer.Running";
  mixer.fault.mapped = true;
  mixer.fault.address = "ns=1;s=Mixer.Fault";
  adapter.equipment.push_back(mixer);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord modbusAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "modbus-line1";
  adapter.protocol = "modbus";
  adapter.connection.host = "10.0.0.12";
  adapter.connection.port = 502;
  virtual_factory::icp::EquipmentMappingRecord fan;
  fan.equipmentId = "FAN-001";
  fan.type = "fan";
  virtual_factory::icp::TelemetryMappingRecord rpm;
  rpm.name = "rpm";
  rpm.table = "holding";
  rpm.registerAddress = 10;
  fan.telemetry.push_back(rpm);
  adapter.equipment.push_back(fan);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord mqttAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "mqtt-broker";
  adapter.protocol = "mqtt";
  adapter.connection.host = "127.0.0.1";
  adapter.connection.port = 1883;
  adapter.connection.clientId = "icp-1b";
  adapter.credentials.username = "icp";
  adapter.credentials.passwordRef = "env:MQTT_PASSWORD";
  virtual_factory::icp::EquipmentMappingRecord plc;
  plc.equipmentId = "PLC-MQTT-001";
  plc.type = "plc";
  virtual_factory::icp::TelemetryMappingRecord temp;
  temp.name = "temperature";
  temp.address = "plant/plc001/telemetry";
  temp.jsonPointer = "/temperature";
  plc.telemetry.push_back(temp);
  adapter.equipment.push_back(plc);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord restAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "rest-gw";
  adapter.protocol = "rest";
  adapter.connection.scheme = "http";
  adapter.connection.host = "127.0.0.1";
  adapter.connection.port = 8080;
  adapter.credentials.tokenRef = "secret:REST_BEARER";
  virtual_factory::icp::EquipmentMappingRecord oven;
  oven.equipmentId = "OVEN-001";
  oven.type = "oven";
  oven.telemetryPath = "/api/oven-001";
  virtual_factory::icp::TelemetryMappingRecord setpoint;
  setpoint.name = "setpoint";
  setpoint.jsonPointer = "/telemetry/setpoint";
  oven.telemetry.push_back(setpoint);
  adapter.equipment.push_back(oven);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord ethernetIpAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "eip-1";
  adapter.protocol = "ethernetip";
  adapter.connection.host = "192.168.1.10";
  adapter.connection.port = 44818;
  adapter.connection.plcType = "controllogix";
  adapter.connection.path = "1,0";
  virtual_factory::icp::EquipmentMappingRecord tank;
  tank.equipmentId = "TANK-001";
  tank.type = "tank";
  virtual_factory::icp::TelemetryMappingRecord level;
  level.name = "level";
  level.address = "TankLevel";
  tank.telemetry.push_back(level);
  adapter.equipment.push_back(tank);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord profinetAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "pn-controller";
  adapter.protocol = "profinet";
  adapter.connection.boardId = "cifx0";
  adapter.connection.channel = 0;
  adapter.connection.stationName = "icp-pn-controller";
  adapter.connection.configArtifactPath = "/opt/icp/pn/config.nxd";
  adapter.connection.interfaceName = "eth1";
  adapter.connection.expectedFirmwareName = "PROFINET";
  virtual_factory::icp::EquipmentMappingRecord device;
  device.equipmentId = "PN-DEV-001";
  device.type = "io-device";
  device.stationName = "mixer-station";
  device.ipAddress = "192.168.0.20";
  device.vendorId = 0x002a;
  device.deviceId = 0x0101;
  virtual_factory::icp::ProfinetSubmoduleRecord submodule;
  submodule.slot = 1;
  submodule.subslot = 1;
  submodule.inputLength = 8;
  submodule.outputLength = 4;
  device.submodules.push_back(submodule);
  virtual_factory::icp::TelemetryMappingRecord speed;
  speed.name = "speed";
  speed.inputByteOffset = 0;
  speed.valueType = "Int16";
  device.telemetry.push_back(speed);
  virtual_factory::icp::CommandMappingRecord start;
  start.command = "start";
  start.outputByteOffset = 0;
  start.bitOffset = 0;
  device.commands.push_back(start);
  device.state.mapped = true;
  device.state.inputByteOffset = 2;
  device.fault.mapped = true;
  device.fault.inputByteOffset = 3;
  adapter.equipment.push_back(device);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord profibusAdapter()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "pb-master";
  adapter.protocol = "profibus";
  adapter.connection.boardId = "cifx1";
  adapter.connection.channel = 0;
  adapter.connection.masterAddress = 1;
  adapter.connection.baudRateKbps = 1500;
  adapter.connection.configArtifactPath = "/opt/icp/pb/config.nxd";
  adapter.connection.expectedFirmwareName = "PROFIBUS";
  virtual_factory::icp::EquipmentMappingRecord slave;
  slave.equipmentId = "PB-SLAVE-001";
  slave.type = "dp-slave";
  slave.stationAddress = 12;
  virtual_factory::icp::ProfibusModuleRecord module;
  module.slot = 0;
  module.ident = "DI16";
  module.inputLength = 2;
  module.outputLength = 0;
  slave.modules.push_back(module);
  virtual_factory::icp::TelemetryMappingRecord status;
  status.name = "status";
  status.inputByteOffset = 0;
  status.valueType = "Uint8";
  slave.telemetry.push_back(status);
  virtual_factory::icp::CommandMappingRecord reset;
  reset.command = "reset";
  reset.outputByteOffset = 0;
  slave.commands.push_back(reset);
  slave.state.mapped = true;
  slave.fault.mapped = true;
  adapter.equipment.push_back(slave);
  return adapter;
}

virtual_factory::icp::IcpConfigurationDocument fullPlant()
{
  virtual_factory::icp::IcpConfigurationDocument document;
  document.name = "demo-plant";
  document.adapters = {
      mockAdapter(),
      opcuaAdapter(),
      modbusAdapter(),
      mqttAdapter(),
      restAdapter(),
      ethernetIpAdapter(),
      profinetAdapter(),
      profibusAdapter(),
  };
  return document;
}

void testCreateModifyRemove()
{
  virtual_factory::icp::ConfigurationCatalog catalog;
  catalog.setName("workspace");
  expect(catalog.upsertAdapter(mockAdapter()).ok, "create mock adapter");
  expect(catalog.adapterCount() == 1, "one adapter after create");
  expect(catalog.adapter("mock-a") != nullptr, "lookup mock adapter");
  expect(catalog.equipmentIds().size() == 1, "one equipment mapping");

  virtual_factory::icp::AdapterConfigRecord updated = mockAdapter();
  updated.description = "updated";
  expect(catalog.upsertAdapter(updated).ok, "modify mock adapter");
  expect(catalog.adapter("mock-a")->description == "updated", "description updated");

  expect(catalog.removeAdapter("mock-a").ok, "remove mock adapter");
  expect(catalog.adapterCount() == 0, "zero adapters after remove");
  expect(!catalog.removeAdapter("missing").ok, "remove missing adapter fails");
}

void testValidation()
{
  virtual_factory::icp::IcpConfigurationDocument document = fullPlant();
  const auto ok = virtual_factory::icp::ConfigurationValidator::validate(document);
  expect(ok.ok, "full plant validates");

  virtual_factory::icp::IcpConfigurationDocument dupAdapters = document;
  dupAdapters.adapters.push_back(mockAdapter());
  const auto dupA = virtual_factory::icp::ConfigurationValidator::validate(dupAdapters);
  expect(!dupA.ok && contains(dupA, "duplicate adapterId"), "duplicate adapter ids");

  virtual_factory::icp::IcpConfigurationDocument dupEq = document;
  virtual_factory::icp::EquipmentMappingRecord copy = mockAdapter().equipment.front();
  copy.equipmentId = "MIXER-001";
  dupEq.adapters.front().equipment.push_back(copy);
  const auto dupE = virtual_factory::icp::ConfigurationValidator::validate(dupEq);
  expect(!dupE.ok && contains(dupE, "duplicate equipmentId"), "duplicate equipment ids");

  virtual_factory::icp::AdapterConfigRecord badProtocol = mockAdapter();
  badProtocol.adapterId = "bad-proto";
  badProtocol.protocol = "siemens";
  const auto proto = virtual_factory::icp::ConfigurationValidator::validateAdapter(badProtocol);
  expect(!proto.ok && contains(proto, "invalid protocol"), "invalid protocol");

  virtual_factory::icp::AdapterConfigRecord missingHost = opcuaAdapter();
  missingHost.connection.endpointUrl.clear();
  const auto missing = virtual_factory::icp::ConfigurationValidator::validateAdapter(missingHost);
  expect(!missing.ok && contains(missing, "endpointUrl"), "missing OPC UA endpoint");

  virtual_factory::icp::AdapterConfigRecord plaintext = mqttAdapter();
  plaintext.credentials.passwordRef = "hunter2";
  const auto secret = virtual_factory::icp::ConfigurationValidator::validateAdapter(plaintext);
  expect(!secret.ok && contains(secret, "passwordRef"), "plaintext password rejected");

  virtual_factory::icp::AdapterConfigRecord mismatch = opcuaAdapter();
  mismatch.equipment.front().adapterId = "someone-else";
  const auto rel = virtual_factory::icp::ConfigurationValidator::validateAdapter(mismatch);
  expect(!rel.ok && contains(rel, "does not match adapter"), "invalid adapter/equipment relationship");

  virtual_factory::icp::AdapterConfigRecord badMap = opcuaAdapter();
  badMap.equipment.front().telemetry.front().address.clear();
  const auto map = virtual_factory::icp::ConfigurationValidator::validateAdapter(badMap);
  expect(!map.ok && contains(map, "NodeId"), "invalid OPC UA mapping");

  virtual_factory::icp::AdapterConfigRecord pb = profibusAdapter();
  pb.equipment.front().stationAddress = 200;
  const auto pbBad = virtual_factory::icp::ConfigurationValidator::validateAdapter(pb);
  expect(!pbBad.ok && contains(pbBad, "stationAddress"), "invalid PROFIBUS station address");

  virtual_factory::icp::IcpConfigurationDocument badVersion = document;
  badVersion.version = 99;
  const auto ver = virtual_factory::icp::ConfigurationValidator::validate(badVersion);
  expect(!ver.ok && contains(ver, "unsupported configuration version"), "unsupported version");
}

void testRoundTripAndRestart()
{
  const std::string dir = tempDir();
  const std::string path = dir + "/icp.json";
  virtual_factory::icp::JsonFileConfigurationRepository repo(path);
  virtual_factory::icp::ConfigurationCatalog catalog;
  catalog.setName("demo-plant");
  for (const auto &adapter : fullPlant().adapters)
  {
    const auto result = catalog.upsertAdapter(adapter);
    expect(result.ok, "upsert " + adapter.adapterId);
  }
  expect(catalog.adapterIds().size() == 8, "eight adapters configured");
  expect(catalog.equipmentIndex().size() == 8, "eight equipment mappings");
  expect(catalog.save(repo).ok, "save configuration");

  virtual_factory::icp::IcpConfigurationDocument loaded;
  const auto parse = repo.load(&loaded);
  expect(parse.ok, "load after save");
  const std::string original = virtual_factory::icp::JsonFileConfigurationRepository::toJsonText(
      catalog.document());
  const std::string roundTrip = virtual_factory::icp::JsonFileConfigurationRepository::toJsonText(loaded);
  expect(original == roundTrip, "round-trip equality");

  virtual_factory::icp::ConfigurationCatalog restarted;
  const auto loadResult = restarted.load(repo);
  expect(loadResult.ok, "load into new catalog (process restart)");
  expect(
      virtual_factory::icp::JsonFileConfigurationRepository::toJsonText(restarted.document())
          == original,
      "persistence across restart");
  expect(restarted.adapter("pn-controller") != nullptr, "PROFINET adapter persisted");
  expect(restarted.adapter("pb-master") != nullptr, "PROFIBUS adapter persisted");
  expect(
      restarted.adapter("pn-controller")->equipment.front().stationName == "mixer-station",
      "PROFINET station name persisted");
  expect(
      restarted.adapter("pn-controller")->connection.stationName == "icp-pn-controller",
      "PROFINET controller station name persisted");
  expect(
      restarted.adapter("pb-master")->equipment.front().stationAddress == 12,
      "PROFIBUS station address persisted");
  expect(
      restarted.adapter("mqtt-broker")->credentials.passwordRef == "env:MQTT_PASSWORD",
      "credential reference persisted (not a password)");
}

void testMalformedAndUnsupportedVersion()
{
  virtual_factory::icp::IcpConfigurationDocument out;
  const auto malformed = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      "{ this is not json", &out);
  expect(!malformed.ok && contains(malformed, "corrupt configuration"), "malformed JSON");

  const auto empty = virtual_factory::icp::JsonFileConfigurationRepository::parseText("", &out);
  expect(!empty.ok && contains(empty, "empty file"), "empty file is corrupt");

  const auto unknownField = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      R"({
        "schema": "virtual-factory.icp.config",
        "version": 1,
        "name": "x",
        "workOrders": []
      })",
      &out);
  expect(!unknownField.ok && contains(unknownField, "unknown field"), "unknown field rejected");

  const auto plaintext = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      R"({
        "schema": "virtual-factory.icp.config",
        "version": 1,
        "name": "x",
        "adapters": [{
          "adapterId": "mqtt-broker",
          "protocol": "mqtt",
          "connection": {"host": "127.0.0.1", "port": 1883},
          "credentials": {"password": "hunter2"}
        }]
      })",
      &out);
  expect(!plaintext.ok && contains(plaintext, "plaintext secret"), "plaintext password key rejected");

  const auto future = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      R"({
        "schema": "virtual-factory.icp.config",
        "version": 99,
        "name": "x",
        "adapters": []
      })",
      &out);
  expect(!future.ok && contains(future, "unsupported configuration version"), "parse unsupported version");

  const auto ancient = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      R"({
        "schema": "virtual-factory.icp.config",
        "version": 0,
        "name": "x",
        "adapters": []
      })",
      &out);
  expect(!ancient.ok && contains(ancient, "no migration path"), "no migration from version 0");

  const auto v1 = virtual_factory::icp::JsonFileConfigurationRepository::parseText(
      R"({
        "schema": "virtual-factory.icp.config",
        "version": 1,
        "name": "migrated",
        "adapters": []
      })",
      &out);
  expect(v1.ok && out.version == 1 && out.name == "migrated", "v1 identity migration");
}

void testAtomicSaveFailure()
{
  const std::string dir = tempDir() + "-atomic";
  ::mkdir(dir.c_str(), 0700);
  const std::string path = dir + "/icp.json";
  virtual_factory::icp::JsonFileConfigurationRepository repo(path);
  virtual_factory::icp::ConfigurationCatalog catalog;
  catalog.setName("atomic");
  expect(catalog.upsertAdapter(mockAdapter()).ok, "atomic fixture adapter");
  expect(catalog.save(repo).ok, "initial atomic save");

  const std::string original = virtual_factory::icp::JsonFileConfigurationRepository::toJsonText(
      catalog.document());

  const std::string tmpPath = path + ".tmp";
  if (::mkdir(tmpPath.c_str(), 0700) != 0)
  {
    expect(false, std::string("mkdir tmp dir: ") + std::strerror(errno));
    return;
  }

  virtual_factory::icp::AdapterConfigRecord extra = opcuaAdapter();
  expect(catalog.upsertAdapter(extra).ok, "mutate before failed save");
  const auto failed = catalog.save(repo);
  expect(!failed.ok, "save fails when temp path is a directory");

  virtual_factory::icp::IcpConfigurationDocument onDisk;
  const auto stillThere = repo.load(&onDisk);
  expect(stillThere.ok, "original file still readable after failed save");
  expect(
      virtual_factory::icp::JsonFileConfigurationRepository::toJsonText(onDisk) == original,
      "failed save did not replace original file");
}

void testAllProtocolsRepresented()
{
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("mock"), "mock");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("opcua"), "opcua");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("modbus"), "modbus");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("mqtt"), "mqtt");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("rest"), "rest");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("ethernetip"), "ethernetip");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("profinet"), "profinet");
  expect(virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("profibus"), "profibus");
  expect(
      !virtual_factory::icp::ConfigurationValidator::isSupportedProtocol("s7"),
      "vendor protocol class not accepted");
}

}  // namespace

int main()
{
  testCreateModifyRemove();
  testValidation();
  testRoundTripAndRestart();
  testMalformedAndUnsupportedVersion();
  testAtomicSaveFailure();
  testAllProtocolsRepresented();

  if (failures == 0)
  {
    std::cout << "icp_configuration_test: all checks passed" << std::endl;
    return 0;
  }
  std::cerr << "icp_configuration_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
