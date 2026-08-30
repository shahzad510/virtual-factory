#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/AdapterManager.hh>
#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>
#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

#include "hilscher/process_image_codec.hh"

#include <iostream>
#include <string>
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

virtual_factory::ProfinetIndustrialAdapter::AdapterConfig sampleProfinetConfig()
{
  virtual_factory::ProfinetIndustrialAdapter::AdapterConfig config;
  config.boardId = "cifx0";
  config.channel = 0;
  config.interfaceName = "PN-IF";
  config.configArtifactPath = "/opt/cifx/config/profinet/config.nxd";

  virtual_factory::ProfinetEquipmentMapping io1;
  io1.id = "PN-IO-001";
  io1.type = "remote_io";
  io1.capabilities = {"start", "stop"};
  io1.device.stationName = "io-device-1";
  io1.device.submodules.push_back({0, 1, 2, 2});
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
  slave1.slave.modules.push_back({0, "mod-a", 2, 2});
  config.equipment.push_back(slave1);

  virtual_factory::ProfibusEquipmentMapping slave2;
  slave2.id = "PB-SLV-002";
  slave2.type = "drive";
  slave2.slave.stationAddress = 5;
  config.equipment.push_back(slave2);

  return config;
}

void testProcessImageCodec()
{
  using virtual_factory::internal::ProcessValueType;
  using virtual_factory::internal::processImageRead;
  using virtual_factory::internal::processImageWrite;

  std::vector<std::uint8_t> image(8, 0);
  expect(processImageWrite(&image, ProcessValueType::Int16, 0, 0, -3.0),
         "codec write int16");
  double value = 0.0;
  expect(processImageRead(image, ProcessValueType::Int16, 0, 0, &value),
         "codec read int16");
  expect(value == -3.0, "codec int16 roundtrip");

  expect(processImageWrite(&image, ProcessValueType::Bool, 2, 3, 1.0),
         "codec write bit");
  expect(processImageRead(image, ProcessValueType::Bool, 2, 3, &value),
         "codec read bit");
  expect(value == 1.0, "codec bit set");
  expect(!processImageRead(image, ProcessValueType::Int32, 6, 0, &value),
         "codec range check");
}

}  // namespace

int main()
{
  using virtual_factory::ConnectionState;
  using virtual_factory::ProfinetIndustrialAdapter;
  using virtual_factory::ProfibusIndustrialAdapter;
  using virtual_factory::icp::AdapterFactory;
  using virtual_factory::icp::AdapterManager;

  testProcessImageCodec();

  ProfinetIndustrialAdapter pn("pn-native", sampleProfinetConfig());
  expect(pn.protocol() == "profinet", "profinet protocol id");
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet starts disconnected");

  expect(!pn.connect(), "profinet connect fails without hardware");
  expect(
      pn.connectionState() == ConnectionState::Faulted,
      "profinet connect failure becomes faulted");
  expect(!pn.lastError().empty(), "profinet lastError set");
  if (pn.equipmentById("PN-IO-001") != nullptr)
  {
    expect(
        !pn.equipmentById("PN-IO-001")->fault(),
        "comms fault is not Equipment::fault");
  }

  if (!pn.hilscherSdkPresent())
  {
    expect(
        pn.lastError().find("BLOCKED BY SDK/HARDWARE") != std::string::npos,
        "stub backend reports SDK blocked");
  }
  else
  {
    expect(
        pn.lastError().find("HARDWARE VALIDATION PENDING") != std::string::npos
            || pn.lastError().find("cifX") != std::string::npos
            || pn.lastError().find("board") != std::string::npos
            || pn.lastError().find("Channel") != std::string::npos,
        "real cifX backend reports hardware/channel failure");
  }

  pn.disconnect();
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet disconnect clears fault state");

  expect(!pn.connect(), "explicit reconnect still fails without hardware");
  expect(pn.connectionState() == ConnectionState::Faulted, "reconnect faulted");
  pn.disconnect();

  expect(pn.equipment().size() == 2U, "profinet exposes two equipment");
  expect(pn.equipmentById("PN-IO-001") != nullptr, "profinet equipment lookup");
  expect(
      pn.equipmentById("missing") == nullptr,
      "profinet missing equipment lookup");

  ProfibusIndustrialAdapter pb("pb-native", sampleProfibusConfig());
  expect(pb.protocol() == "profibus", "profibus protocol id");
  expect(pb.hilscherSdkPresent() == pn.hilscherSdkPresent(), "sdk flag consistent");
  expect(!pb.connect(), "profibus connect fails without hardware");
  expect(
      pb.connectionState() == ConnectionState::Faulted,
      "profibus connect failure becomes faulted");
  expect(
      pn.connectionState() == ConnectionState::Disconnected,
      "profinet disconnect not affected by profibus fault");
  pb.disconnect();
  expect(pb.equipment().size() == 2U, "profibus exposes two equipment");

  auto factoryPn = AdapterFactory::createProfinet(
      "pn-factory", sampleProfinetConfig());
  expect(factoryPn != nullptr, "factory createProfinet");
  expect(factoryPn->protocol() == "profinet", "factory profinet protocol");
  auto factoryPb = AdapterFactory::createProfibus(
      "pb-factory", sampleProfibusConfig());
  expect(factoryPb != nullptr, "factory createProfibus");

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

  expect(!manager.connectAdapter("pn-mgr").ok, "manager pn connect fails");
  expect(
      manager.adapter("pn-mgr")->connectionState() == ConnectionState::Faulted,
      "manager pn faulted");
  expect(
      manager.adapter("pb-mgr")->connectionState() == ConnectionState::Disconnected,
      "pb remains disconnected when pn faults");

  expect(!manager.connectAdapter("pb-mgr").ok, "manager pb connect fails");
  expect(
      manager.adapter("pb-mgr")->connectionState() == ConnectionState::Faulted,
      "manager pb faulted independently");
  expect(
      manager.adapter("pn-mgr")->connectionState() == ConnectionState::Faulted,
      "pn remains independently faulted");

  manager.disconnectAdapter("pn-mgr");
  expect(
      manager.adapter("pn-mgr")->connectionState()
          == ConnectionState::Disconnected,
      "explicit disconnect after fault");
  expect(
      manager.adapter("pb-mgr")->connectionState() == ConnectionState::Faulted,
      "pb not cleared by pn disconnect");

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

  std::cout << "native_fieldbus_scaffolding_test: OK";
  if (pn.hilscherSdkPresent())
  {
    std::cout << " (cifX software backend compiled in; no hardware)";
  }
  else
  {
    std::cout << " (cifX stub backend)";
  }
  std::cout << std::endl;
  return 0;
}
