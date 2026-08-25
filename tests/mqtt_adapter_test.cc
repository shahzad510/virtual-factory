#include "mqtt_test_broker.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MqttIndustrialAdapter.hh>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{

int failures = 0;
std::uint64_t helperSerial = 1;

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

std::string nextHelperId(const char *prefix)
{
  return std::string(prefix) + "-" + std::to_string(helperSerial++);
}

virtual_factory::MqttEquipmentMapping plc001Mapping()
{
  virtual_factory::MqttEquipmentMapping plc;
  plc.id = "PLC-001";
  plc.type = "plc";
  plc.capabilities = {"start", "stop", "set_speed"};
  plc.commands = {
      {"start", "machine/001/cmd/start", "{\"command\":\"start\"}", 1, false},
      {"stop", "machine/001/cmd/stop", "{\"command\":\"stop\"}", 1, false},
      {"set_speed", "machine/001/cmd/speed", "{\"value\":{{value}}}", 1, false},
  };
  plc.telemetry = {
      {"temperature", "machine/001/telemetry", "degC",
       virtual_factory::MqttPayloadEncoding::JsonPointer, "/temperature", 1},
      {"pressure", "machine/001/telemetry", "bar",
       virtual_factory::MqttPayloadEncoding::JsonPointer, "/pressure", 1},
      {"speed", "machine/001/telemetry", "rpm",
       virtual_factory::MqttPayloadEncoding::JsonPointer, "/speed", 1},
      {"production_count", "machine/001/telemetry", "count",
       virtual_factory::MqttPayloadEncoding::JsonPointer, "/production_count",
       1},
  };
  plc.state = virtual_factory::makeMqttSignal(
      "machine/001/state", virtual_factory::MqttPayloadEncoding::BooleanText);
  plc.fault = virtual_factory::makeMqttSignal(
      "machine/001/fault", virtual_factory::MqttPayloadEncoding::BooleanText);
  return plc;
}

virtual_factory::MqttEquipmentMapping plc002Mapping()
{
  virtual_factory::MqttEquipmentMapping plc;
  plc.id = "PLC-002";
  plc.type = "plc";
  plc.capabilities = {"start", "stop"};
  plc.commands = {
      {"start", "machine/002/cmd/start", "1", 1, false},
      {"stop", "machine/002/cmd/stop", "0", 1, false},
  };
  plc.telemetry = {
      {"flow_rate", "machine/002/flow", "L/min",
       virtual_factory::MqttPayloadEncoding::NumberText, "", 1},
  };
  plc.state = virtual_factory::makeMqttSignal("machine/002/state");
  plc.fault = virtual_factory::makeMqttSignal("machine/002/fault");
  return plc;
}

virtual_factory::MqttEquipmentMapping unknownMapping()
{
  virtual_factory::MqttEquipmentMapping unknown;
  unknown.id = "UnknownMachine_01";
  unknown.type = "special_processing_machine";
  unknown.capabilities = {"start"};
  unknown.commands = {
      {"start", "machine/unknown/cmd/start", "{}", 1, false},
  };
  unknown.telemetry = {
      {"temperature", "machine/unknown/temp", "degC",
       virtual_factory::MqttPayloadEncoding::NumberText, "", 1},
  };
  return unknown;
}

virtual_factory::MqttAdapterConfig brokerConfig(
    const std::string &host,
    std::uint16_t port,
    std::vector<virtual_factory::MqttEquipmentMapping> equipment)
{
  virtual_factory::MqttAdapterConfig config;
  config.host = host;
  config.port = port;
  config.timeoutMs = 2000;
  config.pollTimeoutMs = 50;
  config.keepaliveSeconds = 5;
  config.tlsVerify = true;
  config.equipment = std::move(equipment);
  return config;
}

bool waitTelemetry(
    virtual_factory::MqttIndustrialAdapter &adapter,
    const std::string &equipmentId,
    const std::string &name,
    double expected)
{
  for (int i = 0; i < 25; ++i)
  {
    adapter.poll();
    virtual_factory::Equipment *equipment = adapter.equipmentById(equipmentId);
    if (equipment != nullptr && telemetryNear(*equipment, name, expected))
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  return false;
}

bool waitState(
    virtual_factory::MqttIndustrialAdapter &adapter,
    const std::string &equipmentId,
    virtual_factory::OperationalState expected)
{
  for (int i = 0; i < 25; ++i)
  {
    adapter.poll();
    virtual_factory::Equipment *equipment = adapter.equipmentById(equipmentId);
    if (equipment != nullptr && equipment->operationalState() == expected)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  return false;
}

void testConstruction()
{
  virtual_factory::MqttAdapterConfig defaults;
  expect(defaults.port == 1883, "default MQTT port is 1883");
  expect(defaults.pollTimeoutMs == 50, "default poll timeout is 50 ms");
  expect(defaults.tlsVerify, "TLS certificate verification is on by default");
  expect(!defaults.useTls, "TLS is off until configured");

  virtual_factory::MqttIndustrialAdapter adapter(
      "mqtt-construct", brokerConfig("127.0.0.1", 1883, {plc001Mapping()}));
  expect(adapter.id() == "mqtt-construct", "adapter id is stored");
  expect(adapter.protocol() == "mqtt", "protocol() is mqtt");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "constructed adapter is Disconnected");
  expect(adapter.equipment().empty(), "no equipment before connect");
}

void testConnectDisconnectTelemetryCommands()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "anonymous broker starts");
  if (broker.port() == 0)
  {
    return;
  }

  virtual_factory::MqttIndustrialAdapter adapter(
      "mqtt-main",
      brokerConfig(
          broker.host(), broker.port(),
          {plc001Mapping(), plc002Mapping(), unknownMapping()}));
  expect(adapter.connect(), "connect to Mosquitto succeeds");
  expect(adapter.connected(), "adapter is Connected");
  if (!adapter.connected())
  {
    broker.stop();
    return;
  }
  expect(!adapter.clientId().empty(), "client id is assigned");
  expect(adapter.protocol() == "mqtt", "connected protocol remains mqtt");

  virtual_factory::IndustrialAdapter *asAdapter = &adapter;
  expect(asAdapter->protocol() == "mqtt", "IndustrialAdapter* sees mqtt");
  expect(asAdapter->equipment().size() == 3,
         "one broker maps three GenericEquipment instances");

  virtual_factory::Equipment *plc001 = asAdapter->equipmentById("PLC-001");
  virtual_factory::Equipment *plc002 = asAdapter->equipmentById("PLC-002");
  virtual_factory::Equipment *unknown =
      asAdapter->equipmentById("UnknownMachine_01");
  expect(plc001 != nullptr && plc001->type() == "plc",
         "PLC-001 is GenericEquipment identity, not a C++ class");
  expect(plc002 != nullptr && plc002->id() == "PLC-002",
         "PLC-002 is a second mapped instance on the same broker");
  expect(unknown != nullptr && unknown->type() == "special_processing_machine",
         "unknown machine type is metadata");
  expect(asAdapter->equipmentById("DOES-NOT-EXIST") == nullptr,
         "unknown equipment id is not invented");

  virtual_factory::test::MqttTestClient injector;
  expect(injector.connect(
             broker.host(), broker.port(), nextHelperId("inj")),
         "telemetry injector connects");
  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":21.5,\"pressure\":1.25,\"speed\":1000,"
             "\"production_count\":7}"),
         "JSON telemetry publishes");
  expect(injector.publish("machine/001/state", "running"),
         "boolean state publishes");
  expect(injector.publish("machine/002/flow", "125.5"),
         "numeric telemetry publishes");
  expect(injector.publish("machine/unknown/temp", "72"),
         "unknown-machine numeric telemetry publishes");

  expect(waitTelemetry(adapter, "PLC-001", "temperature", 21.5),
         "JSON temperature maps into GenericEquipment");
  expect(waitTelemetry(adapter, "PLC-001", "pressure", 1.25),
         "JSON pressure maps from the same payload");
  expect(waitTelemetry(adapter, "PLC-001", "speed", 1000.0),
         "JSON speed maps from the same payload");
  expect(waitTelemetry(adapter, "PLC-001", "production_count", 7.0),
         "production_count is ordinary latest-value telemetry");
  expect(waitState(
             adapter, "PLC-001", virtual_factory::OperationalState::Running),
         "boolean/state telemetry maps running");
  expect(waitTelemetry(adapter, "PLC-002", "flow_rate", 125.5),
         "numeric telemetry maps independently");
  expect(waitTelemetry(adapter, "UnknownMachine_01", "temperature", 72.0),
         "unknown machine telemetry maps");

  if (plc001 != nullptr)
  {
    expect(telemetryNear(*plc001, "temperature", 21.5, "degC"),
           "Equipment* telemetry includes unit");
    expect(!plc001->fault(), "machine fault is independent of communication");
  }

  virtual_factory::test::MqttTestClient capture;
  expect(capture.connect(
             broker.host(), broker.port(), nextHelperId("cap")),
         "command capture client connects");
  expect(capture.subscribe("machine/001/cmd/start"), "subscribe start");
  expect(capture.subscribe("machine/001/cmd/speed"), "subscribe speed");
  expect(capture.subscribe("machine/002/cmd/start"), "subscribe plc002 start");

  if (plc001 == nullptr)
  {
    capture.disconnect();
    injector.disconnect();
    adapter.disconnect();
    broker.stop();
    return;
  }

  const auto start = plc001->execute("start", 1);
  expect(start.accepted, "mapped start command publishes");
  std::string topic;
  std::string payload;
  expect(capture.waitMessage(&topic, &payload, 1500),
         "start command is observed");
  expect(topic == "machine/001/cmd/start", "start uses mapped topic");
  expect(payload.find("start") != std::string::npos,
         "start payload comes from mapping");

  const auto speed = plc001->execute("set_speed", 1000);
  expect(speed.accepted, "mapped set_speed publishes");
  expect(capture.waitMessage(&topic, &payload, 1500),
         "speed command is observed");
  expect(topic == "machine/001/cmd/speed", "speed uses mapped topic");
  expect(payload.find("1000") != std::string::npos,
         "{{value}} is substituted");

  const auto isolated = plc001->execute("start", 1);
  expect(isolated.accepted, "second start still publishes");
  expect(capture.waitMessage(&topic, &payload, 1500),
         "second start observed");
  expect(topic == "machine/001/cmd/start",
         "PLC-001 command does not use PLC-002 topic");

  const auto unknownCmd = plc001->execute("not-a-command", 0);
  expect(!unknownCmd.accepted, "unknown command is rejected");
  expect(adapter.connected(), "unknown command does not fault the broker");

  expect(injector.publish("machine/999/telemetry", "{\"temperature\":9}"),
         "unmapped topic publishes");
  adapter.poll();
  expect(telemetryNear(*plc001, "temperature", 21.5),
         "unknown topic does not overwrite mapped telemetry");

  expect(injector.publish("machine/001/telemetry", "NOT-JSON{"),
         "malformed payload publishes");
  adapter.poll();
  expect(adapter.connected(),
         "malformed telemetry does not fault the broker session");
  expect(telemetryNear(*plc001, "temperature", 21.5),
         "malformed payload is ignored");

  expect(injector.publish("machine/002/flow", "200"),
         "plc002 numeric update");
  expect(waitTelemetry(adapter, "PLC-002", "flow_rate", 200.0),
         "telemetry isolation: PLC-002 updates independently");
  expect(telemetryNear(*plc001, "speed", 1000.0),
         "telemetry isolation: PLC-001 unchanged");

  adapter.disconnect();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "disconnect returns Disconnected");
  expect(adapter.equipment().empty(), "disconnect clears equipment listing");
  capture.disconnect();
  injector.disconnect();
  broker.stop();
}

void testTwoAdaptersSameBroker()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "shared broker starts");

  auto configA =
      brokerConfig(broker.host(), broker.port(), {plc001Mapping()});
  auto configB =
      brokerConfig(broker.host(), broker.port(), {plc002Mapping()});
  configA.timeoutMs = 5000;
  configB.timeoutMs = 5000;
  virtual_factory::MqttIndustrialAdapter adapterA("mqtt-same-a", configA);
  virtual_factory::MqttIndustrialAdapter adapterB("mqtt-same-b", configB);
  const bool okA = adapterA.connect();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const bool okB = adapterB.connect();
  expect(okA && okB, "two adapters connect to the same broker");
  expect(adapterA.clientId() != adapterB.clientId(),
         "generated client ids are unique on one broker");

  if (!okA || !okB)
  {
    adapterA.disconnect();
    adapterB.disconnect();
    broker.stop();
    return;
  }

  virtual_factory::test::MqttTestClient injector;
  expect(injector.connect(
             broker.host(), broker.port(), nextHelperId("inj-same")),
         "shared-broker injector connects");
  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":11,\"pressure\":1,\"speed\":2,"
             "\"production_count\":3}"),
         "publish plc001");
  expect(injector.publish("machine/002/flow", "33"), "publish plc002");
  expect(waitTelemetry(adapterA, "PLC-001", "temperature", 11.0),
         "adapter A receives its mapping");
  expect(waitTelemetry(adapterB, "PLC-002", "flow_rate", 33.0),
         "adapter B receives its mapping");
  expect(adapterA.equipmentById("PLC-002") == nullptr,
         "adapter A does not own PLC-002");
  expect(adapterB.equipmentById("PLC-001") == nullptr,
         "adapter B does not own PLC-001");

  adapterA.disconnect();
  adapterB.disconnect();
  injector.disconnect();
  broker.stop();
}

void testDuplicateClientId()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "duplicate-id broker starts");

  auto configA =
      brokerConfig(broker.host(), broker.port(), {plc001Mapping()});
  configA.clientId = "vf-fixed-client";
  auto configB = configA;
  virtual_factory::MqttIndustrialAdapter adapterA("mqtt-dup-a", configA);
  virtual_factory::MqttIndustrialAdapter adapterB("mqtt-dup-b", configB);
  expect(adapterA.connect(), "first explicit client id connects");
  expect(!adapterB.connect(), "duplicate client id is rejected");
  expect(adapterB.connectionState() == virtual_factory::ConnectionState::Faulted,
         "duplicate client id is Faulted");
  expect(adapterA.connected(), "first adapter stays Connected");
  adapterA.disconnect();
  adapterB.disconnect();
  broker.stop();
}

void testTwoBrokersIsolation()
{
  virtual_factory::test::MqttTestBroker brokerA;
  virtual_factory::test::MqttTestBroker brokerB;
  expect(brokerA.start() && brokerB.start(), "two brokers start");
  expect(brokerA.port() != brokerB.port(), "brokers use distinct ports");

  auto configA =
      brokerConfig(brokerA.host(), brokerA.port(), {plc001Mapping()});
  configA.keepaliveSeconds = 1;
  virtual_factory::MqttIndustrialAdapter adapterA("mqtt-broker-a", configA);
  virtual_factory::MqttIndustrialAdapter adapterB(
      "mqtt-broker-b",
      brokerConfig(brokerB.host(), brokerB.port(), {plc002Mapping()}));
  expect(adapterA.connect() && adapterB.connect(),
         "each adapter connects to its own broker");

  virtual_factory::test::MqttTestClient injA;
  virtual_factory::test::MqttTestClient injB;
  expect(injA.connect(brokerA.host(), brokerA.port(), nextHelperId("inj-a")),
         "injector A connects");
  expect(injB.connect(brokerB.host(), brokerB.port(), nextHelperId("inj-b")),
         "injector B connects");
  expect(injA.publish(
             "machine/001/telemetry",
             "{\"temperature\":5,\"pressure\":1,\"speed\":2,"
             "\"production_count\":1}"),
         "A telemetry");
  expect(injB.publish("machine/002/flow", "9"), "B telemetry");
  expect(waitTelemetry(adapterA, "PLC-001", "temperature", 5.0),
         "broker A telemetry");
  expect(waitTelemetry(adapterB, "PLC-002", "flow_rate", 9.0),
         "broker B telemetry");

  brokerA.stop();
  adapterA.poll();
  for (int i = 0; i < 40 && adapterA.connected(); ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    adapterA.poll();
  }
  expect(adapterA.connectionState() == virtual_factory::ConnectionState::Faulted,
         "broker A failure is Faulted");
  expect(!adapterA.lastError().empty(), "broker A failure has lastError");
  virtual_factory::Equipment *plc001 = adapterA.equipmentById("PLC-001");
  expect(plc001 != nullptr, "equipment remains listed while Faulted");
  expect(plc001 != nullptr && !plc001->fault(),
         "communication failure is not Equipment::fault()");

  expect(adapterB.connected(),
         "broker A failure does not fault broker B");
  adapterB.poll();
  expect(adapterB.connected(), "broker B still Connected after poll");
  expect(telemetryNear(*adapterB.equipmentById("PLC-002"), "flow_rate", 9.0),
         "broker B telemetry remains");

  injA.disconnect();
  injB.disconnect();
  adapterA.disconnect();
  adapterB.disconnect();
  brokerB.stop();
}

void testReconnectAndRetainQos()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "reconnect broker starts");

  auto config =
      brokerConfig(broker.host(), broker.port(), {plc001Mapping()});
  config.keepaliveSeconds = 1;
  config.equipment[0].telemetry[0].qos = 0;
  config.equipment[0].telemetry[1].qos = 1;
  config.equipment[0].telemetry[2].qos = 2;
  virtual_factory::MqttIndustrialAdapter adapter("mqtt-reconnect", config);
  expect(adapter.connect(), "initial connect");

  virtual_factory::test::MqttTestClient injector;
  expect(injector.connect(
             broker.host(), broker.port(), nextHelperId("inj-re")),
         "reconnect injector");
  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":1,\"pressure\":2,\"speed\":3,"
             "\"production_count\":4}",
             1, false),
         "qos1 telemetry");
  expect(waitTelemetry(adapter, "PLC-001", "temperature", 1.0),
         "qos 0/1/2 subscriptions accept telemetry");

  broker.stop();
  for (int i = 0; i < 40 && adapter.connected(); ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    adapter.poll();
  }
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "broker stop is Faulted");
  virtual_factory::Equipment *plc = adapter.equipmentById("PLC-001");
  expect(plc != nullptr && !plc->fault(),
         "Faulted session does not set machine fault");

  expect(broker.start(), "broker restarts");
  expect(injector.connect(
             broker.host(), broker.port(), nextHelperId("inj-re2")),
         "injector reconnects");
  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":8,\"pressure\":2,\"speed\":3,"
             "\"production_count\":4}",
             1, true),
         "retained telemetry published before adapter reconnect");
  expect(adapter.connect(), "explicit reconnect after Faulted");
  expect(adapter.connected(), "reconnect is Connected");
  expect(waitTelemetry(adapter, "PLC-001", "temperature", 8.0),
         "subscriptions restored; retained telemetry delivered");

  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":9,\"pressure\":2,\"speed\":3,"
             "\"production_count\":4}",
             0, false),
         "qos 0 publish");
  expect(waitTelemetry(adapter, "PLC-001", "temperature", 9.0),
         "qos 0 telemetry is accepted");
  expect(injector.publish(
             "machine/001/telemetry",
             "{\"temperature\":10,\"pressure\":2,\"speed\":3,"
             "\"production_count\":4}",
             2, false),
         "qos 2 publish");
  expect(waitTelemetry(adapter, "PLC-001", "temperature", 10.0),
         "qos 2 telemetry is accepted where practical");

  adapter.disconnect();
  injector.disconnect();
  broker.stop();
}

void testBoundedPoll()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "bounded-poll broker starts");
  auto config =
      brokerConfig(broker.host(), broker.port(), {plc001Mapping()});
  config.pollTimeoutMs = 50;
  virtual_factory::MqttIndustrialAdapter adapter("mqtt-poll", config);
  expect(adapter.connect(), "bounded poll adapter connects");

  const auto start = std::chrono::steady_clock::now();
  adapter.poll();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  expect(elapsed < 400, "poll() is bounded (default 50 ms receive timeout)");

  adapter.disconnect();
  broker.stop();
}

void testAuthAndTls()
{
  virtual_factory::MqttAdapterConfig defaults;
  expect(defaults.tlsVerify, "tlsVerify defaults to true");

  virtual_factory::test::MqttTestBroker plain;
  expect(plain.start(), "plain broker for TLS-mismatch starts");
  auto tlsMismatch =
      brokerConfig(plain.host(), plain.port(), {plc001Mapping()});
  tlsMismatch.useTls = true;
  tlsMismatch.tlsVerify = true;
  virtual_factory::MqttIndustrialAdapter tlsPlain("mqtt-tls-plain", tlsMismatch);
  expect(!tlsPlain.connect(), "TLS client does not connect to plaintext broker");
  expect(tlsPlain.connectionState() == virtual_factory::ConnectionState::Faulted,
         "TLS mismatch is Faulted");
  tlsPlain.disconnect();
  plain.stop();

  virtual_factory::test::MqttTestBrokerOptions authOpts;
  authOpts.requirePassword = true;
  authOpts.username = "factory";
  authOpts.password = "mqtt-lab-secret";
  virtual_factory::test::MqttTestBroker auth(authOpts);
  expect(auth.start(), "password broker starts");

  auto badCfg = brokerConfig(auth.host(), auth.port(), {plc001Mapping()});
  badCfg.username = "factory";
  badCfg.password = "wrong-secret-do-not-log";
  virtual_factory::MqttIndustrialAdapter bad("mqtt-auth-bad", badCfg);
  expect(!bad.connect(), "wrong MQTT password is rejected");
  expect(bad.lastError().find("wrong-secret-do-not-log") == std::string::npos,
         "password is not placed in lastError");

  auto goodCfg = badCfg;
  goodCfg.password = "mqtt-lab-secret";
  virtual_factory::MqttIndustrialAdapter good("mqtt-auth-good", goodCfg);
  expect(good.connect(), "username/password connects");
  good.disconnect();
  bad.disconnect();
  auth.stop();

  virtual_factory::test::MqttTestBrokerOptions tlsOpts;
  tlsOpts.tls = true;
  virtual_factory::test::MqttTestBroker tlsBroker(tlsOpts);
  if (!tlsBroker.start())
  {
    std::cerr << "INFO: TLS broker skipped: " << tlsBroker.lastError()
              << std::endl;
    return;
  }

  auto verifyOn =
      brokerConfig(tlsBroker.host(), tlsBroker.port(), {plc001Mapping()});
  verifyOn.useTls = true;
  verifyOn.tlsVerify = true;
  virtual_factory::MqttIndustrialAdapter strict("mqtt-tls-strict", verifyOn);
  expect(!strict.connect(),
         "self-signed broker is rejected when tlsVerify is true");
  strict.disconnect();

  auto insecure = verifyOn;
  insecure.tlsVerify = false;
  virtual_factory::MqttIndustrialAdapter lab("mqtt-tls-insecure", insecure);
  expect(lab.connect(),
         "insecure TLS is an explicit development/testing opt-in");
  lab.disconnect();
  tlsBroker.stop();
}

void testWildcardRejected()
{
  virtual_factory::test::MqttTestBroker broker;
  expect(broker.start(), "wildcard broker starts");
  auto mapping = plc001Mapping();
  mapping.telemetry[0].topic = "machine/+/telemetry";
  virtual_factory::MqttIndustrialAdapter adapter(
      "mqtt-wild", brokerConfig(broker.host(), broker.port(), {mapping}));
  expect(!adapter.connect(), "wildcard subscription is rejected in 6F");
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "wildcard mapping is Faulted");
  adapter.disconnect();
  broker.stop();
}

void testConnectFailure()
{
  virtual_factory::MqttIndustrialAdapter adapter(
      "mqtt-down", brokerConfig("127.0.0.1", 1, {plc001Mapping()}));
  expect(!adapter.connect(), "connect to closed port fails");
  expect(adapter.connectionState() == virtual_factory::ConnectionState::Faulted,
         "failed connect is Faulted");
  expect(adapter.equipment().empty(),
         "no equipment if connect never succeeded");
}

}  // namespace

int main()
{
  std::cout << "mqtt_adapter_test: DEVELOPMENT/INTEGRATION VALIDATION ONLY\n";
  testConstruction();
  testConnectDisconnectTelemetryCommands();
  testTwoAdaptersSameBroker();
  testDuplicateClientId();
  testTwoBrokersIsolation();
  testReconnectAndRetainQos();
  testBoundedPoll();
  testAuthAndTls();
  testWildcardRejected();
  testConnectFailure();

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "mqtt_adapter_test: all assertions passed\n";
  return 0;
}
