#include "eip_test_server.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/EtherNetIpIndustrialAdapter.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// DEVELOPMENT / INTEGRATION VALIDATION ONLY — libplctag ab_server; not AB/Rockwell cert.

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

const virtual_factory::TelemetryPoint *findTelemetry(
    const std::vector<virtual_factory::TelemetryPoint> &points,
    const std::string &name)
{
  for (const auto &point : points)
  {
    if (point.name == name)
    {
      return &point;
    }
  }
  return nullptr;
}

bool near(double actual, double expected)
{
  return std::fabs(actual - expected) < 1e-3;
}

bool telemetryNear(
    const virtual_factory::Equipment &equipment,
    const std::string &name,
    double expected,
    const char *unit = nullptr)
{
  const auto points = equipment.telemetry();
  const auto *point = findTelemetry(points, name);
  if (point == nullptr || !near(point->value, expected))
  {
    return false;
  }
  return unit == nullptr || point->unit == unit;
}

using virtual_factory::EtherNetIpValueType;

virtual_factory::EtherNetIpEquipmentMapping plc001Mapping()
{
  virtual_factory::EtherNetIpEquipmentMapping m;
  m.id = "PLC-001";
  m.type = "line_controller";
  m.capabilities = {"start", "stop", "set_speed"};
  m.commands = {
      {"start", "Plc001_CmdStart", EtherNetIpValueType::Bool},
      {"stop", "Plc001_CmdStop", EtherNetIpValueType::Bool},
      {"set_speed", "Plc001_CmdSpeed", EtherNetIpValueType::Dint},
  };
  m.telemetry = {
      {"speed", "Plc001_Speed", EtherNetIpValueType::Dint, "rpm"},
      {"temperature", "Plc001_Temp", EtherNetIpValueType::Real, "degC"},
  };
  m.state = virtual_factory::makeEtherNetIpSignal("Plc001_Running");
  m.fault = virtual_factory::makeEtherNetIpSignal("Plc001_Fault");
  return m;
}

virtual_factory::EtherNetIpEquipmentMapping plc002Mapping()
{
  virtual_factory::EtherNetIpEquipmentMapping m;
  m.id = "PLC-002";
  m.type = "packaging_cell";
  m.capabilities = {"start", "stop"};
  m.commands = {
      {"start", "Plc002_CmdStart", EtherNetIpValueType::Bool},
      {"stop", "Plc002_CmdStop", EtherNetIpValueType::Bool},
  };
  m.telemetry = {
      {"count", "Plc002_Count", EtherNetIpValueType::Dint, "ea"},
  };
  m.state = virtual_factory::makeEtherNetIpSignal("Plc002_Running");
  m.fault = virtual_factory::makeEtherNetIpSignal("Plc002_Fault");
  return m;
}

virtual_factory::EtherNetIpEquipmentMapping plc003Mapping()
{
  virtual_factory::EtherNetIpEquipmentMapping m;
  m.id = "PLC-003";
  m.type = "special_processing_machine";
  m.capabilities = {"start", "stop"};
  m.commands = {
      {"start", "Plc003_CmdStart", EtherNetIpValueType::Bool},
      {"stop", "Plc003_CmdStop", EtherNetIpValueType::Bool},
  };
  m.telemetry = {
      {"pressure", "Plc003_Pressure", EtherNetIpValueType::Real, "bar"},
  };
  m.state = virtual_factory::makeEtherNetIpSignal("Plc003_Running");
  m.fault = virtual_factory::makeEtherNetIpSignal("Plc003_Fault");
  return m;
}

std::vector<std::string> deviceATags()
{
  return {
      "Plc001_Speed:DINT[1]", "Plc001_Temp:REAL[1]", "Plc001_Running:BOOL[1]",
      "Plc001_Fault:BOOL[1]", "Plc001_CmdStart:BOOL[1]", "Plc001_CmdStop:BOOL[1]",
      "Plc001_CmdSpeed:DINT[1]", "Plc002_Count:DINT[1]", "Plc002_Running:BOOL[1]",
      "Plc002_Fault:BOOL[1]", "Plc002_CmdStart:BOOL[1]", "Plc002_CmdStop:BOOL[1]",
      "Plc003_Pressure:REAL[1]", "Plc003_Running:BOOL[1]", "Plc003_Fault:BOOL[1]",
      "Plc003_CmdStart:BOOL[1]", "Plc003_CmdStop:BOOL[1]",
  };
}

std::vector<std::string> deviceBTags()
{
  return {
      "Plc001_Speed:DINT[1]", "Plc001_Temp:REAL[1]", "Plc001_Running:BOOL[1]",
      "Plc001_Fault:BOOL[1]", "Plc001_CmdStart:BOOL[1]", "Plc001_CmdStop:BOOL[1]",
      "Plc001_CmdSpeed:DINT[1]",
  };
}

virtual_factory::EtherNetIpAdapterConfig fullDeviceConfig(
    const std::string &host, std::uint16_t port, const std::string &path)
{
  virtual_factory::EtherNetIpAdapterConfig config;
  config.host = host;
  config.port = port;
  config.path = path;
  config.plcType = "ControlLogix";
  config.timeoutMs = 2000;
  config.equipment.push_back(plc001Mapping());
  config.equipment.push_back(plc002Mapping());
  config.equipment.push_back(plc003Mapping());
  return config;
}

virtual_factory::EtherNetIpAdapterConfig oneMachineConfig(
    const std::string &host,
    std::uint16_t port,
    const std::string &path,
    virtual_factory::EtherNetIpEquipmentMapping machine)
{
  virtual_factory::EtherNetIpAdapterConfig config;
  config.host = host;
  config.port = port;
  config.path = path;
  config.plcType = "ControlLogix";
  config.timeoutMs = 2000;
  config.equipment.push_back(std::move(machine));
  return config;
}

long readRssKb()
{
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line))
  {
    if (line.rfind("VmRSS:", 0) == 0)
    {
      return std::stol(line.substr(6));
    }
  }
  return -1;
}

int countOpenFds()
{
  int count = 0;
  for (int fd = 0; fd < 1024; ++fd)
  {
    if (::fcntl(fd, F_GETFD) != -1)
    {
      ++count;
    }
  }
  return count;
}

void seedDevice(
    const std::string &host,
    std::uint16_t port,
    const std::string &path)
{
  virtual_factory::EtherNetIpEquipmentMapping seed = plc001Mapping();
  seed.commands = {
      {"set_speed", "Plc001_Speed", EtherNetIpValueType::Dint},
      {"set_temp", "Plc001_Temp", EtherNetIpValueType::Real},
      {"set_run", "Plc001_Running", EtherNetIpValueType::Bool},
      {"set_fault", "Plc001_Fault", EtherNetIpValueType::Bool},
      {"set_count", "Plc002_Count", EtherNetIpValueType::Dint},
      {"set_p2_run", "Plc002_Running", EtherNetIpValueType::Bool},
      {"set_p2_fault", "Plc002_Fault", EtherNetIpValueType::Bool},
      {"set_pressure", "Plc003_Pressure", EtherNetIpValueType::Real},
      {"set_p3_run", "Plc003_Running", EtherNetIpValueType::Bool},
      {"set_p3_fault", "Plc003_Fault", EtherNetIpValueType::Bool},
  };
  seed.telemetry.clear();
  seed.state = {};
  seed.fault = {};
  virtual_factory::EtherNetIpIndustrialAdapter seeder(
      "eip-seed",
      oneMachineConfig(host, port, path, seed));
  expect(seeder.connect(), "seeder connects");
  auto *s = seeder.equipmentById("PLC-001");
  if (s)
  {
    expect(s->execute("set_speed", 1200.0).accepted, "seed speed");
    expect(s->execute("set_temp", 36.5).accepted, "seed temp");
    expect(s->execute("set_run", 1.0).accepted, "seed running");
    expect(s->execute("set_fault", 0.0).accepted, "seed fault clear");
    expect(s->execute("set_count", 42.0).accepted, "seed count");
    expect(s->execute("set_p2_run", 0.0).accepted, "seed plc002 stopped");
    expect(s->execute("set_p2_fault", 1.0).accepted, "seed plc002 fault");
    expect(s->execute("set_pressure", 2.5).accepted, "seed pressure");
    expect(s->execute("set_p3_run", 1.0).accepted, "seed plc003 running");
    expect(s->execute("set_p3_fault", 0.0).accepted, "seed plc003 ok");
  }
  seeder.disconnect();
}

}  // namespace

int main()
{
  using virtual_factory::ConnectionState;
  using virtual_factory::EtherNetIpIndustrialAdapter;
  using virtual_factory::IndustrialAdapter;
  using virtual_factory::OperationalState;
  using virtual_factory::test::EipTestServer;

  std::cout
      << "eip_adapter_test: DEVELOPMENT / INTEGRATION VALIDATION ONLY\n";

  EipTestServer deviceA;
  expect(deviceA.start(deviceATags()), "device A ab_server starts");
  if (!deviceA.port())
  {
    std::cerr << "ab_server error: " << deviceA.lastError() << std::endl;
    return 1;
  }

  {
    EtherNetIpIndustrialAdapter adapter(
        "eip-a",
        fullDeviceConfig(deviceA.host(), deviceA.port(), deviceA.path()));
    expect(adapter.protocol() == "ethernetip", "protocol ethernetip");
    expect(adapter.connectionState() == ConnectionState::Disconnected,
           "starts disconnected");
    const auto t0 = std::chrono::steady_clock::now();
    expect(adapter.connect(), "connect");
    std::cout << "connect_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count()
              << std::endl;
    expect(adapter.connectionState() == ConnectionState::Connected,
           "connected");
    IndustrialAdapter *asAdapter = &adapter;
    expect(asAdapter->protocol() == "ethernetip", "IndustrialAdapter*");
    adapter.disconnect();
  }

  {
    EtherNetIpIndustrialAdapter adapter(
        "eip-a",
        fullDeviceConfig(deviceA.host(), deviceA.port(), deviceA.path()));
    expect(adapter.connect(), "functional connect");
    seedDevice(deviceA.host(), deviceA.port(), deviceA.path());

    auto *plc001 = adapter.equipmentById("PLC-001");
    auto *plc002 = adapter.equipmentById("PLC-002");
    auto *plc003 = adapter.equipmentById("PLC-003");
    expect(plc001 && plc002 && plc003, "three mappings");

    const auto pollT0 = std::chrono::steady_clock::now();
    adapter.poll();
    std::cout << "poll_ms_3="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - pollT0)
                     .count()
              << std::endl;

    expect(telemetryNear(*plc001, "speed", 1200.0, "rpm"), "telemetry speed");
    expect(telemetryNear(*plc001, "temperature", 36.5, "degC"), "telemetry temp");
    expect(plc001->operationalState() == OperationalState::Running, "state");
    expect(!plc001->fault(), "fault clear on PLC-001");
    expect(telemetryNear(*plc002, "count", 42.0, "ea"), "PLC-002 telemetry");
    expect(plc002->fault(), "machine fault from tag");
    expect(adapter.connectionState() == ConnectionState::Connected,
           "machine fault != comms fault");
    expect(telemetryNear(*plc003, "pressure", 2.5, "bar"), "PLC-003 telemetry");

    expect(plc001->execute("set_speed", 900.0).accepted, "command write");
    expect(!plc001->execute("calibrate").accepted, "unknown command");
    expect(adapter.equipmentById("does-not-exist") == nullptr,
           "unknown equipment");

    expect(adapter.connectionState() == ConnectionState::Connected,
           "adapter still connected after bad command");

    adapter.disconnect();
  }

  {
    virtual_factory::EtherNetIpAdapterConfig bad = fullDeviceConfig(
        "203.0.113.1", 44818, "1,0");
    bad.timeoutMs = 800;
    bad.equipment = {plc001Mapping()};
    EtherNetIpIndustrialAdapter adapter("eip-timeout", bad);
    expect(!adapter.connect(), "timeout connect fails");
    expect(adapter.connectionState() == ConnectionState::Faulted, "Faulted");
    expect(!adapter.lastError().empty(), "lastError on timeout");
  }

  {
    virtual_factory::EtherNetIpAdapterConfig bad = fullDeviceConfig(
        deviceA.host(), 44999, deviceA.path());
    bad.timeoutMs = 800;
    bad.equipment = {plc001Mapping()};
    EtherNetIpIndustrialAdapter adapter("eip-refused", bad);
    expect(!adapter.connect(), "refused connect fails");
    expect(adapter.connectionState() == ConnectionState::Faulted, "refused Faulted");
  }

  {
    virtual_factory::EtherNetIpEquipmentMapping badMap = plc001Mapping();
    badMap.telemetry = {
        {"speed", "DoesNotExist_Tag", EtherNetIpValueType::Dint, "rpm"},
    };
    EtherNetIpIndustrialAdapter adapter(
        "eip-badtag",
        oneMachineConfig(
            deviceA.host(), deviceA.port(), deviceA.path(), badMap));
    expect(!adapter.connect(), "invalid tag fails");
    expect(adapter.connectionState() == ConnectionState::Faulted,
           "invalid tag Faulted");
  }

  {
    EtherNetIpIndustrialAdapter adapter(
        "eip-a",
        oneMachineConfig(
            deviceA.host(), deviceA.port(), deviceA.path(), plc001Mapping()));
    expect(adapter.connect(), "connect before kill");
    expect(!adapter.equipmentById("PLC-001")->fault(), "no auto machine fault");
    deviceA.stop();
    adapter.poll();
    expect(adapter.connectionState() == ConnectionState::Faulted,
           "comms Faulted after device stop");
    expect(adapter.equipmentById("PLC-001") != nullptr, "equipment while Faulted");
    expect(!adapter.equipmentById("PLC-001")->fault(),
           "comms fault != machine fault");
    expect(!adapter.equipmentById("PLC-001")->execute("start").accepted,
           "commands rejected when Faulted");
    expect(deviceA.start(deviceATags()), "restart device A same port");
    expect(adapter.connect(), "explicit reconnect");
    expect(adapter.connectionState() == ConnectionState::Connected,
           "reconnected");
    adapter.poll();
    expect(adapter.connectionState() == ConnectionState::Connected,
           "poll after reconnect");
    adapter.disconnect();
  }

  {
    EipTestServer deviceB;
    expect(deviceB.start(deviceBTags()), "device B starts");
    EtherNetIpIndustrialAdapter adapterA(
        "eip-device-a",
        oneMachineConfig(
            deviceA.host(), deviceA.port(), deviceA.path(), plc001Mapping()));
    EtherNetIpIndustrialAdapter adapterB(
        "eip-device-b",
        oneMachineConfig(
            deviceB.host(), deviceB.port(), deviceB.path(), plc001Mapping()));
    expect(adapterA.connect() && adapterB.connect(), "both connect");

    virtual_factory::EtherNetIpEquipmentMapping seed = plc001Mapping();
    seed.commands = {{"set_speed", "Plc001_Speed", EtherNetIpValueType::Dint}};
    seed.telemetry.clear();
    seed.state = {};
    seed.fault = {};
    EtherNetIpIndustrialAdapter seedA(
        "seed-a",
        oneMachineConfig(deviceA.host(), deviceA.port(), deviceA.path(), seed));
    EtherNetIpIndustrialAdapter seedB(
        "seed-b",
        oneMachineConfig(deviceB.host(), deviceB.port(), deviceB.path(), seed));
    expect(seedA.connect() && seedB.connect(), "seeders");
    expect(seedA.equipmentById("PLC-001")->execute("set_speed", 111.0).accepted,
           "seed A");
    expect(seedB.equipmentById("PLC-001")->execute("set_speed", 222.0).accepted,
           "seed B");
    seedA.disconnect();
    seedB.disconnect();

    adapterA.poll();
    adapterB.poll();
    expect(telemetryNear(*adapterA.equipmentById("PLC-001"), "speed", 111.0),
           "device A isolated");
    expect(telemetryNear(*adapterB.equipmentById("PLC-001"), "speed", 222.0),
           "device B isolated");

    const long rssBefore = readRssKb();
    const int fdsBefore = countOpenFds();
    deviceA.stop();
    adapterA.poll();
    expect(adapterA.connectionState() == ConnectionState::Faulted, "A Faulted");
    expect(adapterB.connectionState() == ConnectionState::Connected, "B ok");
    adapterB.poll();
    std::cout << "VALIDATED UNDER TEST CONDITIONS rss_kb="
              << rssBefore << " fds=" << fdsBefore << std::endl;

    adapterA.disconnect();
    adapterB.disconnect();
    deviceB.stop();
  }

  std::cout << "eip_adapter_test failures=" << failures << std::endl;
  return failures == 0 ? 0 : 1;
}
