// SOFTWARE-INTEGRATION TEST
// Native Hilscher PROFINET/PROFIBUS adapters, ICP factory/manager/cache,
// configuration, lifecycle, and honest failure without hardware.
//
// This is NOT a REAL PROFINET TEST, REAL PROFIBUS TEST, or HARDWARE VALIDATION.

#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
#include <virtual_factory/icp/PollScheduler.hh>
#include <virtual_factory/icp/config/ConfigurationCatalog.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>
#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>
#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

#include "hilscher/cifx_runtime.hh"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

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

bool contains(const std::string &haystack, const char *needle)
{
  return haystack.find(needle) != std::string::npos;
}

virtual_factory::ProfinetIndustrialAdapter::AdapterConfig sampleProfinetConfig()
{
  virtual_factory::ProfinetIndustrialAdapter::AdapterConfig config;
  config.boardId = "cifx0";
  config.channel = 0;
  config.stationName = "icp-pn-controller";
  config.configArtifactPath = "/opt/cifx/config/profinet/config.nxd";

  virtual_factory::ProfinetEquipmentMapping io1;
  io1.id = "PN-IO-001";
  io1.type = "remote_io";
  io1.capabilities = {"start", "stop"};
  io1.device.stationName = "io-device-1";
  io1.device.vendorId = 0x002A;
  io1.device.deviceId = 0x0101;
  io1.device.subslots.push_back({0, 1, "dap"});
  io1.device.subslots.push_back({1, 1, "do8"});
  io1.commands.push_back({"start", virtual_factory::ProfinetValueType::Bool, 0, 0});
  io1.telemetry.push_back(
      {"speed", virtual_factory::ProfinetValueType::Int16, 0, 0, 0, "rpm"});
  io1.state = {virtual_factory::ProfinetValueType::Bool, 2, 0, true};
  io1.fault = {virtual_factory::ProfinetValueType::Bool, 2, 1, true};
  config.equipment.push_back(io1);

  virtual_factory::ProfinetEquipmentMapping io2;
  io2.id = "PN-IO-002";
  io2.type = "remote_io";
  io2.device.stationName = "io-device-2";
  io2.device.subslots.push_back({1, 1, "di8"});
  config.equipment.push_back(io2);

  return config;
}

virtual_factory::ProfibusIndustrialAdapter::AdapterConfig sampleProfibusConfig()
{
  virtual_factory::ProfibusIndustrialAdapter::AdapterConfig config;
  config.boardId = "cifx1";
  config.channel = 0;
  config.masterAddress = 1;
  config.baudRateKbps = 1500;
  config.configArtifactPath = "/opt/cifx/config/profibus/config.nxd";

  virtual_factory::ProfibusEquipmentMapping slave1;
  slave1.id = "PB-SLV-001";
  slave1.type = "remote_io";
  slave1.slave.stationAddress = 3;
  slave1.slave.vendorId = 0x002A;
  slave1.slave.deviceId = 0x0001;
  slave1.slave.modules.push_back({0, "di8"});
  slave1.slave.modules.push_back({1, "do8"});
  slave1.commands.push_back({"start", virtual_factory::ProfibusValueType::Bool, 0, 0});
  slave1.telemetry.push_back(
      {"current", virtual_factory::ProfibusValueType::Int16, 0, 0, 0, "A"});
  slave1.state = {virtual_factory::ProfibusValueType::Bool, 2, 0, true};
  slave1.fault = {virtual_factory::ProfibusValueType::Bool, 2, 1, true};
  config.equipment.push_back(slave1);

  virtual_factory::ProfibusEquipmentMapping slave2;
  slave2.id = "PB-SLV-002";
  slave2.type = "drive";
  slave2.slave.stationAddress = 5;
  slave2.slave.modules.push_back({0, "drive"});
  config.equipment.push_back(slave2);

  return config;
}

std::string writeTempArtifact()
{
  const std::string path =
      "/tmp/vf-hilscher-config-" + std::to_string(getpid()) + ".nxd";
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
  {
    return {};
  }
  file << "SYCON-PLACEHOLDER";
  return path;
}

virtual_factory::icp::AdapterConfigRecord catalogProfinet()
{
  virtual_factory::icp::AdapterConfigRecord adapter;
  adapter.adapterId = "pn-controller";
  adapter.protocol = "profinet";
  adapter.connection.boardId = "cifx0";
  adapter.connection.channel = 0;
  adapter.connection.stationName = "icp-pn-controller";
  adapter.connection.configArtifactPath = "/opt/icp/pn/config.nxd";
  adapter.connection.expectedFirmwareName = "PROFINET";

  virtual_factory::icp::EquipmentMappingRecord io1;
  io1.equipmentId = "PN-IO-001";
  io1.type = "remote_io";
  io1.stationName = "io-device-1";
  io1.ipAddress = "192.168.0.20";
  io1.vendorId = 0x002A;
  io1.deviceId = 0x0101;
  io1.submodules.push_back({0, 1, 2, 1});
  io1.submodules.push_back({1, 1, 8, 4});
  virtual_factory::icp::TelemetryMappingRecord speed;
  speed.name = "speed";
  speed.unit = "rpm";
  speed.inputByteOffset = 0;
  speed.valueType = "Int16";
  io1.telemetry.push_back(speed);
  virtual_factory::icp::CommandMappingRecord start;
  start.command = "start";
  start.outputByteOffset = 0;
  io1.commands.push_back(start);
  io1.state.mapped = true;
  io1.state.inputByteOffset = 2;
  io1.fault.mapped = true;
  io1.fault.inputByteOffset = 2;
  io1.fault.bitOffset = 1;
  adapter.equipment.push_back(io1);

  virtual_factory::icp::EquipmentMappingRecord io2;
  io2.equipmentId = "PN-IO-002";
  io2.type = "remote_io";
  io2.stationName = "io-device-2";
  io2.submodules.push_back({1, 1, 8, 0});
  adapter.equipment.push_back(io2);
  return adapter;
}

virtual_factory::icp::AdapterConfigRecord catalogProfibus()
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

  virtual_factory::icp::EquipmentMappingRecord slave1;
  slave1.equipmentId = "PB-SLV-001";
  slave1.type = "remote_io";
  slave1.stationAddress = 3;
  slave1.vendorId = 0x002A;
  slave1.deviceId = 0x0001;
  slave1.modules.push_back({0, "di8", 2, 0});
  slave1.modules.push_back({1, "do8", 0, 1});
  virtual_factory::icp::TelemetryMappingRecord current;
  current.name = "current";
  current.unit = "A";
  current.inputByteOffset = 0;
  current.valueType = "Int16";
  slave1.telemetry.push_back(current);
  adapter.equipment.push_back(slave1);

  virtual_factory::icp::EquipmentMappingRecord slave2;
  slave2.equipmentId = "PB-SLV-002";
  slave2.type = "drive";
  slave2.stationAddress = 5;
  slave2.modules.push_back({0, "drive", 4, 4});
  adapter.equipment.push_back(slave2);
  return adapter;
}

std::string writeTempCatalogPath()
{
  return "/tmp/vf-icp-native-catalog-" + std::to_string(getpid()) + ".json";
}

}  // namespace

int main()
{
  using virtual_factory::ConnectionState;
  using virtual_factory::ProfinetIndustrialAdapter;
  using virtual_factory::ProfibusIndustrialAdapter;
  using virtual_factory::icp::AdapterFactory;
  using virtual_factory::icp::AdapterManager;
  using virtual_factory::icp::LiveStateCache;
  using virtual_factory::internal::CifxDriver;
  using virtual_factory::internal::CifxInventory;

  ProfinetIndustrialAdapter pn("pn-native", sampleProfinetConfig());
  expect(pn.protocol() == "profinet", "profinet protocol id");
  expect(pn.config().stationName == "icp-pn-controller", "controller station name");
  expect(pn.config().equipment.size() == 2U, "profinet device count");
  expect(
      pn.config().equipment[0].device.subslots.size() == 2U,
      "profinet slots/subslots retained");
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet starts disconnected");

  expect(!pn.connect(), "profinet connect without hardware/SDK fails honestly");
  expect(
      pn.connectionState() == ConnectionState::Faulted,
      "profinet connect failure becomes faulted");
  expect(!pn.lastError().empty(), "profinet lastError set");
  if (pn.hilscherSdkPresent())
  {
    expect(
        contains(pn.lastError(), "HARDWARE")
            || contains(pn.lastError(), "cifX")
            || contains(pn.lastError(), "board")
            || contains(pn.lastError(), "artifact"),
        "profinet SDK-present error names hardware/driver boundary");
  }
  else
  {
    expect(
        contains(pn.lastError(), "BLOCKED BY SDK/HARDWARE")
            || contains(pn.lastError(), "artifact"),
        "profinet SDK-absent blocked message");
  }

  pn.poll();
  expect(
      pn.connectionState() == ConnectionState::Faulted,
      "poll while faulted is a no-op");

  auto *eq = pn.equipmentById("PN-IO-001");
  expect(eq != nullptr, "configured equipment exists while faulted");
  const auto start = eq->execute("start", 1.0);
  expect(!start.accepted, "execute while faulted fails");
  expect(start.message == "communication fault", "execute reports communication fault");

  // Explicit reconnect (no auto-reconnect): retry connect after Faulted.
  expect(!pn.connect(), "explicit reconnect without hardware still fails");
  expect(pn.connectionState() == ConnectionState::Faulted, "reconnect stays faulted");

  pn.disconnect();
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet disconnect clears fault state");

  auto missingPath = sampleProfinetConfig();
  missingPath.configArtifactPath.clear();
  ProfinetIndustrialAdapter pnNoConfig("pn-nopath", missingPath);
  expect(!pnNoConfig.connect(), "profinet rejects empty config path");
  expect(
      contains(pnNoConfig.lastError(), "missing PROFINET configuration artifact"),
      "empty path error");

  auto missingFile = sampleProfinetConfig();
  missingFile.configArtifactPath = "/tmp/vf-no-such-profinet-config.nxd";
  ProfinetIndustrialAdapter pnMissingFile("pn-missing-file", missingFile);
  expect(!pnMissingFile.connect(), "profinet missing artifact file");
  expect(
      contains(pnMissingFile.lastError(), "cannot read PROFINET configuration artifact"),
      "missing artifact message");

  const std::string artifact = writeTempArtifact();
  expect(!artifact.empty(), "temp SYCON placeholder created");
  auto withFile = sampleProfinetConfig();
  withFile.configArtifactPath = artifact;
  ProfinetIndustrialAdapter pnFile("pn-file", withFile);
  expect(!pnFile.connect(), "profinet with placeholder artifact still blocked");
  expect(pnFile.connectionState() == ConnectionState::Faulted, "placeholder still faulted");
  if (pnFile.hilscherSdkPresent())
  {
    expect(
        contains(pnFile.lastError(), "HARDWARE")
            || contains(pnFile.lastError(), "cifX")
            || contains(pnFile.lastError(), "board")
            || contains(pnFile.lastError(), "Driver"),
        "SDK-present placeholder hits driver/hardware boundary");
  }

  ProfibusIndustrialAdapter pb("pb-native", sampleProfibusConfig());
  expect(pb.protocol() == "profibus", "profibus protocol id");
  expect(pb.config().baudRateKbps == 1500U, "profibus baud retained");
  expect(pb.config().masterAddress == 1U, "profibus master address");
  expect(
      pb.config().equipment[0].slave.modules.size() == 2U,
      "profibus modules retained");
  expect(!pb.connect(), "profibus connect blocked without hardware");
  expect(pb.connectionState() == ConnectionState::Faulted, "profibus faulted");
  pb.disconnect();
  expect(pb.equipment().size() == 2U, "profibus exposes two slaves");

  auto pbMissing = sampleProfibusConfig();
  pbMissing.configArtifactPath = "/tmp/vf-no-such-profibus-config.nxd";
  ProfibusIndustrialAdapter pbMissingFile("pb-missing-file", pbMissing);
  expect(!pbMissingFile.connect(), "profibus missing artifact");
  expect(
      contains(pbMissingFile.lastError(), "cannot read PROFIBUS configuration artifact"),
      "profibus missing artifact message");

  auto factoryPn = AdapterFactory::createProfinet("pn-factory", sampleProfinetConfig());
  auto factoryPb = AdapterFactory::createProfibus("pb-factory", sampleProfibusConfig());
  expect(factoryPn != nullptr && factoryPn->protocol() == "profinet", "factory profinet");
  expect(factoryPb != nullptr && factoryPb->protocol() == "profibus", "factory profibus");

  AdapterManager manager;
  expect(
      manager.addAdapter(AdapterFactory::createProfinet(
          "pn-mgr", sampleProfinetConfig())).ok,
      "manager add profinet");
  expect(
      manager.addAdapter(AdapterFactory::createProfibus(
          "pb-mgr", sampleProfibusConfig())).ok,
      "manager add profibus");
  expect(manager.adapterCount() == 2U, "manager holds both adapters");
  expect(!manager.connectAdapter("pn-mgr").ok, "manager connect pn fails honestly");
  expect(!manager.connectAdapter("pb-mgr").ok, "manager connect pb fails honestly");

  LiveStateCache cache;
  auto *pnAdapter = manager.adapter("pn-mgr");
  expect(pnAdapter != nullptr, "manager pn lookup");
  cache.markAdapterCommunication(
      pnAdapter->id(), pnAdapter->connectionState(), pnAdapter->lastError());
  cache.updateFromAdapter(*pnAdapter);
  expect(cache.size() >= 2U, "cache records configured equipment");
  const auto snapshot = cache.equipmentById("PN-IO-001");
  expect(snapshot.has_value(), "cache has PN-IO-001");
  expect(snapshot->stale, "cache marks communication stale without bus");

  using virtual_factory::icp::ConfigurationCatalog;
  using virtual_factory::icp::ConfigResult;
  using virtual_factory::icp::JsonFileConfigurationRepository;
  using virtual_factory::icp::NativeFieldbusConfigMapper;
  using virtual_factory::icp::PollScheduler;

  const std::string catalogPath = writeTempCatalogPath();
  JsonFileConfigurationRepository repository(catalogPath);
  ConfigurationCatalog catalog;
  catalog.setName("native-fieldbus-software-integration");
  expect(catalog.upsertAdapter(catalogProfinet()).ok, "catalog add profinet");
  expect(catalog.upsertAdapter(catalogProfibus()).ok, "catalog add profibus");
  expect(catalog.save(repository).ok, "catalog save without hardware");

  ConfigurationCatalog restarted;
  expect(restarted.load(repository).ok, "catalog load without hardware");
  expect(restarted.adapter("pn-controller") != nullptr, "loaded profinet record");
  expect(restarted.adapter("pb-master") != nullptr, "loaded profibus record");
  expect(
      restarted.adapter("pn-controller")->equipment.size() == 2U,
      "multi IO-Device configuration persisted");
  expect(
      restarted.adapter("pb-master")->equipment.size() == 2U,
      "multi DP-slave configuration persisted");

  ProfinetIndustrialAdapter::AdapterConfig mappedPn;
  ConfigResult mapPn =
      NativeFieldbusConfigMapper::toProfinet(*restarted.adapter("pn-controller"), &mappedPn);
  expect(mapPn.ok, "map catalog to PROFINET AdapterConfig");
  expect(mappedPn.stationName == "icp-pn-controller", "mapped controller station name");
  expect(mappedPn.expectedFirmwareName == "PROFINET", "mapped expected firmware");
  expect(mappedPn.equipment.size() == 2U, "mapped two IO-Devices");
  expect(
      mappedPn.equipment[0].device.subslots.size() == 2U,
      "mapped slots/subslots from catalog");
  expect(
      mappedPn.equipment[0].device.ipAddress == "192.168.0.20",
      "mapped intended IO-Device IP (DCP remains firmware)");

  ProfibusIndustrialAdapter::AdapterConfig mappedPb;
  ConfigResult mapPb =
      NativeFieldbusConfigMapper::toProfibus(*restarted.adapter("pb-master"), &mappedPb);
  expect(mapPb.ok, "map catalog to PROFIBUS AdapterConfig");
  expect(mappedPb.baudRateKbps == 1500U, "mapped baud rate");
  expect(mappedPb.equipment.size() == 2U, "mapped two DP slaves");
  expect(
      mappedPb.equipment[0].slave.modules.front().moduleType == "di8",
      "mapped module ident as metadata (no GSD parser)");

  ConfigResult fromRecord;
  auto catalogPn = AdapterFactory::createProfinetFromRecord(
      *restarted.adapter("pn-controller"), &fromRecord);
  expect(fromRecord.ok && catalogPn != nullptr, "factory from PROFINET catalog record");
  expect(!catalogPn->connect(), "catalog-built PROFINET still blocked without hardware");
  expect(
      catalogPn->connectionState() == ConnectionState::Faulted,
      "catalog-built PROFINET faults honestly");

  auto catalogPb = AdapterFactory::createProfibusFromRecord(
      *restarted.adapter("pb-master"));
  expect(catalogPb != nullptr, "factory from PROFIBUS catalog record");
  expect(!catalogPb->connect(), "catalog-built PROFIBUS still blocked without hardware");

  virtual_factory::icp::AdapterConfigRecord wrong = catalogProfinet();
  wrong.protocol = "opcua";
  ConfigResult wrongResult;
  expect(
      AdapterFactory::createProfinetFromRecord(wrong, &wrongResult) == nullptr
          && !wrongResult.ok,
      "factory rejects non-PROFINET record");

  AdapterManager scheduled;
  expect(
      scheduled.addAdapter(AdapterFactory::createProfinet(
          "pn-sched", sampleProfinetConfig())).ok,
      "scheduler manager add profinet");
  expect(
      scheduled.addAdapter(AdapterFactory::createProfibus(
          "pb-sched", sampleProfibusConfig())).ok,
      "scheduler manager add profibus");
  LiveStateCache scheduledCache;
  PollScheduler scheduler(scheduled, scheduledCache);
  scheduler.pollOnce();
  expect(
      scheduledCache.size() == 0U,
      "PollScheduler skips disconnected native adapters");

  expect(!scheduled.connectAdapter("pn-sched").ok, "scheduler connect pn fails honestly");
  expect(!scheduled.connectAdapter("pb-sched").ok, "scheduler connect pb fails honestly");
  scheduler.pollOnce();
  const auto faulted = scheduledCache.equipmentById("PN-IO-001");
  expect(faulted.has_value(), "PollScheduler caches faulted native equipment");
  expect(faulted->stale, "PollScheduler marks native comms stale");
  expect(
      faulted->communicationState == ConnectionState::Faulted,
      "PollScheduler records Faulted communication");
  expect(!faulted->machineFault, "PollScheduler does not invent machineFault");
  expect(
      scheduled.adapter("pn-sched")->connectionState() == ConnectionState::Faulted,
      "native poll while Faulted is a no-op");

  std::remove(catalogPath.c_str());

  CifxInventory inventory;
  std::string enumError;
  const bool enumerated = CifxDriver::enumerate(&inventory, &enumError);
  if (pn.hilscherSdkPresent())
  {
    if (enumerated)
    {
      expect(inventory.boards.empty(), "no cifX hardware in this environment");
    }
    else
    {
      expect(!enumError.empty(), "enumerate error when driver cannot start");
    }
  }
  else
  {
    expect(!enumerated, "enumerate refused without compiled SDK");
    expect(
        contains(enumError, "BLOCKED BY SDK/HARDWARE"),
        "enumerate blocked message");
  }

  if (!artifact.empty())
  {
    std::remove(artifact.c_str());
  }

  if (failures != 0)
  {
    std::cerr << failures << " SOFTWARE-INTEGRATION test failure(s)" << std::endl;
    return 1;
  }

  std::cout << "native_fieldbus_software_integration_test: OK (SOFTWARE-INTEGRATION)"
            << std::endl;
  return 0;
}
