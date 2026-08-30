// SOFTWARE-INTEGRATION TEST
// Native Hilscher PROFINET/PROFIBUS adapters, ICP factory/manager/cache,
// configuration, lifecycle, and honest failure without hardware.
//
// This is NOT a REAL PROFINET TEST, REAL PROFIBUS TEST, or HARDWARE VALIDATION.

#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/icp/LiveStateCache.hh>
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
