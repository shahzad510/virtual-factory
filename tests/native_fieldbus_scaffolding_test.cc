#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

#include <iostream>
#include <string>

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

virtual_factory::ProfinetIndustrialAdapter::AdapterConfig sampleProfinetConfig()
{
  virtual_factory::ProfinetIndustrialAdapter::AdapterConfig config;
  config.boardId = "cifx0";
  config.channel = 0;
  config.configArtifactPath = "/opt/cifx/config/profinet/config.nxd";

  virtual_factory::ProfinetEquipmentMapping io1;
  io1.id = "PN-IO-001";
  io1.type = "remote_io";
  io1.capabilities = {"start", "stop"};
  io1.device.stationName = "io-device-1";
  config.equipment.push_back(io1);

  virtual_factory::ProfinetEquipmentMapping io2;
  io2.id = "PN-IO-002";
  io2.type = "remote_io";
  io2.device.stationName = "io-device-2";
  config.equipment.push_back(io2);

  return config;
}

virtual_factory::ProfibusIndustrialAdapter::AdapterConfig sampleProfibusConfig()
{
  virtual_factory::ProfibusIndustrialAdapter::AdapterConfig config;
  config.boardId = "cifx1";
  config.channel = 0;
  config.masterAddress = 1;
  config.baudRateKbps = 19200;
  config.configArtifactPath = "/opt/cifx/config/profibus/config.nxd";

  virtual_factory::ProfibusEquipmentMapping slave1;
  slave1.id = "PB-SLV-001";
  slave1.type = "remote_io";
  slave1.slave.stationAddress = 3;
  config.equipment.push_back(slave1);

  virtual_factory::ProfibusEquipmentMapping slave2;
  slave2.id = "PB-SLV-002";
  slave2.type = "drive";
  slave2.slave.stationAddress = 5;
  config.equipment.push_back(slave2);

  return config;
}

}  // namespace

int main()
{
  using virtual_factory::ConnectionState;
  using virtual_factory::ProfinetIndustrialAdapter;
  using virtual_factory::ProfibusIndustrialAdapter;
  using virtual_factory::icp::AdapterManager;

  ProfinetIndustrialAdapter pn("pn-native", sampleProfinetConfig());
  expect(pn.protocol() == "profinet", "profinet protocol id");
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet starts disconnected");

  expect(!pn.connect(), "profinet connect blocked without plant hardware");
  expect(
      pn.connectionState() == ConnectionState::Faulted,
      "profinet connect failure becomes faulted");
  expect(!pn.lastError().empty(), "profinet lastError set");
  if (!pn.hilscherSdkPresent())
  {
    expect(
        pn.lastError().find("BLOCKED BY SDK/HARDWARE") != std::string::npos
            || pn.lastError().find("cannot read") != std::string::npos,
        "Hilscher SDK absent in default CI build");
  }

  pn.disconnect();
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet disconnect clears fault state");

  expect(pn.equipment().size() == 2U, "profinet exposes two equipment");
  expect(pn.equipmentById("PN-IO-001") != nullptr, "profinet equipment lookup");
  expect(
      pn.equipmentById("missing") == nullptr,
      "profinet missing equipment lookup");

  ProfibusIndustrialAdapter pb("pb-native", sampleProfibusConfig());
  expect(pb.protocol() == "profibus", "profibus protocol id");
  expect(!pb.connect(), "profibus connect blocked without plant hardware");
  expect(
      pb.connectionState() == ConnectionState::Faulted,
      "profibus connect failure becomes faulted");
  pb.disconnect();
  expect(pb.equipment().size() == 2U, "profibus exposes two equipment");

  AdapterManager manager;
  expect(
      manager.addAdapter(std::make_unique<ProfinetIndustrialAdapter>(
          "pn-mgr", sampleProfinetConfig())).ok,
      "manager add profinet");
  expect(
      manager.addAdapter(std::make_unique<ProfibusIndustrialAdapter>(
          "pb-mgr", sampleProfibusConfig())).ok,
      "manager add profibus");

  expect(manager.adapterCount() == 2U, "manager holds pn and pb adapters");
  expect(manager.adapter("pn-mgr") != nullptr, "manager profinet lookup");
  expect(manager.adapter("pb-mgr") != nullptr, "manager profibus lookup");

  ProfinetIndustrialAdapter pnMissing("pn-bad", sampleProfinetConfig());
  pnMissing.disconnect();
  auto badPn = sampleProfinetConfig();
  badPn.configArtifactPath.clear();
  ProfinetIndustrialAdapter pnNoConfig("pn-nopath", badPn);
  expect(!pnNoConfig.connect(), "profinet rejects empty config path");
  expect(
      pnNoConfig.connectionState() == ConnectionState::Faulted,
      "profinet config error faulted");

  if (failures != 0)
  {
    std::cerr << failures << " test failure(s)" << std::endl;
    return 1;
  }

  std::cout << "native_fieldbus_scaffolding_test: OK" << std::endl;
  return 0;
}
