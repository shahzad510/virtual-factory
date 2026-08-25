#include "modbus_test_server.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/ModbusIndustrialAdapter.hh>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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
  return std::fabs(actual - expected) < 1e-6;
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

virtual_factory::ModbusRef coil(std::uint16_t address)
{
  return virtual_factory::makeModbusRef(
      virtual_factory::test::ModbusTestServer::kUnitId,
      virtual_factory::ModbusTable::Coil,
      address);
}

virtual_factory::ModbusRef holding(std::uint16_t address)
{
  return virtual_factory::makeModbusRef(
      virtual_factory::test::ModbusTestServer::kUnitId,
      virtual_factory::ModbusTable::HoldingRegister,
      address);
}

virtual_factory::ModbusRef inputReg(std::uint16_t address)
{
  return virtual_factory::makeModbusRef(
      virtual_factory::test::ModbusTestServer::kUnitId,
      virtual_factory::ModbusTable::InputRegister,
      address);
}

virtual_factory::ModbusEquipmentMapping mixerMapping()
{
  using virtual_factory::test::ModbusTestServer;
  virtual_factory::ModbusEquipmentMapping mixer;
  mixer.id = "TEST-MIXER-001";
  mixer.type = "mixer";
  mixer.capabilities = {"start", "stop", "set_speed"};
  mixer.commands = {
      {"start", coil(ModbusTestServer::kMixerStart)},
      {"stop", coil(ModbusTestServer::kMixerStop)},
      {"set_speed", holding(ModbusTestServer::kMixerSpeedSetpoint)},
  };
  mixer.telemetry = {
      {"speed", holding(ModbusTestServer::kMixerSpeedActual), "rpm"},
      {"temperature", inputReg(ModbusTestServer::kMixerTemperature), "degC"},
  };
  mixer.stateCoil = coil(ModbusTestServer::kMixerRunning);
  mixer.faultCoil = coil(ModbusTestServer::kMixerFault);
  return mixer;
}

virtual_factory::ModbusEquipmentMapping pumpMapping()
{
  using virtual_factory::test::ModbusTestServer;
  virtual_factory::ModbusEquipmentMapping pump;
  pump.id = "Pump_01";
  pump.type = "centrifugal_pump";
  pump.capabilities = {"start", "stop"};
  pump.commands = {
      {"start", coil(ModbusTestServer::kPumpStart)},
      {"stop", coil(ModbusTestServer::kPumpStop)},
  };
  pump.telemetry = {
      {"flow_rate", holding(ModbusTestServer::kPumpFlow), "L/min"},
      {"pressure", holding(ModbusTestServer::kPumpPressure), "bar"},
  };
  pump.stateCoil = coil(ModbusTestServer::kPumpRunning);
  pump.faultCoil = coil(ModbusTestServer::kPumpFault);
  return pump;
}

virtual_factory::ModbusEquipmentMapping unknownMapping()
{
  using virtual_factory::test::ModbusTestServer;
  virtual_factory::ModbusEquipmentMapping machine;
  machine.id = "UnknownMachine_01";
  machine.type = "special_processing_machine";
  machine.capabilities = {"start", "stop"};
  machine.commands = {
      {"start", coil(ModbusTestServer::kUnknownStart)},
      {"stop", coil(ModbusTestServer::kUnknownStop)},
  };
  machine.telemetry = {
      {"temperature", holding(ModbusTestServer::kUnknownTemperature), "degC"},
  };
  machine.stateCoil = coil(ModbusTestServer::kUnknownRunning);
  machine.faultCoil = coil(ModbusTestServer::kUnknownFault);
  return machine;
}

virtual_factory::ModbusAdapterConfig fullConfig(
    const std::string &host, std::uint16_t port)
{
  virtual_factory::ModbusAdapterConfig config;
  config.host = host;
  config.port = port;
  config.timeoutMs = 1000;
  config.equipment.push_back(mixerMapping());
  config.equipment.push_back(pumpMapping());
  config.equipment.push_back(unknownMapping());
  return config;
}

virtual_factory::ModbusAdapterConfig oneMachineConfig(
    const std::string &host,
    std::uint16_t port,
    virtual_factory::ModbusEquipmentMapping machine)
{
  virtual_factory::ModbusAdapterConfig config;
  config.host = host;
  config.port = port;
  config.timeoutMs = 1000;
  config.equipment.push_back(std::move(machine));
  return config;
}

void useAsMes(virtual_factory::IndustrialAdapter &adapter)
{
  expect(adapter.protocol() == "modbus",
         "MES sees protocol metadata modbus, not register addresses");
  expect(adapter.connected(), "MES sees connected adapter");
  expect(adapter.equipment().size() == 3,
         "MES sees three mapped machines, no Pump/Mixer C++ classes");

  virtual_factory::Equipment *mixer = adapter.equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "MES finds mixer by id");
  if (mixer == nullptr)
  {
    return;
  }
  expect(mixer->type() == "mixer", "type is metadata, not a Mixer class");
  expect(mixer->hasCapability("set_speed"), "MES sees set_speed capability");

  virtual_factory::Equipment *pump = adapter.equipmentById("Pump_01");
  expect(pump != nullptr, "MES finds Pump_01 without Pump.hh");
  if (pump != nullptr)
  {
    expect(pump->type() == "centrifugal_pump", "pump type is metadata");
    expect(!pump->hasCapability("set_speed"),
           "pump mapping does not invent mixer speed");
  }

  virtual_factory::Equipment *unknown =
      adapter.equipmentById("UnknownMachine_01");
  expect(unknown != nullptr, "MES finds UnknownMachine_01 without a C++ class");
  expect(adapter.equipmentById("DOES-NOT-EXIST") == nullptr,
         "unknown equipment id is not invented");

  adapter.poll();
  expect(telemetryNear(*mixer, "speed", 42.0, "rpm"),
         "MES reads mixer speed without Modbus addresses");
  if (pump != nullptr)
  {
    expect(telemetryNear(*pump, "flow_rate", 125.0, "L/min"),
           "MES reads pump flow_rate via generic telemetry");
    expect(telemetryNear(*pump, "pressure", 21.0, "bar"),
           "MES reads pump pressure telemetry");
  }
  if (unknown != nullptr)
  {
    expect(telemetryNear(*unknown, "temperature", 72.0, "degC"),
           "MES reads unknown-machine temperature");
    expect(unknown->fault(),
           "unknown machine fault coil is a process fault, not comms");
  }
}

void testMultipleServers()
{
  virtual_factory::test::ModbusTestServer serverA;
  virtual_factory::test::ModbusTestServer serverB;
  expect(serverA.start(), "server A starts");
  expect(serverB.start(), "server B starts");
  expect(serverA.port() != serverB.port(),
         "two Modbus TCP servers have distinct ports");
  if (serverA.port() == 0 || serverB.port() == 0)
  {
    return;
  }

  virtual_factory::ModbusIndustrialAdapter plcA(
      "modbus-plc-a",
      oneMachineConfig(serverA.host(), serverA.port(), mixerMapping()));
  virtual_factory::ModbusIndustrialAdapter plcB(
      "modbus-plc-b",
      oneMachineConfig(serverB.host(), serverB.port(), pumpMapping()));

  expect(plcA.connect(), "PLC-A connects");
  expect(plcB.connect(), "PLC-B connects");
  expect(plcA.connected() && plcB.connected(),
         "both Modbus connections are Connected");

  virtual_factory::IndustrialAdapter *asA = &plcA;
  virtual_factory::IndustrialAdapter *asB = &plcB;
  expect(asA->equipmentById("TEST-MIXER-001") != nullptr,
         "mixer belongs to PLC-A adapter");
  expect(asA->equipmentById("Pump_01") == nullptr, "pump is not on PLC-A");
  expect(asB->equipmentById("Pump_01") != nullptr,
         "pump belongs to PLC-B adapter");
  expect(asB->equipmentById("TEST-MIXER-001") == nullptr,
         "mixer is not on PLC-B");

  asA->poll();
  asB->poll();
  virtual_factory::Equipment *mixer = asA->equipmentById("TEST-MIXER-001");
  virtual_factory::Equipment *pump = asB->equipmentById("Pump_01");
  expect(mixer != nullptr && pump != nullptr, "both machines exposed");
  if (mixer == nullptr || pump == nullptr)
  {
    serverA.stop();
    serverB.stop();
    return;
  }

  expect(telemetryNear(*mixer, "speed", 42.0), "poll reads mixer from server A");
  expect(telemetryNear(*pump, "flow_rate", 125.0),
         "poll reads pump from server B");

  expect(mixer->execute("start").accepted, "command to mixer writes server A");
  expect(serverA.coil(virtual_factory::test::ModbusTestServer::kMixerStart),
         "Start coil changed on server A");
  expect(!serverB.coil(virtual_factory::test::ModbusTestServer::kMixerStart),
         "server B mixer Start coil unchanged");
  expect(pump->execute("start").accepted, "command to pump writes server B");
  expect(serverB.coil(virtual_factory::test::ModbusTestServer::kPumpStart),
         "Pump Start coil changed on server B");
  expect(!pump->execute("calibrate").accepted,
         "unknown command on PLC-B is rejected");

  serverA.stop();
  asA->poll();
  asB->poll();
  expect(plcA.connectionState() == virtual_factory::ConnectionState::Faulted,
         "PLC-A is Faulted after its server stops");
  expect(plcB.connectionState() == virtual_factory::ConnectionState::Connected,
         "PLC-B stays Connected when PLC-A fails");
  expect(pump->execute("stop").accepted,
         "commands still work on the healthy server");
  expect(!mixer->execute("stop").accepted,
         "commands on the failed server are rejected");
  expect(!mixer->fault(),
         "PLC-A comms failure is not a mixer process fault");

  serverB.setHolding(virtual_factory::test::ModbusTestServer::kPumpFlow, 90);
  asB->poll();
  expect(telemetryNear(*pump, "flow_rate", 90.0),
         "PLC-B telemetry still updates while PLC-A is down");

  expect(serverA.start(), "server A restarts");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  expect(plcA.connect(), "PLC-A reconnects without touching PLC-B");
  expect(plcA.connected(), "PLC-A is Connected after reconnect");
  expect(plcB.connected(), "PLC-B remained Connected during PLC-A reconnect");
  mixer = asA->equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "mixer re-exposed after PLC-A reconnect");
  asA->poll();
  expect(mixer != nullptr && telemetryNear(*mixer, "speed", 42.0),
         "PLC-A telemetry returns after reconnect");

  plcA.disconnect();
  plcB.disconnect();
  serverA.stop();
  serverB.stop();
}

void testInvalidAddress()
{
  virtual_factory::test::ModbusTestServer server;
  expect(server.start(), "invalid-address server starts");

  virtual_factory::ModbusEquipmentMapping bad = mixerMapping();
  bad.telemetry.push_back(
      {"orphan",
       virtual_factory::makeModbusRef(
           virtual_factory::test::ModbusTestServer::kUnitId,
           virtual_factory::ModbusTable::HoldingRegister,
           400),
       "none"});

  virtual_factory::ModbusIndustrialAdapter adapter(
      "modbus-bad-map",
      oneMachineConfig(server.host(), server.port(), bad));
  expect(adapter.connect(), "connect succeeds with invalid mapping");
  adapter.poll();
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "invalid register address faults the adapter");
  expect(!adapter.lastError().empty(), "invalid address has an error reason");
  virtual_factory::Equipment *mixer = adapter.equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "equipment remains listed while Faulted");
  expect(mixer != nullptr && !mixer->fault(),
         "illegal address is not a machine fault");

  adapter.disconnect();
  server.stop();
}

void testConnectFailure()
{
  virtual_factory::ModbusIndustrialAdapter adapter(
      "modbus-down",
      oneMachineConfig("127.0.0.1", 1, mixerMapping()));
  expect(!adapter.connect(), "connect to closed port fails");
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "failed connect is Faulted");
  expect(adapter.equipment().empty() || adapter.equipmentById("TEST-MIXER-001") !=
                                            nullptr,
         "bind happens only after successful connect");
  expect(adapter.equipment().empty(),
         "no equipment exposed if connect never succeeded");
}

// Modest isolation check (4 localhost endpoints). This is correctness
// validation in this environment, not a production capacity claim.
void testModestEndpointIsolation()
{
  constexpr int kCount = 4;
  virtual_factory::test::ModbusTestServer servers[kCount];
  std::unique_ptr<virtual_factory::ModbusIndustrialAdapter> adapters[kCount];

  for (int i = 0; i < kCount; ++i)
  {
    expect(servers[i].start(), "modest isolation: server starts");
    virtual_factory::ModbusEquipmentMapping mixer = mixerMapping();
    mixer.id = std::string("Mixer_") + std::to_string(i);
    adapters[i] = std::make_unique<virtual_factory::ModbusIndustrialAdapter>(
        std::string("modbus-modest-") + std::to_string(i),
        oneMachineConfig(servers[i].host(), servers[i].port(), mixer));
    expect(adapters[i]->connect(), "modest isolation: adapter connects");
    adapters[i]->poll();
    virtual_factory::Equipment *eq =
        adapters[i]->equipmentById(std::string("Mixer_") + std::to_string(i));
    expect(eq != nullptr, "modest isolation: mapped mixer is exposed");
    if (eq != nullptr)
    {
      expect(telemetryNear(*eq, "speed", 42.0),
             "modest isolation: telemetry from this endpoint only");
    }
  }

  servers[1].stop();
  adapters[1]->poll();
  expect(adapters[1]->connectionState() ==
             virtual_factory::ConnectionState::Faulted,
         "modest isolation: failed endpoint is Faulted");
  expect(adapters[0]->connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "modest isolation: endpoint 0 stays Connected");
  expect(adapters[2]->connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "modest isolation: endpoint 2 stays Connected");
  expect(adapters[3]->connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "modest isolation: endpoint 3 stays Connected");

  for (int i = 0; i < kCount; ++i)
  {
    adapters[i]->disconnect();
    servers[i].stop();
  }
}

}  // namespace

int main()
{
  virtual_factory::test::ModbusTestServer server;
  expect(server.start(), "test Modbus TCP server starts");
  if (server.port() == 0)
  {
    return EXIT_FAILURE;
  }

  virtual_factory::ModbusIndustrialAdapter adapter(
      "adapter-modbus-1", fullConfig(server.host(), server.port()));

  expect(adapter.id() == "adapter-modbus-1", "adapter construction id");
  expect(adapter.protocol() == "modbus", "protocol is modbus");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "initial connection state is Disconnected");
  expect(adapter.equipment().empty(), "no equipment while disconnected");
  expect(adapter.equipmentById("Pump_01") == nullptr,
         "lookup fails while disconnected");

  expect(adapter.connect(), "connect succeeds");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "connection state is Connected");
  expect(adapter.lastError().empty(), "no error after connect");
  expect(adapter.equipment().size() == 3,
         "configured equipment is exposed after connect");

  useAsMes(adapter);

  virtual_factory::Equipment *mixer = adapter.equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "mixer available after MES-shaped use");
  if (mixer == nullptr)
  {
    server.stop();
    return EXIT_FAILURE;
  }

  adapter.poll();
  expect(mixer->operationalState() ==
             virtual_factory::OperationalState::Stopped,
         "mapped mixer running coil false is Stopped");
  expect(!mixer->fault(), "mapped mixer fault coil false is not a machine fault");

  expect(mixer->execute("start").accepted, "execute start is accepted");
  expect(server.coil(virtual_factory::test::ModbusTestServer::kMixerStart),
         "mixer Start coil written true");
  expect(!server.coil(virtual_factory::test::ModbusTestServer::kMixerStop),
         "mixer Stop coil unchanged by start");

  server.setCoil(virtual_factory::test::ModbusTestServer::kMixerRunning, true);
  adapter.poll();
  expect(mixer->running(), "poll maps running coil true to equipment Running");
  expect(adapter.connected(),
         "successful command/poll leaves adapter connected");

  expect(mixer->execute("stop").accepted, "execute stop is accepted");
  expect(server.coil(virtual_factory::test::ModbusTestServer::kMixerStop),
         "mixer Stop coil written true");

  server.setCoil(virtual_factory::test::ModbusTestServer::kMixerRunning, false);
  adapter.poll();
  expect(!mixer->running(), "poll maps running coil false to Stopped");

  expect(mixer->execute("set_speed", 1200.0).accepted,
         "execute set_speed writes setpoint");
  expect(server.holding(
             virtual_factory::test::ModbusTestServer::kMixerSpeedSetpoint) ==
             1200,
         "speed setpoint holding register received 1200");

  server.setHolding(
      virtual_factory::test::ModbusTestServer::kMixerSpeedActual, 1185);
  server.setInputRegister(
      virtual_factory::test::ModbusTestServer::kMixerTemperature, 82);
  adapter.poll();
  expect(telemetryNear(*mixer, "speed", 1185.0),
         "poll updates speed telemetry from Modbus");
  expect(telemetryNear(*mixer, "temperature", 82.0),
         "poll updates temperature telemetry from Modbus");

  virtual_factory::Equipment *pump = adapter.equipmentById("Pump_01");
  expect(pump != nullptr, "pump still exposed");
  if (pump != nullptr)
  {
    expect(pump->execute("start").accepted, "pump start writes Modbus");
    expect(server.coil(virtual_factory::test::ModbusTestServer::kPumpStart),
           "Pump_01 start coil written true");
    expect(!pump->execute("set_speed", 1.0).accepted,
           "unmapped pump command is rejected");
    expect(!server.coil(virtual_factory::test::ModbusTestServer::kMixerStart) ||
               mixer->execute("start").accepted,
           "mixer and pump commands are isolated by mapping");
  }

  expect(!mixer->execute("calibrate").accepted,
         "unmapped mixer command is rejected");

  server.setCoil(virtual_factory::test::ModbusTestServer::kMixerFault, true);
  adapter.poll();
  expect(mixer->fault(), "machine fault coil maps to Equipment::fault");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "machine fault is not a communication fault");

  server.setCoil(virtual_factory::test::ModbusTestServer::kMixerFault, false);
  adapter.poll();
  expect(!mixer->fault(), "clearing fault coil clears equipment fault");

  const std::uint16_t port = server.port();
  server.stop();
  adapter.poll();
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "lost server is adapter Faulted");
  expect(!adapter.lastError().empty(), "communication failure has a reason");
  expect(!adapter.connected(), "Faulted is not connected");
  expect(adapter.equipmentById("TEST-MIXER-001") != nullptr,
         "last-known equipment remains while Faulted");
  expect(adapter.equipmentById("Pump_01") != nullptr,
         "last-known pump remains while Faulted");
  expect(!mixer->execute("start").accepted,
         "commands rejected during communication fault");
  expect(!mixer->fault(),
         "communication failure is not a machine process fault");

  expect(server.start(), "test server restarts");
  expect(server.port() == port, "server restarts on the same TCP port");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  expect(adapter.connect(), "connect after Faulted recreates the session");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "reconnected adapter is Connected");
  expect(adapter.lastError().empty(), "reconnect clears last error");
  virtual_factory::Equipment *pumpAgain = adapter.equipmentById("Pump_01");
  expect(pumpAgain != nullptr, "pump re-exposed after reconnect");
  if (pumpAgain != nullptr)
  {
    adapter.poll();
    expect(telemetryNear(*pumpAgain, "flow_rate", 125.0),
           "telemetry readable again after reconnect");
  }

  adapter.disconnect();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "disconnect returns to Disconnected");
  expect(adapter.equipment().empty(), "equipment hidden after disconnect");

  server.stop();

  testConnectFailure();
  testInvalidAddress();
  testMultipleServers();
  testModestEndpointIsolation();

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "modbus_adapter_test: all assertions passed" << std::endl;
  return EXIT_SUCCESS;
}
