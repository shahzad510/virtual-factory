#include "rest_test_server.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/RestIndustrialAdapter.hh>

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

virtual_factory::RestEquipmentMapping mixerMapping()
{
  virtual_factory::RestEquipmentMapping mixer;
  mixer.id = "TEST-MIXER-001";
  mixer.type = "mixer";
  mixer.capabilities = {"start", "stop", "set_speed"};
  mixer.commands = {
      {"start", virtual_factory::RestHttpMethod::Post, "/api/mixer/start",
       "{\"command\":\"start\"}"},
      {"stop", virtual_factory::RestHttpMethod::Post, "/api/mixer/stop",
       "{\"command\":\"stop\"}"},
      {"set_speed", virtual_factory::RestHttpMethod::Put, "/api/mixer/speed",
       "{\"value\":{{value}}}"},
  };
  mixer.telemetryPath = "/api/mixer";
  mixer.telemetry = {
      {"speed", "/telemetry/speed", "rpm"},
      {"temperature", "/telemetry/temperature", "degC"},
  };
  mixer.statePointer = "/running";
  mixer.faultPointer = "/fault";
  return mixer;
}

virtual_factory::RestEquipmentMapping pumpMapping()
{
  virtual_factory::RestEquipmentMapping pump;
  pump.id = "Pump_01";
  pump.type = "centrifugal_pump";
  pump.capabilities = {"start", "stop"};
  pump.commands = {
      {"start", virtual_factory::RestHttpMethod::Patch, "/api/pump/start",
       "{\"command\":\"start\"}"},
      {"stop", virtual_factory::RestHttpMethod::Post, "/api/pump/stop",
       "{\"command\":\"stop\"}"},
  };
  pump.telemetryPath = "/api/pump";
  pump.telemetry = {
      {"flow_rate", "/telemetry/flow_rate", "L/min"},
      {"pressure", "/telemetry/pressure", "bar"},
  };
  pump.statePointer = "/running";
  pump.faultPointer = "/fault";
  return pump;
}

virtual_factory::RestEquipmentMapping unknownMapping()
{
  virtual_factory::RestEquipmentMapping unknown;
  unknown.id = "UnknownMachine_01";
  unknown.type = "special_processing_machine";
  unknown.capabilities = {"start", "stop"};
  unknown.commands = {
      {"start", virtual_factory::RestHttpMethod::Post, "/api/unknown/start",
       "{}"},
      {"stop", virtual_factory::RestHttpMethod::Post, "/api/unknown/stop",
       "{}"},
  };
  unknown.telemetryPath = "/api/unknown";
  unknown.telemetry = {
      {"temperature", "/telemetry/temperature", "degC"},
  };
  unknown.statePointer = "/running";
  unknown.faultPointer = "/fault";
  return unknown;
}

virtual_factory::RestAdapterConfig originConfig(
    const std::string &host,
    std::uint16_t port,
    std::vector<virtual_factory::RestEquipmentMapping> equipment)
{
  virtual_factory::RestAdapterConfig config;
  config.scheme = "http";
  config.host = host;
  config.port = port;
  config.timeoutMs = 2000;
  config.healthPath = "/health";
  config.tlsVerify = true;
  config.equipment = std::move(equipment);
  return config;
}

virtual_factory::RestAdapterConfig fullConfig(
    const std::string &host, std::uint16_t port)
{
  return originConfig(host, port, {mixerMapping(), pumpMapping(), unknownMapping()});
}

virtual_factory::RestAdapterConfig oneMachineConfig(
    const std::string &host,
    std::uint16_t port,
    virtual_factory::RestEquipmentMapping mapping)
{
  return originConfig(host, port, {std::move(mapping)});
}

void useAsMes(virtual_factory::IndustrialAdapter &adapter)
{
  virtual_factory::IndustrialAdapter *asAdapter = &adapter;
  expect(asAdapter->protocol() == "rest", "MES sees protocol rest");
  expect(asAdapter->equipment().size() == 3,
         "MES sees three mapped machines, no Pump/Mixer C++ classes");

  virtual_factory::Equipment *mixer = asAdapter->equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "MES finds mixer by id");
  if (mixer == nullptr)
  {
    return;
  }
  expect(mixer->type() == "mixer", "type is metadata, not a Mixer class");
  expect(mixer->hasCapability("set_speed"), "MES sees set_speed capability");

  virtual_factory::Equipment *pump = asAdapter->equipmentById("Pump_01");
  expect(pump != nullptr, "MES finds Pump_01 without Pump.hh");
  if (pump != nullptr)
  {
    expect(pump->type() == "centrifugal_pump", "pump type is metadata");
    expect(!pump->hasCapability("set_speed"),
           "pump mapping does not invent mixer speed");
  }

  virtual_factory::Equipment *unknown =
      asAdapter->equipmentById("UnknownMachine_01");
  expect(unknown != nullptr, "MES finds UnknownMachine_01 without a C++ class");
  expect(asAdapter->equipmentById("DOES-NOT-EXIST") == nullptr,
         "unknown equipment id is not invented");

  asAdapter->poll();
  expect(telemetryNear(*mixer, "speed", 42.0, "rpm"),
         "MES reads mixer speed and temperature from one JSON GET");
  expect(telemetryNear(*mixer, "temperature", 21.5, "degC"),
         "multiple telemetry values come from one JSON resource");
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
           "unknown machine JSON fault is a process fault, not comms");
  }
}

void testConnectFailure()
{
  virtual_factory::RestIndustrialAdapter adapter(
      "rest-down", oneMachineConfig("127.0.0.1", 1, mixerMapping()));
  expect(!adapter.connect(), "connect to closed port fails");
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "failed connect is Faulted");
  expect(adapter.equipment().empty(),
         "no equipment exposed if connect never succeeded");
  expect(!adapter.lastError().empty(), "connect failure has a reason");
}

void testHttpError()
{
  virtual_factory::test::RestTestServer server;
  expect(server.start(), "http-error server starts");

  virtual_factory::RestEquipmentMapping bad = mixerMapping();
  bad.telemetryPath = "/fail";
  virtual_factory::RestIndustrialAdapter adapter(
      "rest-http-error",
      oneMachineConfig(server.host(), server.port(), bad));
  expect(adapter.connect(), "connect succeeds before mapped HTTP 500");
  adapter.poll();
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "HTTP 500 during poll is communication Faulted");
  expect(!adapter.lastError().empty(), "HTTP error has a reason");
  virtual_factory::Equipment *mixer = adapter.equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "equipment remains listed while Faulted");
  expect(mixer != nullptr && !mixer->fault(),
         "HTTP 500 is not a machine process fault");

  adapter.disconnect();
  server.stop();
}

void testTimeout()
{
  virtual_factory::test::RestTestServer server;
  expect(server.start(), "timeout server starts");
  server.setSlowDelayMs(1500);

  virtual_factory::RestAdapterConfig config =
      oneMachineConfig(server.host(), server.port(), mixerMapping());
  config.timeoutMs = 200;
  config.healthPath = "/slow";
  virtual_factory::RestIndustrialAdapter adapter("rest-timeout", config);
  expect(!adapter.connect(), "connect health GET times out");
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "timeout is communication Faulted");
  expect(!adapter.lastError().empty(), "timeout has a reason");
  expect(adapter.equipment().empty(), "timeout before bind exposes no equipment");

  server.stop();
}

void testNoHealthEndpoint()
{
  virtual_factory::test::RestTestServer server;
  expect(server.start(), "no-health server starts");

  virtual_factory::RestAdapterConfig config =
      oneMachineConfig(server.host(), server.port(), mixerMapping());
  config.healthPath.clear();
  virtual_factory::RestIndustrialAdapter adapter("rest-no-health", config);
  expect(adapter.connect(),
         "connect without a health API uses origin TCP connectivity");
  expect(adapter.connected(), "no-health adapter is Connected");
  adapter.poll();
  virtual_factory::Equipment *mixer = adapter.equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr && telemetryNear(*mixer, "speed", 42.0),
         "mapped JSON GET establishes telemetry after connect without health");

  adapter.disconnect();
  server.stop();
}

void testBasePathAndTlsDefault()
{
  virtual_factory::RestAdapterConfig defaults;
  expect(defaults.tlsVerify,
         "TLS certificate verification is enabled by default");
  expect(defaults.scheme == "http", "default scheme is http");

  virtual_factory::test::RestTestServer server;
  expect(server.start(), "base-path server starts");

  virtual_factory::RestAdapterConfig config =
      oneMachineConfig(server.host(), server.port(), mixerMapping());
  config.basePath = "/gw";
  config.healthPath = "/health";
  virtual_factory::RestIndustrialAdapter adapter("rest-base", config);
  expect(adapter.connect(), "connect uses scheme+host+port+base path");
  adapter.disconnect();
  server.stop();
}

void testAuthDoesNotLogSecrets()
{
  virtual_factory::test::RestTestServer server;
  expect(server.start(), "auth server starts");
  server.requireAuthorization("Bearer test-token-do-not-log");

  virtual_factory::RestAdapterConfig config =
      oneMachineConfig(server.host(), server.port(), mixerMapping());
  config.auth.kind = virtual_factory::RestAuthKind::Bearer;
  config.auth.bearerToken = "wrong-token-secret";
  virtual_factory::RestIndustrialAdapter bad("rest-auth-bad", config);
  expect(!bad.connect(), "wrong bearer token is rejected");
  expect(bad.connectionState() == virtual_factory::ConnectionState::Faulted,
         "auth failure is communication Faulted");
  expect(bad.lastError().find("wrong-token-secret") == std::string::npos,
         "bearer token is not placed in lastError");
  expect(bad.lastError().find("HTTP 401") != std::string::npos,
         "auth failure reports HTTP 401");

  config.auth.bearerToken = "test-token-do-not-log";
  virtual_factory::RestIndustrialAdapter good("rest-auth-good", config);
  expect(good.connect(), "correct bearer token connects");
  expect(good.connected(), "bearer adapter is Connected");
  good.disconnect();

  server.requireAuthorization("Basic cmVzdDpsYWI=");
  virtual_factory::RestAdapterConfig basic =
      oneMachineConfig(server.host(), server.port(), mixerMapping());
  basic.auth.kind = virtual_factory::RestAuthKind::Basic;
  basic.auth.username = "rest";
  basic.auth.password = "lab-secret-password";
  virtual_factory::RestIndustrialAdapter badBasic("rest-basic-bad", basic);
  expect(!badBasic.connect(), "wrong basic password is rejected");
  expect(badBasic.lastError().find("lab-secret-password") == std::string::npos,
         "basic password is not placed in lastError");

  basic.auth.password = "lab";
  virtual_factory::RestIndustrialAdapter goodBasic("rest-basic-good", basic);
  expect(goodBasic.connect(), "correct basic credentials connect");
  goodBasic.disconnect();
  server.stop();
}

void testMultipleOrigins()
{
  virtual_factory::test::RestTestServer serverA;
  virtual_factory::test::RestTestServer serverB;
  expect(serverA.start(), "origin A starts");
  expect(serverB.start(), "origin B starts");
  expect(serverA.port() != serverB.port(),
         "two REST origins have distinct ports");
  if (serverA.port() == 0 || serverB.port() == 0)
  {
    return;
  }

  virtual_factory::RestIndustrialAdapter originA(
      "rest-origin-a",
      oneMachineConfig(serverA.host(), serverA.port(), mixerMapping()));
  virtual_factory::RestIndustrialAdapter originB(
      "rest-origin-b",
      oneMachineConfig(serverB.host(), serverB.port(), pumpMapping()));

  expect(originA.connect(), "origin A connects");
  expect(originB.connect(), "origin B connects");
  expect(originA.connected() && originB.connected(),
         "both REST origins are Connected");

  virtual_factory::IndustrialAdapter *asA = &originA;
  virtual_factory::IndustrialAdapter *asB = &originB;
  expect(asA->equipmentById("TEST-MIXER-001") != nullptr,
         "mixer belongs to origin A");
  expect(asA->equipmentById("Pump_01") == nullptr, "pump is not on origin A");
  expect(asB->equipmentById("Pump_01") != nullptr,
         "pump belongs to origin B");
  expect(asB->equipmentById("TEST-MIXER-001") == nullptr,
         "mixer is not on origin B");

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

  expect(telemetryNear(*mixer, "speed", 42.0), "poll reads mixer from origin A");
  expect(telemetryNear(*pump, "flow_rate", 125.0),
         "poll reads pump from origin B");

  expect(mixer->execute("start").accepted, "command to mixer writes origin A");
  expect(serverA.mixerStartCount() == 1, "mixer start reached origin A");
  expect(serverB.mixerStartCount() == 0, "origin B mixer start unchanged");
  expect(pump->execute("start").accepted, "command to pump writes origin B");
  expect(serverB.pumpStartCount() == 1, "pump start reached origin B");
  expect(!pump->execute("calibrate").accepted,
         "unknown command on origin B is rejected");

  serverA.stop();
  asA->poll();
  asB->poll();
  expect(originA.connectionState() == virtual_factory::ConnectionState::Faulted,
         "origin A is Faulted after its server stops");
  expect(originB.connectionState() == virtual_factory::ConnectionState::Connected,
         "origin B stays Connected when origin A fails");
  expect(pump->execute("stop").accepted,
         "commands still work on the healthy origin");
  expect(!mixer->execute("stop").accepted,
         "commands on the failed origin are rejected");
  expect(!mixer->fault(),
         "origin A comms failure is not a mixer process fault");

  serverB.setPumpFlow(90.0);
  asB->poll();
  expect(telemetryNear(*pump, "flow_rate", 90.0),
         "origin B telemetry still updates while origin A is down");

  expect(serverA.start(), "origin A restarts");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  expect(originA.connect(), "origin A reconnects without touching origin B");
  expect(originA.connected(), "origin A is Connected after reconnect");
  expect(originB.connected(), "origin B remained Connected during A reconnect");
  mixer = asA->equipmentById("TEST-MIXER-001");
  expect(mixer != nullptr, "mixer re-exposed after origin A reconnect");
  asA->poll();
  expect(mixer != nullptr && telemetryNear(*mixer, "speed", 42.0),
         "origin A telemetry returns after reconnect");

  originA.disconnect();
  originB.disconnect();
  serverA.stop();
  serverB.stop();
}

}  // namespace

int main()
{
  std::cout
      << "rest_adapter_test: local HTTP fixture is DEVELOPMENT/INTEGRATION "
         "validation only, not vendor certification."
      << std::endl;

  virtual_factory::test::RestTestServer server;
  expect(server.start(), "test REST HTTP server starts");
  if (server.port() == 0)
  {
    return EXIT_FAILURE;
  }

  virtual_factory::RestIndustrialAdapter adapter(
      "adapter-rest-1", fullConfig(server.host(), server.port()));

  expect(adapter.id() == "adapter-rest-1", "adapter construction id");
  expect(adapter.protocol() == "rest", "protocol is rest");
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
         "mapped mixer running false is Stopped");
  expect(!mixer->fault(), "mapped mixer fault false is not a machine fault");

  expect(mixer->execute("start").accepted, "execute start is accepted");
  expect(server.mixerRunning(), "mixer start POST reached the fixture");
  expect(server.mixerStartCount() == 1, "mixer start count is one");
  expect(server.pumpStartCount() == 0, "pump start not invoked by mixer start");

  adapter.poll();
  expect(mixer->running(), "poll maps JSON running true to equipment Running");
  expect(adapter.connected(),
         "successful command/poll leaves adapter connected");

  expect(mixer->execute("stop").accepted, "execute stop is accepted");
  expect(!server.mixerRunning(), "mixer stop POST reached the fixture");
  adapter.poll();
  expect(!mixer->running(), "poll maps JSON running false to Stopped");

  expect(mixer->execute("set_speed", 1200.0).accepted,
         "execute set_speed writes JSON body");
  expect(near(server.mixerSpeed(), 1200.0),
         "speed PUT body value reached the fixture");
  expect(server.lastBody().find("1200") != std::string::npos,
         "set_speed substituted {{value}} into the JSON body");

  server.setMixerTemperature(82.0);
  adapter.poll();
  expect(telemetryNear(*mixer, "speed", 1200.0),
         "poll updates speed telemetry from JSON");
  expect(telemetryNear(*mixer, "temperature", 82.0),
         "poll updates temperature from the same JSON GET");

  virtual_factory::Equipment *pump = adapter.equipmentById("Pump_01");
  expect(pump != nullptr, "pump still exposed");
  if (pump != nullptr)
  {
    expect(pump->execute("start").accepted, "pump start writes REST PATCH");
    expect(server.pumpRunning(), "Pump_01 start reached pump resource");
    expect(server.mixerStartCount() == 1,
           "pump start did not increment mixer start count");
    expect(!pump->execute("set_speed", 1.0).accepted,
           "unmapped pump command is rejected");
  }

  expect(!mixer->execute("calibrate").accepted,
         "unmapped mixer command is rejected");

  server.setMixerFault(true);
  adapter.poll();
  expect(mixer->fault(), "machine JSON fault maps to Equipment::fault");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Connected,
         "machine fault is not a communication fault");

  server.setMixerFault(false);
  adapter.poll();
  expect(!mixer->fault(), "clearing JSON fault clears equipment fault");

  const std::uint16_t port = server.port();
  server.stop();
  adapter.poll();
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "lost origin is adapter Faulted");
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
  expect(adapter.connect(), "connect after Faulted recreates the HTTP session");
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
  testHttpError();
  testTimeout();
  testNoHealthEndpoint();
  testBasePathAndTlsDefault();
  testAuthDoesNotLogSecrets();
  testMultipleOrigins();

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "rest_adapter_test: all assertions passed" << std::endl;
  return EXIT_SUCCESS;
}
