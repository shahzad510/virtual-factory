/// ICP standalone product acceptance / regression harness.
/// Validates ApplicationService + HTTP API paths beyond unit adapter tests.
#include "modbus_test_server.hh"
#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include <httplib.h>

namespace
{

int failures = 0;

void expect(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

std::string tempPath(const char *suffix)
{
  std::ostringstream stream;
  stream << "/tmp/icp-accept-" << ::getpid() << "-" << suffix;
  return stream.str();
}

int pickPort()
{
  return 19080 + static_cast<int>(::getpid() % 800);
}

virtual_factory::icp::AdapterConfigRecord mockAdapterRecord(const std::string &id)
{
  virtual_factory::icp::AdapterConfigRecord rec;
  rec.adapterId = id;
  rec.protocol = "mock";
  rec.enabled = true;
  rec.description = "acceptance mock";
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "Motor-Accept";
  eq.type = "motor";
  eq.capabilities = {"start", "stop"};
  virtual_factory::icp::TelemetryMappingRecord tel;
  tel.name = "speed";
  tel.unit = "rpm";
  eq.telemetry.push_back(tel);
  virtual_factory::icp::CommandMappingRecord start;
  start.command = "start";
  virtual_factory::icp::CommandMappingRecord stop;
  stop.command = "stop";
  eq.commands.push_back(start);
  eq.commands.push_back(stop);
  rec.equipment.push_back(eq);
  return rec;
}

void testMissingConfigFirstRun()
{
  const std::string path = tempPath("missing.json");
  ::unlink(path.c_str());

  virtual_factory::icp::IcpConfigurationDocument doc;
  virtual_factory::icp::JsonFileConfigurationRepository repo(path);
  const auto loaded = repo.load(&doc);
  expect(loaded.ok, "missing config file is first-run OK: " + loaded.message);
  expect(doc.adapters.empty(), "missing file yields empty adapters");
  expect(
      loaded.message.find("not found") != std::string::npos,
      "missing file message mentions not found");

  virtual_factory::icp::ApplicationService service(path);
  service.start();
  const auto load = service.loadConfiguration();
  expect(load.ok, "ApplicationService load missing file");
  const auto st = service.status();
  expect(st.configurationLoaded, "status.configurationLoaded true");
  expect(
      st.configurationLoadState.find("not found") != std::string::npos,
      "status reports first-run state");
  service.stop();
}

void testMalformedConfigRejected()
{
  const std::string path = tempPath("bad.json");
  {
    std::ofstream out(path);
    out << "{ not json";
  }
  virtual_factory::icp::IcpConfigurationDocument doc;
  virtual_factory::icp::JsonFileConfigurationRepository repo(path);
  const auto loaded = repo.load(&doc);
  expect(!loaded.ok, "malformed JSON rejected on load");

  virtual_factory::icp::ApplicationService service(path);
  service.start();
  const auto load = service.loadConfiguration();
  expect(!load.ok, "ApplicationService reports malformed load failure");
  expect(service.configuration().adapters.empty(), "catalog unchanged on bad load");
  service.stop();
}

void testEmptyValidConfiguration()
{
  const std::string path = tempPath("empty.json");
  virtual_factory::icp::IcpConfigurationDocument doc;
  doc.name = "empty-acceptance";
  virtual_factory::icp::JsonFileConfigurationRepository repo(path);
  expect(repo.save(doc).ok, "save empty valid configuration");
  expect(repo.load(&doc).ok, "load empty valid configuration");
  expect(doc.adapters.empty(), "empty adapters round-trip");
}

void testMockFullStackE2E()
{
  const std::string path = tempPath("mock-e2e.json");
  ::unlink(path.c_str());

  virtual_factory::icp::ApplicationService service(path);
  service.start();
  expect(service.upsertAdapterConfig(mockAdapterRecord("mock-acc")).ok, "upsert mock");
  expect(service.saveConfiguration().ok, "save mock config");
  expect(service.connectAdapter("mock-acc").ok, "connect mock");
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  auto snap = service.equipmentById("Motor-Accept");
  expect(snap.has_value(), "equipment snapshot exists");
  if (snap)
  {
    expect(snap->protocol == "mock", "protocol mock");
    expect(snap->communicationState == virtual_factory::ConnectionState::Connected,
           "connected communication state");
    expect(!snap->stale, "not stale when connected");
    expect(!snap->telemetry.empty(), "telemetry present");
    expect(!snap->machineFault, "no machine fault on healthy mock");
  }

  const auto cmd = service.executeEquipmentCommand("Motor-Accept", "start", 0.0);
  expect(cmd.ok, "start command via ApplicationService: " + cmd.message);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  snap = service.equipmentById("Motor-Accept");
  expect(snap.has_value() && snap->operationalState == virtual_factory::OperationalState::Running,
         "machine state RUNNING after start");

  expect(service.disconnectAdapter("mock-acc").ok, "disconnect mock");
  snap = service.equipmentById("Motor-Accept");
  expect(!snap || snap->stale || snap->communicationState != virtual_factory::ConnectionState::Connected,
         "stale or disconnected after disconnect");

  service.stop();
  virtual_factory::icp::ApplicationService reloaded(path);
  reloaded.start();
  expect(reloaded.loadConfiguration().ok, "reload after restart");
  expect(reloaded.catalog().adapter("mock-acc") != nullptr, "adapter persisted");
  reloaded.stop();
}

void testHttpApiAndSecurity()
{
  const std::string path = tempPath("api.json");
  ::unlink(path.c_str());

  virtual_factory::icp::ApplicationService service(path);
  service.start();
  service.upsertAdapterConfig(mockAdapterRecord("mock-api"));
  service.saveConfiguration();
  service.connectAdapter("mock-api");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const int port = pickPort();
  virtual_factory::icp::HttpApiServer api(
      service, "", "127.0.0.1", port);
  expect(api.start(), "HTTP API start");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(5, 0);

  const auto endpoints = std::vector<std::pair<std::string, std::string>>{
      {"GET", "/api/v1/health"},
      {"GET", "/api/v1/status"},
      {"GET", "/api/v1/protocols"},
      {"GET", "/api/v1/configuration"},
      {"GET", "/api/v1/adapters"},
      {"GET", "/api/v1/equipment"},
      {"GET", "/api/v1/mappings"},
      {"GET", "/api/v1/diagnostics"},
      {"GET", "/api/v1/events"},
      {"GET", "/api/v1/configuration/export"},
  };
  for (const auto &ep : endpoints)
  {
    auto res = ep.first == "GET" ? client.Get(ep.second) : httplib::Result{};
    expect(res && res->status == 200, ep.first + " " + ep.second);
    expect(res->body.find("password") == std::string::npos || res->body.find("passwordRef") != std::string::npos,
           "no plaintext password key in " + ep.second);
    expect(res->body.find("hunter2") == std::string::npos, "no embedded secret in " + ep.second);
  }

  auto bad = client.Put("/api/v1/configuration", "{", "application/json");
  expect(bad && bad->status >= 400, "malformed JSON rejected");

  auto missing = client.Get("/api/v1/adapters/does-not-exist");
  expect(missing && missing->status == 404, "missing adapter 404");

  nlohmann::json cmd = {{"command", "start"}};
  auto cmdRes = client.Post(
      "/api/v1/equipment/Motor-Accept/command", cmd.dump(), "application/json");
  expect(cmdRes && cmdRes->status == 200, "equipment command via HTTP");
  if (cmdRes)
  {
    auto body = nlohmann::json::parse(cmdRes->body);
    expect(body.value("ok", false), "command ok in body");
  }

  api.stop();
  service.stop();
}

void testOpcUaThroughApplicationService()
{
  using virtual_factory::test::OpcUaTestServer;
  OpcUaTestServer server;
  expect(server.start(), "OPC UA test server start");

  const std::string path = tempPath("opcua.json");
  virtual_factory::icp::ApplicationService service(path);
  service.start();

  virtual_factory::icp::AdapterConfigRecord rec;
  rec.adapterId = "opcua-acc";
  rec.protocol = "opcua";
  rec.enabled = true;
  rec.connection.endpointUrl = server.endpointUrl();
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = OpcUaTestServer::kMixerId;
  eq.type = "mixer";
  eq.capabilities = {"start", "stop"};
  virtual_factory::icp::TelemetryMappingRecord speed;
  speed.name = "speed";
  speed.address = OpcUaTestServer::kMixerSpeedActual;
  speed.namespaceIndex = 1;
  speed.unit = "rpm";
  eq.telemetry.push_back(speed);
  virtual_factory::icp::SignalMappingRecord state;
  state.mapped = true;
  state.address = OpcUaTestServer::kMixerRunning;
  state.namespaceIndex = 1;
  eq.state = state;
  rec.equipment.push_back(eq);

  expect(service.upsertAdapterConfig(rec).ok, "upsert OPC UA adapter config");
  expect(service.connectAdapter("opcua-acc").ok, "connect OPC UA via ApplicationService");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  service.manager().adapter("opcua-acc")->poll();
  service.cache().updateFromAdapter(*service.manager().adapter("opcua-acc"));

  auto snap = service.equipmentById(OpcUaTestServer::kMixerId);
  expect(snap.has_value(), "OPC UA equipment in cache");
  if (snap)
  {
    expect(snap->communicationState == virtual_factory::ConnectionState::Connected,
           "OPC UA connected");
    expect(!snap->telemetry.empty(), "OPC UA telemetry from real server");
  }

  expect(service.disconnectAdapter("opcua-acc").ok, "disconnect OPC UA");
  server.stop();
  service.stop();
}

void testModbusThroughApplicationService()
{
  using virtual_factory::test::ModbusTestServer;
  ModbusTestServer server;
  expect(server.start(), "Modbus test server start");

  const std::string path = tempPath("modbus.json");
  virtual_factory::icp::ApplicationService service(path);
  service.start();

  virtual_factory::icp::AdapterConfigRecord rec;
  rec.adapterId = "modbus-acc";
  rec.protocol = "modbus";
  rec.enabled = true;
  rec.connection.host = server.host();
  rec.connection.port = server.port();
  rec.connection.timeoutMs = 2000;
  virtual_factory::icp::EquipmentMappingRecord eq;
  eq.equipmentId = "MODBUS-MIXER";
  eq.type = "mixer";
  virtual_factory::icp::TelemetryMappingRecord speed;
  speed.name = "speed";
  speed.table = "inputRegister";
  speed.registerAddress = ModbusTestServer::kMixerSpeedActual;
  speed.unitId = ModbusTestServer::kUnitId;
  eq.telemetry.push_back(speed);
  rec.equipment.push_back(eq);

  server.setInputRegister(ModbusTestServer::kMixerSpeedActual, 1500);

  expect(service.upsertAdapterConfig(rec).ok, "upsert Modbus config");
  expect(service.connectAdapter("modbus-acc").ok, "connect Modbus via ApplicationService");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  service.manager().adapter("modbus-acc")->poll();
  service.cache().updateFromAdapter(*service.manager().adapter("modbus-acc"));

  auto snap = service.equipmentById("MODBUS-MIXER");
  expect(snap.has_value() && !snap->telemetry.empty(), "Modbus telemetry in cache");
  if (snap && !snap->telemetry.empty())
  {
    expect(snap->telemetry.front().value > 0.0, "Modbus read non-zero from real server");
  }

  server.stop();
  service.stop();
}

void testProfinetProfibusSoftwareBoundary()
{
  const std::string path = tempPath("fieldbus.json");
  virtual_factory::icp::ApplicationService service(path);
  service.start();

  virtual_factory::icp::AdapterConfigRecord pn;
  pn.adapterId = "pn-acc";
  pn.protocol = "profinet";
  pn.connection.boardId = "cifx0";
  pn.connection.interfaceName = "eth0";
  pn.connection.processImageBytes = 64;
  virtual_factory::icp::EquipmentMappingRecord pnEq;
  pnEq.equipmentId = "PN-IO";
  pnEq.type = "io_device";
  pnEq.stationName = "dev-1";
  virtual_factory::icp::ProfinetSubmoduleRecord sub;
  sub.slot = 0;
  sub.subslot = 1;
  sub.inputLength = 8;
  sub.outputLength = 8;
  pnEq.submodules.push_back(sub);
  pn.equipment.push_back(pnEq);
  expect(service.upsertAdapterConfig(pn).ok, "PROFINET config without hardware");
  expect(service.validateConfiguration().ok, "PROFINET validates without hardware");

  const auto pnConnect = service.connectAdapter("pn-acc");
  expect(!pnConnect.ok, "PROFINET connect fails without hardware");
  if (!pnConnect.ok)
  {
    expect(
        pnConnect.message.find("Hilscher") != std::string::npos
            || pnConnect.message.find("hardware") != std::string::npos
            || pnConnect.message.find("artifact") != std::string::npos
            || pnConnect.message.find("SDK") != std::string::npos,
        "PROFINET failure message is honest: " + pnConnect.message);
  }

  const auto views = service.adapters();
  for (const auto &view : views)
  {
    if (view.adapterId == "pn-acc")
    {
      expect(view.connectionState != "CONNECTED", "PROFINET never falsely CONNECTED");
    }
  }

  const auto diag = service.hilscherDiagnostics();
  expect(
      diag.boardCount == 0 || diag.readinessState.find("NO") != std::string::npos
          || diag.readinessState == "SDK_MISSING",
      "Hilscher diagnostics honest without hardware card");

  service.stop();
}

void testCommunicationVsMachineFault()
{
  const std::string path = tempPath("fault.json");
  virtual_factory::icp::ApplicationService service(path);
  service.start();
  service.upsertAdapterConfig(mockAdapterRecord("mock-fault"));
  service.connectAdapter("mock-fault");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto *runtime = dynamic_cast<virtual_factory::MockIndustrialAdapter *>(
      service.manager().adapter("mock-fault"));
  expect(runtime != nullptr, "mock runtime present");
  if (runtime)
  {
    runtime->simulateCommunicationFailure("simulated comm loss");
    service.cache().markAdapterCommunication(
        "mock-fault",
        runtime->connectionState(),
        runtime->lastError());
    auto snap = service.equipmentById("Motor-Accept");
    expect(snap.has_value(), "snapshot after comm failure");
    if (snap)
    {
      expect(snap->stale || snap->communicationState != virtual_factory::ConnectionState::Connected,
             "communication fault marks stale/disconnected");
      // Machine fault should not be auto-invented by comm loss alone.
      expect(!snap->machineFault, "comm failure does not invent machine fault");
    }
  }
  service.stop();
}

void testMockConnectionDisplaySemantics()
{
  const std::string path = tempPath("mock-display.json");
  virtual_factory::icp::ApplicationService service(path);
  service.start();
  service.upsertAdapterConfig(mockAdapterRecord("mock-display"));
  service.connectAdapter("mock-display");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  for (const virtual_factory::icp::RuntimeAdapterView &view : service.adapters())
  {
    if (view.adapterId == "mock-display")
    {
      expect(view.connectionState == "CONNECTED", "canonical mock state remains CONNECTED");
      expect(
          view.connectionStateDisplay == "SIMULATED_ACTIVE",
          "mock display label is SIMULATED_ACTIVE");
    }
  }
  service.stop();
}

void testAdapterImplementationClassification()
{
  virtual_factory::icp::AdapterConfigRecord mock;
  mock.protocol = "mock";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(mock) == "simulated",
      "mock implementation is simulated");

  virtual_factory::icp::AdapterConfigRecord opcua;
  opcua.protocol = "opcua";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(opcua) == "gateway",
      "opcua implementation is gateway");

  virtual_factory::icp::AdapterConfigRecord pnNative;
  pnNative.protocol = "profinet";
  pnNative.connection.boardId = "cifx0";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(pnNative)
          == "hilscher_native",
      "profinet with boardId is hilscher_native");

  virtual_factory::icp::AdapterConfigRecord pbNative;
  pbNative.protocol = "profibus";
  pbNative.connection.boardId = "cifx0";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(pbNative)
          == "hilscher_native",
      "profibus with boardId is hilscher_native");

  virtual_factory::icp::AdapterConfigRecord pnGateway;
  pnGateway.protocol = "profinet";
  pnGateway.implementation = "gateway";
  pnGateway.connection.endpointUrl = "opc.tcp://127.0.0.1:4840";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(pnGateway) == "gateway",
      "profinet with gateway implementation is gateway");

  virtual_factory::icp::AdapterConfigRecord pbGateway;
  pbGateway.protocol = "profibus";
  pbGateway.implementation = "gateway";
  pbGateway.connection.host = "127.0.0.1";
  expect(
      virtual_factory::icp::ApplicationService::adapterImplementation(pbGateway) == "gateway",
      "profibus with gateway implementation is gateway");
}

void testMultiProtocolAdapterCoexistence()
{
  const std::string path = tempPath("multi-proto.json");
  ::unlink(path.c_str());

  virtual_factory::icp::ApplicationService service(path);
  service.start();

  const std::vector<std::pair<std::string, virtual_factory::icp::AdapterConfigRecord>>
      adapters = {
          {"mock", mockAdapterRecord("mock-multi")},
          {"opcua",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "opcua-multi";
             rec.protocol = "opcua";
             rec.connection.endpointUrl = "opc.tcp://127.0.0.1:4840";
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "UA-1";
             eq.type = "device";
             virtual_factory::icp::TelemetryMappingRecord tel;
             tel.name = "speed";
             tel.address = "ns=1;s=Speed";
             tel.namespaceIndex = 1;
             eq.telemetry.push_back(tel);
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"modbus",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "modbus-multi";
             rec.protocol = "modbus";
             rec.connection.host = "127.0.0.1";
             rec.connection.port = 502;
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "MB-1";
             eq.type = "device";
             virtual_factory::icp::TelemetryMappingRecord tel;
             tel.name = "reg";
             tel.table = "holdingRegister";
             tel.registerAddress = 1;
             eq.telemetry.push_back(tel);
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"mqtt",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "mqtt-multi";
             rec.protocol = "mqtt";
             rec.connection.host = "127.0.0.1";
             rec.connection.port = 1883;
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "MQ-1";
             eq.type = "device";
             virtual_factory::icp::TelemetryMappingRecord tel;
             tel.name = "t";
             tel.address = "plant/telemetry";
             eq.telemetry.push_back(tel);
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"rest",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "rest-multi";
             rec.protocol = "rest";
             rec.connection.scheme = "http";
             rec.connection.host = "127.0.0.1";
             rec.connection.port = 8081;
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "REST-1";
             eq.type = "device";
             eq.telemetryPath = "/api/device";
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"ethernetip",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "eip-multi";
             rec.protocol = "ethernetip";
             rec.connection.host = "127.0.0.1";
             rec.connection.port = 44818;
             rec.connection.path = "1,0";
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "EIP-1";
             eq.type = "device";
             virtual_factory::icp::TelemetryMappingRecord tel;
             tel.name = "tag";
             tel.address = "Program:Main.MyTag";
             eq.telemetry.push_back(tel);
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"profinet",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "pn-multi";
             rec.protocol = "profinet";
             rec.connection.boardId = "cifx0";
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "PN-1";
             eq.type = "io_device";
             eq.stationName = "dev";
             virtual_factory::icp::ProfinetSubmoduleRecord sub;
             sub.slot = 0;
             sub.subslot = 1;
             sub.inputLength = 8;
             sub.outputLength = 8;
             eq.submodules.push_back(sub);
             rec.equipment.push_back(eq);
             return rec;
           }()},
          {"profibus",
           [] {
             virtual_factory::icp::AdapterConfigRecord rec;
             rec.adapterId = "pb-multi";
             rec.protocol = "profibus";
             rec.connection.boardId = "cifx0";
             rec.connection.baudRateKbps = 1500;
             virtual_factory::icp::EquipmentMappingRecord eq;
             eq.equipmentId = "PB-1";
             eq.type = "dp_slave";
             eq.stationAddress = 3;
             virtual_factory::icp::ProfibusModuleRecord mod;
             mod.slot = 0;
             mod.ident = "m0";
             mod.inputLength = 8;
             mod.outputLength = 8;
             eq.modules.push_back(mod);
             rec.equipment.push_back(eq);
             return rec;
           }()},
      };

  for (const auto &entry : adapters)
  {
    const auto upsert = service.upsertAdapterConfig(entry.second);
    expect(upsert.ok, "upsert " + entry.first + ": " + upsert.message);
  }
  expect(service.validateConfiguration().ok, "validate multi-protocol catalog");
  expect(service.saveConfiguration().ok, "save multi-protocol catalog");

  expect(service.catalog().document().adapters.size() == adapters.size(), "all adapters in catalog");

  virtual_factory::icp::ApplicationService reloaded(path);
  expect(reloaded.loadConfiguration().ok, "reload multi-protocol");
  for (const auto &entry : adapters)
  {
    const virtual_factory::icp::AdapterConfigRecord *rec =
        reloaded.catalog().adapter(entry.second.adapterId);
    expect(rec != nullptr, "persisted adapter " + entry.first);
    if (rec)
    {
      expect(rec->protocol == entry.first, "protocol preserved for " + entry.first);
    }
  }

  virtual_factory::icp::ConfigResult removed = reloaded.removeAdapterConfig("mqtt-multi");
  expect(removed.ok, "remove one adapter");
  expect(reloaded.catalog().adapter("mqtt-multi") == nullptr, "mqtt removed");
  expect(reloaded.catalog().adapter("modbus-multi") != nullptr, "modbus remains");
  service.stop();
}

}  // namespace

int main()
{
  testMissingConfigFirstRun();
  testMalformedConfigRejected();
  testEmptyValidConfiguration();
  testMockFullStackE2E();
  testHttpApiAndSecurity();
  testOpcUaThroughApplicationService();
  testModbusThroughApplicationService();
  testProfinetProfibusSoftwareBoundary();
  testCommunicationVsMachineFault();
  testMockConnectionDisplaySemantics();
  testAdapterImplementationClassification();
  testMultiProtocolAdapterCoexistence();

  if (failures == 0)
  {
    std::cout << "icp_standalone_acceptance_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_standalone_acceptance_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
