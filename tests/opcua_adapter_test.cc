#include "opcua_test_server.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
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

/// Bind telemetry() to a local first. Pointers into a temporary vector dangle.
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

virtual_factory::OpcUaEquipmentMapping mixerMapping()
{
  using virtual_factory::OpcUaNodeRef;
  using virtual_factory::test::OpcUaTestServer;

  virtual_factory::OpcUaEquipmentMapping mixer;
  mixer.id = OpcUaTestServer::kMixerId;
  mixer.type = "mixer";
  mixer.capabilities = {"start", "stop", "set_speed"};
  mixer.commands = {
      {"start", OpcUaNodeRef{1, OpcUaTestServer::kMixerStart}},
      {"stop", OpcUaNodeRef{1, OpcUaTestServer::kMixerStop}},
      {"set_speed", OpcUaNodeRef{1, OpcUaTestServer::kMixerSpeedSetpoint}},
  };
  mixer.telemetry = {
      {"speed", OpcUaNodeRef{1, OpcUaTestServer::kMixerSpeedActual}, "rpm"},
      {"temperature", OpcUaNodeRef{1, OpcUaTestServer::kMixerTemperature},
       "degC"},
  };
  mixer.stateNode = OpcUaNodeRef{1, OpcUaTestServer::kMixerRunning};
  mixer.faultNode = OpcUaNodeRef{1, OpcUaTestServer::kMixerFault};
  return mixer;
}

virtual_factory::OpcUaEquipmentMapping pumpMapping()
{
  using virtual_factory::OpcUaNodeRef;
  using virtual_factory::test::OpcUaTestServer;

  virtual_factory::OpcUaEquipmentMapping pump;
  pump.id = OpcUaTestServer::kPumpId;
  pump.type = "centrifugal_pump";
  pump.capabilities = {"start", "stop"};
  pump.commands = {
      {"start", OpcUaNodeRef{1, OpcUaTestServer::kPumpStart}},
      {"stop", OpcUaNodeRef{1, OpcUaTestServer::kPumpStop}},
  };
  pump.telemetry = {
      {"flow_rate", OpcUaNodeRef{1, OpcUaTestServer::kPumpFlowRate}, "L/min"},
      {"pressure", OpcUaNodeRef{1, OpcUaTestServer::kPumpPressure}, "bar"},
  };
  pump.stateNode = OpcUaNodeRef{1, OpcUaTestServer::kPumpRunning};
  pump.faultNode = OpcUaNodeRef{1, OpcUaTestServer::kPumpFault};
  return pump;
}

virtual_factory::OpcUaEquipmentMapping unknownMapping()
{
  using virtual_factory::OpcUaNodeRef;
  using virtual_factory::test::OpcUaTestServer;

  virtual_factory::OpcUaEquipmentMapping machine;
  machine.id = OpcUaTestServer::kUnknownId;
  machine.type = "special_processing_machine";
  machine.capabilities = {"start", "stop"};
  machine.commands = {
      {"start", OpcUaNodeRef{1, OpcUaTestServer::kUnknownStart}},
      {"stop", OpcUaNodeRef{1, OpcUaTestServer::kUnknownStop}},
  };
  machine.telemetry = {
      {"temperature", OpcUaNodeRef{1, OpcUaTestServer::kUnknownTemperature},
       "degC"},
  };
  machine.stateNode = OpcUaNodeRef{1, OpcUaTestServer::kUnknownRunning};
  machine.faultNode = OpcUaNodeRef{1, OpcUaTestServer::kUnknownFault};
  return machine;
}

virtual_factory::OpcUaAdapterConfig singleServerConfig(
    const std::string &endpoint)
{
  virtual_factory::OpcUaAdapterConfig config;
  config.endpointUrl = endpoint;
  config.equipment.push_back(mixerMapping());
  config.equipment.push_back(pumpMapping());
  config.equipment.push_back(unknownMapping());
  return config;
}

virtual_factory::OpcUaAdapterConfig oneMachineConfig(
    const std::string &endpoint,
    virtual_factory::OpcUaEquipmentMapping machine)
{
  virtual_factory::OpcUaAdapterConfig config;
  config.endpointUrl = endpoint;
  config.equipment.push_back(std::move(machine));
  return config;
}

/// Two adapters, two servers: one PLC failure must not disable the other.
void testMultipleServers()
{
  virtual_factory::test::OpcUaTestServer serverA;
  virtual_factory::test::OpcUaTestServer serverB;
  expect(serverA.start(), "server A starts");
  expect(serverB.start(), "server B starts");
  expect(serverA.endpointUrl() != serverB.endpointUrl(),
         "two OPC UA servers have distinct endpoints");
  if (!serverA.start() || !serverB.start())
  {
    return;
  }

  virtual_factory::OpcUaIndustrialAdapter plcA(
      "opcua-plc-a", oneMachineConfig(serverA.endpointUrl(), mixerMapping()));
  virtual_factory::OpcUaIndustrialAdapter plcB(
      "opcua-plc-b", oneMachineConfig(serverB.endpointUrl(), pumpMapping()));

  expect(plcA.connect(), "PLC-A connects");
  expect(plcB.connect(), "PLC-B connects");
  expect(plcA.connected() && plcB.connected(),
         "both OPC UA connections are Connected");

  virtual_factory::IndustrialAdapter *asA = &plcA;
  virtual_factory::IndustrialAdapter *asB = &plcB;
  expect(asA->equipmentById("TEST-MIXER-001") != nullptr,
         "mixer belongs to PLC-A adapter");
  expect(asA->equipmentById("Pump_01") == nullptr,
         "pump is not on PLC-A");
  expect(asB->equipmentById("Pump_01") != nullptr,
         "pump belongs to PLC-B adapter");
  expect(asB->equipmentById("TEST-MIXER-001") == nullptr,
         "mixer is not on PLC-B");
  expect(asA->equipmentById("NO-SUCH-MACHINE") == nullptr,
         "unknown id on PLC-A is not invented");

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

  expect(telemetryNear(*mixer, "speed", 42.0),
         "poll reads mixer from server A");
  expect(telemetryNear(*pump, "flow_rate", 125.4),
         "poll reads pump from server B");

  expect(mixer->execute("start").accepted, "command to mixer writes server A");
  expect(serverA.startNode(), "Start node changed on server A");
  expect(!serverB.booleanNode(
             virtual_factory::test::OpcUaTestServer::kMixerStart),
         "server B mixer Start node unchanged");
  expect(pump->execute("start").accepted, "command to pump writes server B");
  expect(serverB.booleanNode(
             virtual_factory::test::OpcUaTestServer::kPumpStart),
         "Pump Start node changed on server B");
  expect(!pump->execute("calibrate").accepted,
         "unknown command on PLC-B is rejected");

  serverA.stop();
  asA->poll();
  asB->poll();
  expect(plcA.connectionState() ==
             virtual_factory::ConnectionState::Faulted,
         "PLC-A is Faulted after its server stops");
  expect(plcB.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "PLC-B stays Connected when PLC-A fails");
  expect(pump->execute("stop").accepted,
         "commands still work on the healthy server");
  expect(serverB.booleanNode(
             virtual_factory::test::OpcUaTestServer::kPumpStop),
         "stop reached server B while A is down");
  expect(!mixer->execute("stop").accepted,
         "commands on the failed server are rejected");
  expect(!mixer->fault(),
         "PLC-A comms failure is not a mixer process fault");
  expect(pump->running(), "PLC-B equipment operational state is unchanged");

  serverB.setDoubleNode(
      virtual_factory::test::OpcUaTestServer::kPumpFlowRate, 90.0);
  asB->poll();
  expect(telemetryNear(*pump, "flow_rate", 90.0),
         "PLC-B telemetry still updates while PLC-A is down");

  expect(serverA.start(), "server A restarts");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  expect(plcA.connect(), "PLC-A reconnects without touching PLC-B");
  expect(plcA.connected(), "PLC-A is Connected after reconnect");
  expect(plcB.connected(), "PLC-B remained Connected during PLC-A reconnect");
  mixer = asA->equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "mixer re-exposed after PLC-A reconnect");
  asA->poll();
  expect(mixer != nullptr && telemetryNear(*mixer, "speed", 42.0),
         "PLC-A telemetry returns after reconnect");
  asB->poll();
  expect(telemetryNear(*pump, "flow_rate", 90.0),
         "PLC-B telemetry undisturbed by PLC-A reconnect");

  plcA.disconnect();
  plcB.disconnect();
  serverA.stop();
  serverB.stop();
}

/// MES/SCADA-shaped client: IndustrialAdapter + Equipment only. No NodeIds.
void useAsMes(virtual_factory::IndustrialAdapter &adapter)
{
  expect(adapter.protocol() == "opcua",
         "MES sees protocol metadata opcua, not node IDs");
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
  expect(!mixer->hasCapability("load_recipe"),
         "MES does not assume conveyor/recipe commands");

  virtual_factory::Equipment *pump = adapter.equipmentById("Pump_01");
  expect(pump != nullptr, "MES finds Pump_01 without Pump.hh");
  if (pump != nullptr)
  {
    expect(pump->type() == "centrifugal_pump", "pump type is metadata");
    expect(!pump->hasCapability("set_speed"),
           "pump mapping does not invent conveyor speed");
  }

  virtual_factory::Equipment *unknown =
      adapter.equipmentById("UnknownMachine_01");
  expect(unknown != nullptr, "MES finds UnknownMachine_01 without a C++ class");
  if (unknown != nullptr)
  {
    expect(unknown->type() == "special_processing_machine",
           "unknown machine type is open-ended metadata");
  }

  expect(adapter.equipmentById("DOES-NOT-EXIST") == nullptr,
         "unknown equipment id is not invented");

  adapter.poll();
  expect(telemetryNear(*mixer, "speed", 42.0, "rpm"),
         "MES reads mixer speed telemetry without OPC UA node IDs");
  if (pump != nullptr)
  {
    expect(telemetryNear(*pump, "flow_rate", 125.4, "L/min"),
           "MES reads pump flow_rate from OPC UA via generic telemetry");
    expect(telemetryNear(*pump, "pressure", 2.1, "bar"),
           "MES reads pump pressure telemetry");
    expect(pump->running(), "pump Running node maps to equipment Running");
    expect(!pump->fault(), "pump Fault=false is not a machine fault");
  }
  if (unknown != nullptr)
  {
    expect(telemetryNear(*unknown, "temperature", 72.5, "degC"),
           "MES reads unknown-machine temperature telemetry");
    expect(!unknown->running(), "unknown machine Running=false is Stopped");
    expect(unknown->fault(),
           "unknown machine Fault=true is a process fault, not comms");
  }
}

}  // namespace

int main()
{
  {
    using virtual_factory::opcUaNodeRefFromConfig;
    const auto expanded = opcUaNodeRefFromConfig(2, "ns=2;s=MotorSpeed");
    expect(expanded.namespaceIndex == 2, "expanded NodeId namespaceIndex");
    expect(expanded.identifier == "MotorSpeed", "expanded NodeId identifier stripped");
    const auto bare = opcUaNodeRefFromConfig(2, "MotorSpeed");
    expect(bare.namespaceIndex == 2, "bare identifier keeps namespaceIndex");
    expect(bare.identifier == "MotorSpeed", "bare identifier preserved");
  }

  virtual_factory::test::OpcUaTestServer server;
  expect(server.start(), "test OPC UA server starts");
  if (!server.start())
  {
    return EXIT_FAILURE;
  }

  virtual_factory::OpcUaIndustrialAdapter adapter(
      "adapter-opcua-1", singleServerConfig(server.endpointUrl()));

  expect(adapter.id() == "adapter-opcua-1", "adapter construction id");
  expect(adapter.protocol() == "opcua", "protocol is opcua");
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
         "mapped mixer Running=false is Stopped");
  expect(!mixer->fault(), "mapped mixer Fault=false is not a machine fault");

  expect(mixer->execute("start").accepted, "execute start is accepted");
  expect(server.startNode(), "mixer Start node written true");
  expect(!server.stopNode(), "mixer Stop node unchanged by start");

  server.setRunning(true);
  adapter.poll();
  expect(mixer->running(), "poll maps Running=true to equipment Running");
  expect(adapter.connected(),
         "successful command/poll leaves adapter connected");

  expect(mixer->execute("stop").accepted, "execute stop is accepted");
  expect(server.stopNode(), "mixer Stop node written true");

  server.setRunning(false);
  adapter.poll();
  expect(!mixer->running(), "poll maps Running=false to Stopped");

  expect(mixer->execute("set_speed", 1200.0).accepted,
         "execute set_speed writes setpoint");
  expect(near(server.speedSetpoint(), 1200.0),
         "SpeedSetpoint node received 1200");

  server.setSpeedActual(1185.0);
  server.setTemperature(82.5);
  adapter.poll();
  expect(telemetryNear(*mixer, "speed", 1185.0),
         "poll updates speed telemetry from OPC UA");
  expect(telemetryNear(*mixer, "temperature", 82.5),
         "poll updates temperature telemetry from OPC UA");

  virtual_factory::Equipment *pump = adapter.equipmentById("Pump_01");
  expect(pump != nullptr, "pump still exposed");
  if (pump != nullptr)
  {
    expect(pump->execute("start").accepted, "pump start writes OPC UA");
    expect(server.booleanNode(virtual_factory::test::OpcUaTestServer::kPumpStart),
           "Pump_01.Start node written true");
    expect(!pump->execute("set_speed", 1.0).accepted,
           "unmapped pump command is rejected");
  }

  expect(!mixer->execute("calibrate").accepted,
         "unmapped mixer command is rejected");

  server.setFault(true);
  adapter.poll();
  expect(mixer->fault(), "machine Fault node maps to Equipment::fault");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "machine fault is not a communication fault");

  server.setFault(false);
  adapter.poll();
  expect(!mixer->fault(), "clearing Fault node clears equipment fault");

  const std::string endpoint = server.endpointUrl();
  server.stop();
  adapter.poll();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Faulted,
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

  expect(server.start(), "test server restarts on the same port");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  expect(server.endpointUrl() == endpoint, "reconnect uses the same endpoint");
  expect(adapter.connect(), "connect after Faulted reconnects");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "reconnected adapter is Connected");
  expect(adapter.lastError().empty(), "reconnect clears last error");
  virtual_factory::Equipment *pumpAgain = adapter.equipmentById("Pump_01");
  expect(pumpAgain != nullptr, "pump re-exposed after reconnect");
  if (pumpAgain != nullptr)
  {
    adapter.poll();
    expect(telemetryNear(*pumpAgain, "flow_rate", 125.4),
           "telemetry readable again after reconnect");
  }

  adapter.disconnect();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "disconnect returns to Disconnected");
  expect(adapter.equipment().empty(), "equipment hidden after disconnect");

  server.stop();

  testMultipleServers();

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "opcua_adapter_test: all assertions passed" << std::endl;
  return EXIT_SUCCESS;
}
