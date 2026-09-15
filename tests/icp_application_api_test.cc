#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>
#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>

#include <chrono>
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


bool waitForAdapterState(
    ApplicationService &service,
    const std::string &adapterId,
    const std::string &state,
    int timeoutMs = 8000)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto view = service.adapter(adapterId);
    if (view && view->connectionState == state)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

std::string tempConfigPath()
{
  std::ostringstream stream;
  stream << "/tmp/icp-gui-e2e-" << ::getpid() << ".json";
  return stream.str();
}

int freePort()
{
  return 18080 + (::getpid() % 1000);
}

}  // namespace

int main()
{
  using json = nlohmann::json;
  using virtual_factory::icp::ApplicationService;
  using virtual_factory::icp::EquipmentCommandResult;
  using virtual_factory::icp::HttpApiServer;

  const std::string configPath = tempConfigPath();
  ::unlink(configPath.c_str());

  // --- ApplicationService Mock lifecycle + persistence ---
  {
    ApplicationService service(configPath);
    service.start();
    expect(service.running(), "service starts");

    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-e2e";
    mock.protocol = "mock";
    mock.enabled = true;
    mock.description = "e2e mock";
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "Motor-01";
    eq.type = "motor";
    eq.capabilities = {"start", "stop"};
    virtual_factory::icp::TelemetryMappingRecord tel;
    tel.name = "speed";
    tel.unit = "rpm";
    eq.telemetry.push_back(tel);
    mock.equipment.push_back(eq);

    auto upsert = service.upsertAdapterConfig(mock);
    expect(upsert.ok, "upsert mock adapter: " + upsert.message);
    auto validated = service.validateConfiguration();
    expect(validated.ok, "validate configuration: " + validated.message);
    auto saved = service.saveConfiguration();
    expect(saved.ok, "save configuration: " + saved.message);

    auto connected = service.connectAdapter("mock-e2e");
    expect(connected.ok, "connect mock: " + connected.message);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    auto equipment = service.equipment();
    expect(!equipment.empty(), "equipment appears after connect");
    bool foundMotor = false;
    for (const auto &snap : equipment)
    {
      if (snap.equipmentId == "Motor-01")
      {
        foundMotor = true;
        expect(snap.protocol == "mock", "equipment protocol is mock");
        expect(!snap.telemetry.empty(), "live telemetry present");
      }
    }
    expect(foundMotor, "Motor-01 present");

    auto disconnected = service.disconnectAdapter("mock-e2e");
    expect(disconnected.ok, "disconnect mock");
    service.stop();
  }

  {
    ApplicationService reloaded(configPath);
    auto loaded = reloaded.loadConfiguration();
    expect(loaded.ok, "reload configuration after restart: " + loaded.message);
    expect(reloaded.catalog().adapter("mock-e2e") != nullptr, "persisted adapter present");
  }

  // --- HTTP Application API + GUI static + Mock/PN/PB flows ---
  ApplicationService httpService(configPath);
  httpService.start();
  expect(httpService.loadConfiguration().ok, "HTTP service loads persisted config");

  const int port = freePort();
  const std::string guiRoot =
#ifdef VF_ICP_GUI_ROOT
      VF_ICP_GUI_ROOT;
#else
      "icp/gui";
#endif
  HttpApiServer api(httpService, guiRoot, "127.0.0.1", port);
  expect(api.start(), "HTTP API starts");
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(5, 0);

  {
    auto health = client.Get("/api/v1/health");
    expect(health && health->status == 200, "GET /api/v1/health");
  }
  {
    auto gui = client.Get("/");
    expect(gui && gui->status == 200, "GET / GUI index");
    if (gui)
    {
      expect(
          gui->body.find("Industrial Connectivity") != std::string::npos,
          "GUI brand present");
    }
  }
  {
    auto st = client.Get("/api/v1/status");
    expect(st && st->status == 200, "GET /api/v1/status");
    if (st)
    {
      auto body = json::parse(st->body);
      expect(body["mesDependency"] == false, "status: no MES dependency");
      expect(body["cicDependency"] == false, "status: no CIC dependency");
    }
  }
  {
    auto protocols = client.Get("/api/v1/protocols");
    expect(protocols && protocols->status == 200, "GET /api/v1/protocols");
    if (protocols)
    {
      auto body = json::parse(protocols->body);
      expect(body["protocols"].is_array() && body["protocols"].size() >= 8, "protocol list");
    }
  }

  {
    json adapter = {
        {"adapterId", "mock-http"},
        {"protocol", "mock"},
        {"enabled", true},
        {"description", "http mock"},
        {"connection", json::object()},
        {"credentials", json::object()},
        {"equipment",
         json::array(
             {{{"equipmentId", "Pump-01"},
               {"type", "pump"},
               {"capabilities", json::array({"start", "stop"})},
               {"telemetry",
                json::array({{{"name", "pressure"}, {"unit", "bar"}}})},
               {"commands", json::array({{{"command", "start"}}})},
               {"state", {{"mapped", false}}},
               {"fault", {{"mapped", false}}}}})}};
    auto post = client.Post("/api/v1/adapters", adapter.dump(), "application/json");
    expect(post && post->status == 200, "POST /api/v1/adapters mock: " + (post ? post->body : ""));
    auto save = client.Post("/api/v1/configuration/save");
    expect(save && save->status == 200, "POST save");
    auto connect = client.Post("/api/v1/adapters/mock-http/connect");
    expect(
        connect && connect->status == 200,
        "POST connect mock-http: " + (connect ? connect->body : ""));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto equipment = client.Get("/api/v1/equipment");
    expect(equipment && equipment->status == 200, "GET equipment");
    if (equipment)
    {
      auto body = json::parse(equipment->body);
      bool found = false;
      for (const auto &eqItem : body["equipment"])
      {
        if (eqItem["equipmentId"] == "Pump-01")
        {
          found = true;
          expect(eqItem.contains("communicationState"), "communication state field");
          expect(eqItem.contains("machineState"), "machine state field");
          expect(eqItem.contains("machineFault"), "machine fault field");
          expect(
              eqItem["telemetry"].is_array() && !eqItem["telemetry"].empty(),
              "telemetry from backend");
        }
      }
      expect(found, "Pump-01 appears via API");
    }
    auto disc = client.Post("/api/v1/adapters/mock-http/disconnect");
    expect(disc && disc->status == 200, "disconnect mock-http");
  }

  {
    auto diag = client.Get("/api/v1/diagnostics");
    expect(diag && diag->status == 200, "GET diagnostics after mock only");
    if (diag)
    {
      auto body = json::parse(diag->body);
      expect(
          !body["implementations"].contains("gateway"),
          "mock-only: no gateway implementation section");
      expect(
          !body["implementations"].contains("hilscher_native"),
          "mock-only: no Hilscher section");
      expect(!body.contains("hilscher"), "mock-only: no global hilscher block");
      expect(body.contains("system"), "diagnostics: system health summary");
      expect(body.contains("icp"), "diagnostics: ICP system self-health");
      expect(body.contains("activeAlarms"), "diagnostics: activeAlarms array");
      expect(body.contains("recentEvents"), "diagnostics: recentEvents array");
      expect(body["system"].contains("overallHealth"), "diagnostics: overallHealth");
      expect(body["system"].contains("faultedHealthAdapters"),
             "diagnostics: faultedHealthAdapters count");
      expect(body["system"].contains("unknownHealthAdapters"),
             "diagnostics: unknownHealthAdapters count");
      expect(body["system"]["mtbfStatus"] == "insufficient_data",
             "diagnostics: MTBF not fabricated without history");
      expect(body["icp"].contains("overallHealth"), "icp overallHealth present");
      expect(body["icp"].contains("serviceStatus"), "icp serviceStatus present");
      expect(body["icp"].contains("selfTestResult"), "icp selfTestResult present");
      expect(
          body["icp"]["overallHealth"] == "HEALTHY"
              || body["icp"]["overallHealth"] == "DEGRADED",
          "icp health independent of industrial adapters (running service)");
      bool foundMock = false;
      for (const auto &adapter : body["adapters"])
      {
        if (adapter["adapterId"] == "mock-http")
        {
          foundMock = true;
          expect(adapter.contains("connectionAttempts"), "adapter connectionAttempts");
          expect(adapter.contains("successfulConnections"), "adapter successfulConnections");
          expect(adapter.contains("failedConnections"), "adapter failedConnections");
          expect(adapter.contains("reconnectCount"), "adapter reconnectCount");
          expect(adapter.contains("faultCount"), "adapter faultCount");
          expect(adapter.contains("communicationHealth"), "adapter communicationHealth alias");
          expect(adapter.contains("health"), "adapter health field");
          expect(adapter.contains("healthReason"), "adapter healthReason field");
          expect(adapter.contains("communicationLifecycleState"),
                 "adapter communicationLifecycleState");
          expect(adapter.contains("associatedEquipment"),
                 "adapter associatedEquipment list");
          expect(adapter.contains("uptimeMs"), "adapter uptimeMs");
          expect(adapter["successfulConnections"].get<int>() >= 1,
                 "mock successfulConnections after connect");
          expect(adapter["protocolSpecific"]["available"] == false,
                 "protocol-specific not fabricated");
          expect(
              adapter["protocolSpecific"]["detail"].get<std::string>().find(
                  "no additional protocol-specific metrics")
                  != std::string::npos,
              "protocol-specific wording present");
          // Diagnostics block runs after disconnect: session counters remain,
          // but health must not be conflated with connection lifecycle.
          const std::string health = adapter["health"].get<std::string>();
          expect(health == "HEALTHY" || health == "UNKNOWN" || health == "DEGRADED"
                     || health == "FAULTED",
                 "mock health uses HEALTHY/DEGRADED/FAULTED/UNKNOWN vocabulary");
          expect(health != "FAILED", "health must not use FAILED (use FAULTED)");
          expect(adapter["communicationLifecycleState"] == "DISCONNECTED"
                     || adapter["communicationLifecycleState"] == "UNKNOWN",
                 "disconnected mock lifecycle is DISCONNECTED/UNKNOWN");
          expect(adapter["communicationHealth"] == health,
                 "communicationHealth alias matches health");
          expect(adapter.contains("healthReason"), "healthReason explains UNKNOWN/etc");
          expect(adapter.contains("connectionState"), "connectionState still present");
        }
      }
      expect(foundMock, "diagnostics includes mock-http adapter metrics");

      // Active alarms must not include recovered early-warning noise for a healthy mock.
      for (const auto &alarm : body["activeAlarms"])
      {
        if (alarm["sourceId"] == "mock-http" && alarm["category"] == "COMMUNICATION")
        {
          expect(false, "disconnected mock should not keep COMMUNICATION active alarm");
        }
      }

      for (const auto &eq : body["equipment"])
      {
        if (eq["adapterId"] == "mock-http")
        {
          expect(eq.contains("health"), "equipment health present");
          expect(eq.contains("healthReason"), "equipment healthReason present");
          expect(eq.contains("operationalStateDisplay"),
                 "equipment operationalStateDisplay present");
          expect(eq.contains("configuredCommands"), "equipment configuredCommands");
          expect(eq.contains("commands"), "equipment commands diagnostic array");
          expect(eq["commandRuntimeState"] != "Not available",
                 "command runtime placeholder replaced");
          // Never-executed configured commands must not be treated as errors.
          if (eq["commands"].is_array())
          {
            for (const auto &cmd : eq["commands"])
            {
              expect(cmd.contains("availability"), "command availability");
              expect(cmd.contains("execution"), "command execution");
              expect(cmd["execution"] == "NOT_EXECUTED" || cmd["execution"] == "SUCCESS"
                         || cmd["execution"] == "FAILED" || cmd["execution"] == "UNKNOWN",
                     "command execution vocabulary");
              if (cmd["execution"] == "NOT_EXECUTED")
              {
                expect(cmd["availability"] == "CONFIGURED"
                           || cmd["availability"] == "AVAILABLE"
                           || cmd["availability"] == "UNAVAILABLE",
                       "not-executed availability is CONFIGURED/AVAILABLE/UNAVAILABLE");
              }
            }
          }
        }
      }
    }
  }

  {
    // Connected health semantics: CONNECTED must not auto-map to HEALTHY.
    auto recon = client.Post("/api/v1/adapters/mock-http/connect");
    expect(recon && recon->status == 200, "connect mock-http for health semantics");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    auto diag = client.Get("/api/v1/diagnostics");
    expect(diag && diag->status == 200, "diagnostics while mock connected");
    if (diag)
    {
      auto body = json::parse(diag->body);
      for (const auto &adapter : body["adapters"])
      {
        if (adapter["adapterId"] != "mock-http")
        {
          continue;
        }
        expect(adapter["communicationLifecycleState"] == "CONNECTED",
               "lifecycle CONNECTED while connected");
        const std::string health = adapter["health"].get<std::string>();
        expect(health == "HEALTHY" || health == "UNKNOWN" || health == "DEGRADED",
               "connected health is HEALTHY/UNKNOWN/DEGRADED (not auto-FAILED)");
        expect(adapter.contains("healthReason"), "connected healthReason present");
        if (health == "UNKNOWN")
        {
          expect(
              adapter["healthReason"].get<std::string>().find("no successful communication")
                  != std::string::npos,
              "UNKNOWN health explains missing successful communication");
        }
        if (health == "HEALTHY")
        {
          expect(adapter["hasSuccessfulCommunication"] == true,
                 "HEALTHY requires observed successful communication");
        }
      }
      // ICP software health must not fault solely because of industrial adapters.
      expect(body["icp"]["overallHealth"] != "FAULTED"
                 || body["icp"]["configurationStatus"] == "INVALID"
                 || body["icp"]["serviceStatus"] == "STOPPED",
             "ICP FAULTED only for software/config issues");
    }
    auto disc = client.Post("/api/v1/adapters/mock-http/disconnect");
    expect(disc && disc->status == 200, "disconnect after health semantics");
  }

  {
    // Duration totals must not explode across repeated diagnostics observations.
    auto recon = client.Post("/api/v1/adapters/mock-http/connect");
    expect(recon && recon->status == 200, "connect for duration regression");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::int64_t firstConnected = -1;
    for (int i = 0; i < 8; ++i)
    {
      auto diag = client.Get("/api/v1/diagnostics");
      expect(diag && diag->status == 200, "diagnostics duration poll");
      if (!diag)
      {
        break;
      }
      auto body = json::parse(diag->body);
      for (const auto &adapter : body["adapters"])
      {
        if (adapter["adapterId"] != "mock-http")
        {
          continue;
        }
        const auto connected = adapter["cumulativeConnectedMs"].get<std::int64_t>();
        const auto disconnected =
            adapter["cumulativeDisconnectedMs"].get<std::int64_t>();
        expect(connected >= 0, "cumulativeConnectedMs non-negative");
        expect(disconnected >= 0, "cumulativeDisconnectedMs non-negative");
        expect(connected < 60LL * 60LL * 1000LL,
               "cumulativeConnectedMs not epoch garbage");
        expect(disconnected < 60LL * 60LL * 1000LL,
               "cumulativeDisconnectedMs not epoch garbage");
        if (firstConnected < 0)
        {
          firstConnected = connected;
        }
        else
        {
          // May grow with open-interval inclusion, but must not quadratic-explode.
          expect(connected <= firstConnected + 30LL * 1000LL,
                 "cumulativeConnectedMs stable across diagnostics polls");
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    auto disc = client.Post("/api/v1/adapters/mock-http/disconnect");
    expect(disc && disc->status == 200, "disconnect after duration regression");
  }

  {
    auto recon = client.Post("/api/v1/adapters/mock-http/connect");
    expect(recon && recon->status == 200, "reconnect setup: connect mock-http again");
    auto reconnect = client.Post("/api/v1/adapters/mock-http/reconnect");
    expect(reconnect && reconnect->status == 200, "reconnect mock-http");
    auto diag = client.Get("/api/v1/diagnostics");
    expect(diag && diag->status == 200, "diagnostics after reconnect");
    if (diag)
    {
      auto body = json::parse(diag->body);
      for (const auto &adapter : body["adapters"])
      {
        if (adapter["adapterId"] == "mock-http")
        {
          expect(adapter["reconnectCount"].get<int>() >= 1,
                 "reconnectCount increments on reconnect");
          expect(adapter["connectionAttempts"].get<int>() >= 2,
                 "connectionAttempts include reconnect connect");
        }
      }
    }
    auto disc = client.Post("/api/v1/adapters/mock-http/disconnect");
    expect(disc && disc->status == 200, "disconnect mock-http after reconnect test");
  }

  {
    json adapter = {
        {"adapterId", "pn-gateway"},
        {"protocol", "profinet"},
        {"implementation", "gateway"},
        {"enabled", true},
        {"description", "PROFINET field devices via industrial gateway"},
        {"connection",
         {{"endpointUrl", "opc.tcp://127.0.0.1:4840"}, {"timeoutMs", 2000}}},
        {"credentials", json::object()},
        {"equipment",
         json::array(
             {{{"equipmentId", "PN-GW-01"},
               {"type", "plc"},
               {"capabilities", json::array()},
               {"telemetry",
                json::array({{{"name", "speed"},
                              {"unit", "rpm"},
                              {"address", "ns=2;s=Speed"},
                              {"namespaceIndex", 2}}})},
               {"commands", json::array()},
               {"state", {{"mapped", false}}},
               {"fault", {{"mapped", false}}}}})}};
    auto post = client.Post("/api/v1/adapters", adapter.dump(), "application/json");
    expect(
        post && post->status == 200,
        "configure PROFINET gateway adapter: " + (post ? post->body : ""));
    auto validate = client.Post("/api/v1/configuration/validate");
    expect(validate && validate->status == 200, "validate PROFINET gateway");
  }

  {
    json adapter = {
        {"adapterId", "pb-gateway"},
        {"protocol", "profibus"},
        {"implementation", "gateway"},
        {"enabled", true},
        {"description", "PROFIBUS field devices via industrial gateway"},
        {"connection", {{"host", "127.0.0.1"}, {"port", 502}, {"timeoutMs", 2000}}},
        {"credentials", json::object()},
        {"equipment",
         json::array(
             {{{"equipmentId", "PB-GW-01"},
               {"type", "plc"},
               {"capabilities", json::array()},
               {"telemetry",
                json::array({{{"name", "reg"},
                              {"table", "holdingRegister"},
                              {"registerAddress", 1}}})},
               {"commands", json::array()},
               {"state", {{"mapped", false}}},
               {"fault", {{"mapped", false}}}}})}};
    auto post = client.Post("/api/v1/adapters", adapter.dump(), "application/json");
    expect(
        post && post->status == 200,
        "configure PROFIBUS gateway adapter: " + (post ? post->body : ""));
  }

  {
    auto diag = client.Get("/api/v1/diagnostics");
    expect(diag && diag->status == 200, "GET diagnostics with gateway fieldbus only");
    if (diag)
    {
      auto body = json::parse(diag->body);
      expect(
          body["implementations"].contains("gateway"),
          "gateway section when PROFINET/PROFIBUS use gateway implementation");
      expect(
          !body["implementations"].contains("hilscher_native"),
          "no Hilscher section for gateway PROFINET/PROFIBUS");
      if (body["implementations"].contains("gateway"))
      {
        const auto &gw = body["implementations"]["gateway"];
        const auto &adapters = gw["adapters"];
        bool pnGateway = false;
        bool pbGateway = false;
        for (const auto &item : adapters)
        {
          if (item["adapterId"] == "pn-gateway"
              && item["implementation"] == "gateway")
          {
            pnGateway = true;
          }
          if (item["adapterId"] == "pb-gateway"
              && item["implementation"] == "gateway")
          {
            pbGateway = true;
          }
        }
        expect(pnGateway, "PROFINET gateway adapter listed under gateway implementation");
        expect(pbGateway, "PROFIBUS gateway adapter listed under gateway implementation");
      }
    }
  }

  {
    auto connect = client.Post("/api/v1/adapters/pn-gateway/connect");
    expect(static_cast<bool>(connect), "connect PROFINET gateway response");
    if (connect)
    {
      const std::string body = connect->body;
      expect(
          body.find("Hilscher") == std::string::npos,
          "PROFINET gateway connect error must not mention Hilscher: " + body);
      expect(
          body.find("Gateway runtime for PROFINET is not currently available") != std::string::npos,
          "PROFINET gateway connect reports product-level gateway limitation: " + body);
    }
  }

  {
    auto connect = client.Post("/api/v1/adapters/pb-gateway/connect");
    expect(static_cast<bool>(connect), "connect PROFIBUS gateway response");
    if (connect)
    {
      const std::string body = connect->body;
      expect(
          body.find("Hilscher") == std::string::npos,
          "PROFIBUS gateway connect error must not mention Hilscher: " + body);
      expect(
          body.find("Gateway runtime for PROFIBUS is not currently available") != std::string::npos,
          "PROFIBUS gateway connect reports product-level gateway limitation: " + body);
    }
  }

  {
    json adapter = {
        {"adapterId", "pn-cfg"},
        {"protocol", "profinet"},
        {"implementation", "hilscher_native"},
        {"enabled", true},
        {"description", "pn without hardware"},
        {"connection",
         {{"boardId", "cifx0"},
          {"channel", 0},
          {"interfaceName", "eth0"},
          {"stationName", "icp-controller"},
          {"processImageBytes", 64}}},
        {"credentials", json::object()},
        {"equipment",
         json::array(
             {{{"equipmentId", "PN-IO-01"},
               {"type", "io_device"},
               {"stationName", "device-01"},
               {"ipAddress", "192.168.0.10"},
               {"capabilities", json::array()},
               {"telemetry",
                json::array({{{"name", "in0"},
                              {"inputByteOffset", 0},
                              {"valueType", "UINT8"}}})},
               {"commands", json::array()},
               {"state", {{"mapped", false}}},
               {"fault", {{"mapped", false}}},
               {"submodules",
                json::array({{{"slot", 0},
                              {"subslot", 1},
                              {"inputLength", 8},
                              {"outputLength", 8}}})}}})}};
    auto post = client.Post("/api/v1/adapters", adapter.dump(), "application/json");
    expect(
        post && post->status == 200,
        "configure PROFINET without hardware: " + (post ? post->body : ""));
    auto validate = client.Post("/api/v1/configuration/validate");
    expect(validate && validate->status == 200, "validate with PROFINET");
    auto save = client.Post("/api/v1/configuration/save");
    expect(save && save->status == 200, "save PROFINET config");
    auto connect = client.Post("/api/v1/adapters/pn-cfg/connect");
    expect(static_cast<bool>(connect), "connect PROFINET response");
    if (connect)
    {
      bool failed = connect->status >= 400;
      if (!failed)
      {
        try
        {
          failed = json::parse(connect->body).value("ok", true) == false;
        }
        catch (...)
        {
          failed = true;
        }
      }
      expect(failed, "PROFINET connect must not pretend success without hardware");
      const std::string body = connect->body;
      expect(
          body.find("Hilscher") != std::string::npos
              || body.find("hardware") != std::string::npos
              || body.find("BLOCKED") != std::string::npos
              || body.find("SDK") != std::string::npos
              || body.find("cifX") != std::string::npos
              || body.find("not") != std::string::npos
              || body.find("FAIL") != std::string::npos
              || body.find("fail") != std::string::npos
              || body.find("error") != std::string::npos
              || body.find("Error") != std::string::npos,
          "PROFINET connect error mentions failure/hardware: " + body);
    }
  }

  {
    json adapter = {
        {"adapterId", "pb-cfg"},
        {"protocol", "profibus"},
        {"implementation", "hilscher_native"},
        {"enabled", true},
        {"connection",
         {{"boardId", "cifx0"},
          {"channel", 0},
          {"masterAddress", 1},
          {"baudRateKbps", 1500},
          {"processImageBytes", 64}}},
        {"credentials", json::object()},
        {"equipment",
         json::array(
             {{{"equipmentId", "PB-Slave-01"},
               {"type", "dp_slave"},
               {"stationAddress", 3},
               {"capabilities", json::array()},
               {"telemetry",
                json::array({{{"name", "in0"},
                              {"inputByteOffset", 0},
                              {"valueType", "UINT8"}}})},
               {"commands", json::array()},
               {"state", {{"mapped", false}}},
               {"fault", {{"mapped", false}}},
               {"modules",
                json::array({{{"slot", 0},
                              {"ident", "m0"},
                              {"inputLength", 8},
                              {"outputLength", 8}}})}}})}};
    auto post = client.Post("/api/v1/adapters", adapter.dump(), "application/json");
    expect(
        post && post->status == 200,
        "configure PROFIBUS without hardware: " + (post ? post->body : ""));
    auto save = client.Post("/api/v1/configuration/save");
    expect(save && save->status == 200, "save PROFIBUS config");
  }

  {
    auto diag = client.Get("/api/v1/diagnostics");
    expect(diag && diag->status == 200, "GET diagnostics");
    if (diag)
    {
      auto body = json::parse(diag->body);
      expect(body["runtime"]["mesDependency"] == false, "diagnostics: no MES");
      expect(
          body.contains("implementations") && body["implementations"].is_object(),
          "diagnostics: contextual implementations object");
      expect(
          body["implementations"].contains("gateway"),
          "gateway section when gateway PROFINET/PROFIBUS adapters configured");
      expect(
          body["implementations"].contains("hilscher_native"),
          "hilscher section when Hilscher native PROFINET/PROFIBUS configured");
      if (body["implementations"].contains("hilscher_native"))
      {
        const auto &hw = body["implementations"]["hilscher_native"]["hardware"];
        expect(
            hw["hardware"] == "NOT DETECTED" || hw["boardCount"] == 0,
            "Hilscher hardware not falsely READY");
        const auto &adapters = body["implementations"]["hilscher_native"]["adapters"];
        bool nativePn = false;
        bool nativePb = false;
        for (const auto &item : adapters)
        {
          if (item["adapterId"] == "pn-cfg"
              && item["implementation"] == "hilscher_native")
          {
            nativePn = true;
          }
          if (item["adapterId"] == "pb-cfg"
              && item["implementation"] == "hilscher_native")
          {
            nativePb = true;
          }
        }
        expect(nativePn, "native PROFINET adapter under hilscher implementation");
        expect(nativePb, "native PROFIBUS adapter under hilscher implementation");
      }
      if (body["implementations"].contains("gateway"))
      {
        expect(
            body["implementations"]["gateway"].contains("adapters"),
            "gateway section still present alongside native fieldbus");
      }
      expect(!body.contains("hilscher"), "no global permanent hilscher block");
    }
  }

  // Adapter list / remove
  {
    auto list = client.Get("/api/v1/adapters");
    expect(list && list->status == 200, "list adapters");
    auto del = client.Delete("/api/v1/adapters/mock-http");
    expect(del && del->status == 200, "remove adapter");
  }

  api.stop();
  httpService.stop();

  // --- ApplicationService GUI/config OPC UA NodeId mapping path ---
  // JSON stores address as expanded NodeId text; createRuntimeAdapter must
  // map to bare identifier so the adapter reads ns=1;s=<id>, not ns=1;s=ns=1;s=<id>.
  {
    using virtual_factory::ConnectionState;
    using virtual_factory::opcUaNodeRefFromConfig;

    // Exact GUI regression: address="ns=2;s=MotorSpeed", namespaceIndex=2.
    {
      const auto mapped = opcUaNodeRefFromConfig(2, "ns=2;s=MotorSpeed");
      expect(mapped.namespaceIndex == 2,
             "ApplicationService boundary: MotorSpeed namespaceIndex==2");
      expect(mapped.identifier == "MotorSpeed",
             "ApplicationService boundary: MotorSpeed identifier is bare");
      const std::string constructed =
          "ns=" + std::to_string(static_cast<unsigned>(mapped.namespaceIndex))
          + ";s=" + mapped.identifier;
      expect(constructed == "ns=2;s=MotorSpeed",
             "ApplicationService boundary: constructs ns=2;s=MotorSpeed");
      expect(constructed != "ns=2;s=ns=2;s=MotorSpeed",
             "ApplicationService boundary: must not double-encode NodeId");
    }

    virtual_factory::test::OpcUaTestServer opcuaServer;
    expect(opcuaServer.start(), "ApplicationService OPC UA fixture starts");
    if (opcuaServer.start())
    {
      const std::string opcuaConfigPath =
          "/tmp/icp-opcua-gui-map-" + std::to_string(::getpid()) + ".json";
      ::unlink(opcuaConfigPath.c_str());

      ApplicationService opcuaService(opcuaConfigPath);
      opcuaService.start();

      virtual_factory::icp::AdapterConfigRecord opcua;
      opcua.adapterId = "opcua-gui-map";
      opcua.protocol = "opcua";
      opcua.enabled = true;
      opcua.connection.endpointUrl = opcuaServer.endpointUrl();
      virtual_factory::icp::EquipmentMappingRecord eq;
      eq.equipmentId = virtual_factory::test::OpcUaTestServer::kMixerId;
      eq.type = "mixer";
      eq.capabilities = {"start", "stop"};
      virtual_factory::icp::TelemetryMappingRecord speed;
      speed.name = "speed";
      speed.unit = "rpm";
      speed.namespaceIndex = 1;
      speed.address = std::string("ns=1;s=")
          + virtual_factory::test::OpcUaTestServer::kMixerSpeedActual;
      eq.telemetry.push_back(speed);
      virtual_factory::icp::SignalMappingRecord state;
      state.mapped = true;
      state.namespaceIndex = 1;
      state.address = std::string("ns=1;s=")
          + virtual_factory::test::OpcUaTestServer::kMixerRunning;
      eq.state = state;
      virtual_factory::icp::SignalMappingRecord fault;
      fault.mapped = true;
      fault.namespaceIndex = 1;
      fault.address = std::string("ns=1;s=")
          + virtual_factory::test::OpcUaTestServer::kMixerFault;
      eq.fault = fault;
      opcua.equipment.push_back(eq);

      auto upserted = opcuaService.upsertAdapterConfig(opcua);
      expect(upserted.ok, "upsert OPC UA GUI-style adapter: " + upserted.message);
      auto connected = opcuaService.connectAdapter("opcua-gui-map");
      expect(connected.ok, "connect OPC UA GUI-style adapter: " + connected.message);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      auto adapterView = opcuaService.adapter("opcua-gui-map");
      expect(adapterView.has_value(), "GUI-map adapter view present");
      if (adapterView)
      {
        expect(adapterView->connectionState == "CONNECTED",
               "GUI-map adapter CONNECTED (not FAULTED from BadNodeId): "
                   + adapterView->connectionState
                   + " err=" + adapterView->lastError);
      }

      bool found = false;
      for (const auto &snap : opcuaService.equipment())
      {
        if (snap.equipmentId == virtual_factory::test::OpcUaTestServer::kMixerId)
        {
          found = true;
          expect(snap.protocol == "opcua", "GUI-map equipment protocol opcua");
          expect(snap.communicationState == ConnectionState::Connected,
                 "GUI-map equipment Connected (NodeId not double-encoded)");
          expect(snap.lastError.empty(),
                 "GUI-map equipment has no lastError: " + snap.lastError);
          bool hasSpeed = false;
          for (const auto &tel : snap.telemetry)
          {
            if (tel.name == "speed")
            {
              hasSpeed = true;
              expect(tel.value > 0.0, "GUI-map speed telemetry populated");
            }
          }
          expect(hasSpeed, "GUI-map speed telemetry present after poll");
        }
      }
      expect(found, "GUI-map mixer equipment present");

      opcuaService.disconnectAdapter("opcua-gui-map");
      opcuaService.stop();
      ::unlink(opcuaConfigPath.c_str());
    }
  }

  // --- Lifecycle preservation: failed connect must not become phantom DISCONNECTED ---
  {
    const std::string lifePath =
        "/tmp/icp-life-" + std::to_string(::getpid()) + ".json";
    ::unlink(lifePath.c_str());
    ApplicationService life(lifePath);
    life.start();

    virtual_factory::icp::AdapterConfigRecord opcua;
    opcua.adapterId = "opcua-life";
    opcua.protocol = "opcua";
    opcua.enabled = true;
    opcua.connection.endpointUrl = "opc.tcp://127.0.0.1:1";  // nothing listening
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "EQ-LIFE";
    eq.type = "plc";
    virtual_factory::icp::TelemetryMappingRecord tel;
    tel.name = "speed";
    tel.address = "ns=1;s=Speed";
    tel.namespaceIndex = 1;
    eq.telemetry.push_back(tel);
    opcua.equipment.push_back(eq);
    expect(life.upsertAdapterConfig(opcua).ok, "upsert opcua-life");

    auto connected = life.connectAdapter("opcua-life");
    expect(connected.ok && connected.accepted,
           "connect to dead endpoint is accepted asynchronously");
    expect(waitForAdapterState(life, "opcua-life", "FAULTED", 8000),
           "async connect to dead endpoint becomes FAULTED");
    expect(life.status().runtimeAdapterCount == 1,
           "failed connect still keeps runtime adapter object");
    auto view = life.adapter("opcua-life");
    expect(view.has_value(), "opcua-life view present");
    if (view)
    {
      expect(view->runtimePresent, "runtimePresent true after failed connect");
      expect(view->connectionState == "FAULTED",
             "failed connect is FAULTED not silent DISCONNECTED");
      expect(!view->lastError.empty(), "FAULTED has lastError");
    }

    // FAULTED remains recoverable via Disconnect / Connect API.
    expect(life.disconnectAdapter("opcua-life").ok, "disconnect from FAULTED");
    expect(waitForAdapterState(life, "opcua-life", "DISCONNECTED", 5000),
           "explicit disconnect yields DISCONNECTED");
    view = life.adapter("opcua-life");
    expect(life.status().runtimeAdapterCount == 1,
           "runtime adapter remains after disconnect");

    // Diagnostics / Adapters share the same lifecycle vocabulary.
    const auto report = life.diagnosticsReport();
    expect(report.icp.overallHealth == "HEALTHY"
               || report.icp.overallHealth == "DEGRADED",
           "ICP self-health independent of industrial FAULTED/DISCONNECTED");
    bool found = false;
    for (const auto &a : report.adapters)
    {
      if (a.adapter.adapterId != "opcua-life")
      {
        continue;
      }
      found = true;
      expect(a.adapter.connectionState == "DISCONNECTED",
             "diagnostics lifecycle matches adapters()");
      expect(a.session.communicationLifecycleState == "DISCONNECTED",
             "diagnostics communicationLifecycleState matches");
    }
    expect(found, "diagnostics includes opcua-life");

    // ensureRuntimeAdapter create-first: rebuild on connect must not drop count to 0.
    for (int i = 0; i < 3; ++i)
    {
      (void)life.connectAdapter("opcua-life");
      expect(waitForAdapterState(life, "opcua-life", "FAULTED", 8000),
             "attempt reaches FAULTED");
      expect(life.status().runtimeAdapterCount == 1,
             "runtimeAdapters stays 1 across failed reconnect attempts");
      expect(life.disconnectAdapter("opcua-life").ok, "disconnect between attempts");
      expect(waitForAdapterState(life, "opcua-life", "DISCONNECTED", 5000),
             "disconnect between attempts settles");
    }

    life.stop();
    ::unlink(lifePath.c_str());
  }

  {
    // --- Command diagnostics + richer lifecycle/health events ---
    const std::string cmdPath = tempConfigPath() + "-cmd.json";
    ::unlink(cmdPath.c_str());
    ApplicationService svc(cmdPath);
    svc.start();

    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-cmd";
    mock.protocol = "mock";
    mock.enabled = true;
    mock.description = "command diagnostics";
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "EQ-CMD";
    eq.type = "mixer";
    eq.capabilities = {"start", "stop"};
    virtual_factory::icp::TelemetryMappingRecord tel;
    tel.name = "speed";
    tel.unit = "rpm";
    eq.telemetry.push_back(tel);
    virtual_factory::icp::CommandMappingRecord startCmd;
    startCmd.command = "start";
    eq.commands.push_back(startCmd);
    virtual_factory::icp::CommandMappingRecord stopCmd;
    stopCmd.command = "stop";
    eq.commands.push_back(stopCmd);
    virtual_factory::icp::CommandMappingRecord customCmd;
    customCmd.command = "cmd1";
    eq.commands.push_back(customCmd);
    mock.equipment.push_back(eq);
    expect(svc.upsertAdapterConfig(mock).ok, "upsert mock-cmd");
    expect(svc.connectAdapter("mock-cmd").ok, "connect mock-cmd");

    {
      auto cmds = svc.commandDiagnosticsForEquipment("mock-cmd", "EQ-CMD");
      expect(!cmds.empty(), "configured commands exposed");
      bool foundStart = false;
      for (const auto &c : cmds)
      {
        if (c.command != "start")
        {
          continue;
        }
        foundStart = true;
        expect(c.execution == "NOT_EXECUTED", "configured start not auto-executed");
        expect(c.availability == "CONFIGURED" || c.availability == "AVAILABLE",
               "never-executed start is CONFIGURED or AVAILABLE");
        expect(c.lastError.empty(), "never-executed has no error");
      }
      expect(foundStart, "start command present in diagnostics");
    }

    {
      const auto before = svc.events(200);
      const std::size_t beforeCount = before.size();
      // Repeated observations must not duplicate lifecycle events.
      (void)svc.diagnosticsReport();
      (void)svc.diagnosticsReport();
      const auto after = svc.events(200);
      std::size_t lifecycleEvents = 0;
      for (const auto &ev : after)
      {
        if (ev.adapterId == "mock-cmd" && ev.eventType == "lifecycle_changed")
        {
          ++lifecycleEvents;
          expect(!ev.previousState.empty() && !ev.newState.empty(),
                 "lifecycle event has previous/new state");
        }
      }
      expect(lifecycleEvents >= 1, "at least one lifecycle transition event recorded");
      expect(after.size() - beforeCount < 8,
             "diagnostics polls do not flood duplicate lifecycle events");
    }

    {
      EquipmentCommandResult ok = svc.executeEquipmentCommand("EQ-CMD", "start", 0.0);
      expect(ok.ok, "execute start succeeds on mock");
      auto cmds = svc.commandDiagnosticsForEquipment("mock-cmd", "EQ-CMD");
      bool found = false;
      for (const auto &c : cmds)
      {
        if (c.command != "start")
        {
          continue;
        }
        found = true;
        expect(c.execution == "SUCCESS", "start execution SUCCESS");
        expect(c.availability == "AVAILABLE", "successful command AVAILABLE");
        expect(c.hasLastExecution, "last execution timestamp set");
      }
      expect(found, "start runtime diagnostic updated");
    }

    {
      EquipmentCommandResult bad =
          svc.executeEquipmentCommand("EQ-CMD", "not_a_real_command", 0.0);
      expect(!bad.ok, "unknown command fails");
      bool sawCommandEvent = false;
      for (const auto &ev : svc.events(50))
      {
        if (ev.category == "command" && ev.eventType == "command_failed")
        {
          sawCommandEvent = true;
          expect(ev.command == "not_a_real_command"
                     || ev.message.find("not_a_real_command") != std::string::npos,
                 "failed command event names the command");
        }
      }
      expect(sawCommandEvent, "failed command produces command event");
    }

    expect(svc.disconnectAdapter("mock-cmd").ok, "disconnect mock-cmd");
    {
      auto cmds = svc.commandDiagnosticsForEquipment("mock-cmd", "EQ-CMD");
      for (const auto &c : cmds)
      {
        if (c.command == "stop" && !c.hasLastExecution)
        {
          expect(c.availability == "UNAVAILABLE",
                 "disconnected never-executed command is UNAVAILABLE");
          expect(c.execution == "NOT_EXECUTED", "still NOT_EXECUTED when disconnected");
        }
      }
      bool recoveredAlarmClear = true;
      for (const auto &alarm : svc.diagnosticsReport().activeAlarms)
      {
        if (alarm.sourceId == "mock-cmd" && alarm.category == "CONNECTION")
        {
          recoveredAlarmClear = false;
        }
      }
      expect(recoveredAlarmClear, "no active CONNECTION alarm after clean disconnect");
    }

    expect(svc.connectAdapter("mock-cmd").ok, "reconnect mock-cmd");
    expect(waitForAdapterState(svc, "mock-cmd", "CONNECTED", 5000),
           "mock-cmd CONNECTED after connect");
    expect(svc.reconnectAdapter("mock-cmd").ok, "explicit reconnect mock-cmd");
    expect(waitForAdapterState(svc, "mock-cmd", "CONNECTED", 5000),
           "mock-cmd CONNECTED after reconnect");
    {
      bool sawRecovery = false;
      for (const auto &ev : svc.events(100))
      {
        if (ev.adapterId == "mock-cmd"
            && (ev.category == "recovery" || ev.recovery == "successful"))
        {
          sawRecovery = true;
        }
      }
      expect(sawRecovery, "recovery event retained in recent events");
    }

    const auto icpHealth = svc.diagnosticsReport().icp.overallHealth;
    expect(icpHealth == "HEALTHY" || icpHealth == "DEGRADED",
           "ICP health independent after command/lifecycle exercises");

    svc.stop();
    ::unlink(cmdPath.c_str());
  }


  {
    // Communication fault history, auto-reconnect, and responsiveness.
    const std::string faultPath = tempConfigPath() + "-fault.json";
    ::unlink(faultPath.c_str());
    ApplicationService svc(faultPath);
    svc.start();

    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-fault";
    mock.protocol = "mock";
    mock.enabled = true;
    mock.description = "fault history";
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "EQ-FAULT";
    eq.type = "pump";
    eq.capabilities = {"start", "stop"};
    mock.equipment.push_back(eq);
    expect(svc.upsertAdapterConfig(mock).ok, "upsert mock-fault");
    expect(svc.connectAdapter("mock-fault").ok, "connect mock-fault");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    auto *mockRuntime = dynamic_cast<virtual_factory::MockIndustrialAdapter *>(
        svc.manager().adapter("mock-fault"));
    expect(mockRuntime != nullptr, "mock-fault runtime pointer");
    if (mockRuntime == nullptr)
    {
      svc.stop();
      ::unlink(faultPath.c_str());
    }
    else
    {
      mockRuntime->simulateCommunicationFailure(
          "OPC UA read failed for ns=2;s=MotorSpeed: BadSecureChannelClosed");

      bool sawFaultEvent = false;
      bool sawAlarm = false;
      for (int i = 0; i < 50 && !(sawFaultEvent && sawAlarm); ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const auto &ev : svc.events(100))
        {
          if (ev.adapterId == "mock-fault"
              && (ev.eventType == "communication_fault" || ev.newState == "FAULTED"))
          {
            if (ev.level == "error" || ev.eventType == "communication_fault")
            {
              sawFaultEvent = true;
              expect(!ev.errorDetails.empty() || !ev.reason.empty(),
                     "fault event carries error details");
              if (!ev.errorDetails.empty())
              {
                expect(
                    ev.errorDetails.find("BadSecureChannelClosed") != std::string::npos,
                    "fault event preserves OPC UA status text");
              }
              if (!ev.nodeId.empty())
              {
                expect(ev.nodeId.find("MotorSpeed") != std::string::npos,
                       "fault event captures nodeId");
              }
            }
          }
        }
        for (const auto &alarm : svc.diagnosticsReport().activeAlarms)
        {
          if (alarm.sourceId == "mock-fault")
          {
            sawAlarm = true;
          }
        }
      }
      expect(sawFaultEvent, "historical communication fault event recorded by poll hook");
      expect(sawAlarm, "active alarm while faulted");
      expect(svc.manager().adapter("mock-fault")->connectionState()
                 == virtual_factory::ConnectionState::Faulted,
             "adapter currently FAULTED");

      const auto icpDuring = svc.diagnosticsReport().icp.overallHealth;
      expect(icpDuring == "HEALTHY" || icpDuring == "DEGRADED",
             "ICP software health independent of industrial fault");

      std::size_t faultEvents = 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(900));
      for (const auto &ev : svc.events(200))
      {
        if (ev.adapterId == "mock-fault" && ev.eventType == "communication_fault")
        {
          ++faultEvents;
        }
      }
      expect(faultEvents == 1, "no communication_fault poll flood");

      const auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < 25; ++i)
      {
        (void)svc.status();
        (void)svc.diagnosticsReport();
        (void)svc.events(10);
      }
      const auto elapsedMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0)
              .count();
      expect(elapsedMs < 2000, "service remains responsive during outage");

      // Peer available again; keep FAULTED so ICP auto-reconnect can connect().
      mockRuntime->clearForcedOutage();
      bool recovered = false;
      bool faultHistoryKept = false;
      bool recoveryEvent = false;
      for (int i = 0; i < 60 && !recovered; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (svc.manager().adapter("mock-fault")->connectionState()
            == virtual_factory::ConnectionState::Connected)
        {
          recovered = true;
        }
      }
      expect(recovered, "automatic reconnect restores CONNECTED without GUI");

      for (const auto &ev : svc.events(200))
      {
        if (ev.adapterId != "mock-fault")
        {
          continue;
        }
        if (ev.eventType == "communication_fault")
        {
          faultHistoryKept = true;
        }
        if (ev.eventType == "communication_recovered" || ev.recovery == "successful")
        {
          recoveryEvent = true;
        }
      }
      expect(faultHistoryKept, "fault history survives recovery");
      expect(recoveryEvent, "recovery event recorded");

      bool alarmCleared = true;
      for (const auto &alarm : svc.diagnosticsReport().activeAlarms)
      {
        if (alarm.sourceId == "mock-fault")
        {
          alarmCleared = false;
        }
      }
      expect(alarmCleared, "active alarm cleared after recovery");

      // Second outage/recovery cycle.
      mockRuntime->simulateCommunicationFailure(
          "OPC UA read failed for ns=2;s=MotorSpeed: BadConnectionClosed");
      bool secondFault = false;
      for (int i = 0; i < 50 && !secondFault; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::size_t faults = 0;
        for (const auto &ev : svc.events(200))
        {
          if (ev.adapterId == "mock-fault" && ev.eventType == "communication_fault")
          {
            ++faults;
          }
        }
        if (faults >= 2)
        {
          secondFault = true;
        }
      }
      expect(secondFault, "second outage creates another historical fault event");
      mockRuntime->clearForcedOutage();
      bool secondRecovery = false;
      for (int i = 0; i < 60 && !secondRecovery; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (svc.manager().adapter("mock-fault")->connectionState()
            == virtual_factory::ConnectionState::Connected)
        {
          secondRecovery = true;
        }
      }
      expect(secondRecovery, "second automatic recovery succeeds");

      std::size_t recoveries = 0;
      std::size_t faults = 0;
      for (const auto &ev : svc.events(300))
      {
        if (ev.adapterId != "mock-fault")
        {
          continue;
        }
        if (ev.eventType == "communication_fault")
        {
          ++faults;
        }
        if (ev.eventType == "communication_recovered" || ev.recovery == "successful")
        {
          ++recoveries;
        }
      }
      expect(faults >= 2, "repeated outages keep fault history");
      expect(recoveries >= 2, "repeated recoveries keep recovery history");

      svc.stop();
      ::unlink(faultPath.c_str());
    }
  }


  {
    // Real open62541 outage: durable fault event + automatic recovery.
    const std::string opcuaPath = tempConfigPath() + "-opcua-outage.json";
    ::unlink(opcuaPath.c_str());
    virtual_factory::test::OpcUaTestServer server;
    expect(server.start(), "open62541 fixture starts for outage test");
    if (server.port() != 0)
    {
      ApplicationService svc(opcuaPath);
      svc.start();

      virtual_factory::icp::AdapterConfigRecord opcua;
      opcua.adapterId = "opcua-outage";
      opcua.protocol = "opcua";
      opcua.enabled = true;
      opcua.connection.endpointUrl = server.endpointUrl();
      opcua.connection.timeoutMs = 1000;
      virtual_factory::icp::EquipmentMappingRecord eq;
      eq.equipmentId = virtual_factory::test::OpcUaTestServer::kMixerId;
      eq.type = "mixer";
      eq.capabilities = {"start", "stop"};
      virtual_factory::icp::TelemetryMappingRecord tel;
      tel.name = "speed";
      tel.address =
          std::string("ns=1;s=") + virtual_factory::test::OpcUaTestServer::kMixerSpeedActual;
      tel.namespaceIndex = 1;
      eq.telemetry.push_back(tel);
      opcua.equipment.push_back(eq);
      expect(svc.upsertAdapterConfig(opcua).ok, "upsert opcua-outage");
      expect(svc.connectAdapter("opcua-outage").ok, "connect opcua-outage");

      bool healthy = false;
      for (int i = 0; i < 40 && !healthy; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (svc.manager().adapter("opcua-outage") != nullptr
            && svc.manager().adapter("opcua-outage")->connectionState()
                   == virtual_factory::ConnectionState::Connected)
        {
          healthy = true;
        }
      }
      expect(healthy, "opcua-outage CONNECTED with live open62541 server");

      server.stop();

      bool sawFault = false;
      const auto tFault0 = std::chrono::steady_clock::now();
      for (int i = 0; i < 50 && !sawFault; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Service must remain responsive while the peer is gone.
        (void)svc.status();
        (void)svc.diagnosticsReport();
        for (const auto &ev : svc.events(100))
        {
          if (ev.adapterId == "opcua-outage"
              && (ev.eventType == "communication_fault" || ev.newState == "FAULTED")
              && (ev.level == "error" || ev.eventType == "communication_fault"))
          {
            sawFault = true;
            expect(!ev.errorDetails.empty() || !ev.reason.empty(),
                   "opcua fault event retains protocol error");
          }
        }
      }
      const auto faultDetectMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - tFault0)
              .count();
      expect(sawFault, "killing open62541 creates durable communication_fault event");
      expect(faultDetectMs < 8000, "fault detection remains bounded");
      expect(svc.manager().adapter("opcua-outage")->connectionState()
                 == virtual_factory::ConnectionState::Faulted,
             "opcua-outage FAULTED after server kill");
      const auto icpDuring = svc.diagnosticsReport().icp.overallHealth;
      expect(icpDuring == "HEALTHY" || icpDuring == "DEGRADED",
             "ICP remains healthy during OPC UA outage");

      expect(server.start(), "restart open62541 for auto-recovery");
      bool recovered = false;
      for (int i = 0; i < 80 && !recovered; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        (void)svc.status();
        if (svc.manager().adapter("opcua-outage")->connectionState()
            == virtual_factory::ConnectionState::Connected)
        {
          recovered = true;
        }
      }
      expect(recovered, "OPC UA auto-reconnect restores CONNECTED without GUI Reconnect");

      bool faultKept = false;
      bool recoverySeen = false;
      for (const auto &ev : svc.events(200))
      {
        if (ev.adapterId != "opcua-outage")
        {
          continue;
        }
        if (ev.eventType == "communication_fault")
        {
          faultKept = true;
        }
        if (ev.eventType == "communication_recovered" || ev.recovery == "successful")
        {
          recoverySeen = true;
        }
      }
      expect(faultKept, "OPC UA fault history retained after recovery");
      expect(recoverySeen, "OPC UA recovery event recorded");

      svc.stop();
    }
    ::unlink(opcuaPath.c_str());
  }


  {
    // Scenario A: OPC UA DOWN BEFORE ICP START — ICP must become available,
    // then auto-recover when the fixture starts (no ICP restart, no GUI Connect).
    // Continues into a runtime outage cycle (scenario B) on the same session.
    const std::string startupPath = tempConfigPath() + "-opcua-startup-down.json";
    ::unlink(startupPath.c_str());

    virtual_factory::test::OpcUaTestServer server;
    // Reserve a port for the fixture but do not start it yet.
    {
      virtual_factory::test::OpcUaTestServer probe;
      expect(probe.start(), "probe free OPC UA port");
      server.setPort(probe.port());
      probe.stop();
    }
    const std::string endpoint = server.endpointUrl();

    {
      ApplicationService writer(startupPath);
      virtual_factory::icp::AdapterConfigRecord opcua;
      opcua.adapterId = "opcua-startup-down";
      opcua.protocol = "opcua";
      opcua.enabled = true;
      opcua.connection.endpointUrl = endpoint;
      opcua.connection.timeoutMs = 1000;
      virtual_factory::icp::EquipmentMappingRecord eq;
      eq.equipmentId = virtual_factory::test::OpcUaTestServer::kMixerId;
      eq.type = "mixer";
      eq.capabilities = {"start", "stop"};
      virtual_factory::icp::TelemetryMappingRecord tel;
      tel.name = "speed";
      tel.address =
          std::string("ns=1;s=") + virtual_factory::test::OpcUaTestServer::kMixerSpeedActual;
      tel.namespaceIndex = 1;
      eq.telemetry.push_back(tel);
      opcua.equipment.push_back(eq);
      expect(writer.upsertAdapterConfig(opcua).ok, "persist opcua-startup-down config");
      expect(writer.saveConfiguration().ok, "save opcua-startup-down config");
    }

    const auto tStart = std::chrono::steady_clock::now();
    ApplicationService svc(startupPath);
    svc.start();
    expect(svc.loadConfiguration().ok, "load config while OPC UA peer is down");
    const int httpPort = freePort() + 17;
    HttpApiServer api(svc, "icp/gui", "127.0.0.1", httpPort);
    expect(api.start(), "HTTP starts while OPC UA peer is down");
    const auto startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - tStart)
                               .count();
    expect(startupMs < 3000, "ICP startup remains bounded with OPC UA down");

    httplib::Client client("127.0.0.1", httpPort);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);
    auto diagResp = client.Get("/api/v1/diagnostics");
    expect(diagResp && diagResp->status == 200,
           "diagnostics HTTP responds while OPC UA unavailable");
    if (diagResp && diagResp->status == 200)
    {
      const json body = json::parse(diagResp->body, nullptr, false);
      expect(!body.is_discarded(), "diagnostics JSON parses");
      if (!body.is_discarded())
      {
        const std::string health =
            body.value("icp", json::object()).value("overallHealth", "");
        expect(health == "HEALTHY" || health == "DEGRADED",
               "ICP health stays up with OPC UA down at startup");
      }
    }
    expect(svc.diagnosticsReport().icp.overallHealth == "HEALTHY"
               || svc.diagnosticsReport().icp.overallHealth == "DEGRADED",
           "ICP overallHealth independent of OPC UA availability");

    virtual_factory::IndustrialAdapter *runtime = nullptr;
    bool sawFaulted = false;
    for (int i = 0; i < 80 && !sawFaulted; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      (void)svc.diagnosticsReport();
      runtime = svc.manager().adapter("opcua-startup-down");
      if (runtime == nullptr)
      {
        continue;
      }
      // Wait for deferred connect to FAULT — proves peer-down is an adapter
      // condition and seeds durable communication_fault history.
      if (runtime->connectionState() == virtual_factory::ConnectionState::Faulted)
      {
        sawFaulted = true;
      }
    }
    expect(sawFaulted,
           "OPC UA adapter FAULTED while peer down — ICP remains operational");
    expect(runtime != nullptr, "enabled OPC UA adapter was materialized at load");

    expect(server.start(), "start open62541 fixture for startup-down recovery");

    bool recovered = false;
    for (int i = 0; i < 100 && !recovered; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto *r = svc.manager().adapter("opcua-startup-down");
      if (r != nullptr
          && r->connectionState() == virtual_factory::ConnectionState::Connected)
      {
        recovered = true;
      }
      auto diag = client.Get("/api/v1/diagnostics");
      expect(diag && diag->status == 200, "diagnostics stays responsive during recovery");
    }
    expect(recovered, "startup-down OPC UA auto-recovers to CONNECTED without ICP restart");

    bool faultHistory = false;
    bool recoveryEvent = false;
    for (const auto &ev : svc.events(300))
    {
      if (ev.adapterId != "opcua-startup-down")
      {
        continue;
      }
      if (ev.eventType == "communication_fault")
      {
        faultHistory = true;
      }
      if (ev.eventType == "communication_recovered" || ev.recovery == "successful")
      {
        recoveryEvent = true;
      }
    }
    expect(faultHistory, "startup-down retains communication_fault history");
    expect(recoveryEvent, "startup-down records recovery event");

    // Runtime outage after startup recovery (scenario B direction).
    server.stop();
    bool faultedAgain = false;
    for (int i = 0; i < 50 && !faultedAgain; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      (void)svc.diagnosticsReport();
      auto diag = client.Get("/api/v1/diagnostics");
      expect(diag && diag->status == 200, "ICP HTTP responsive after peer kill");
      auto *r = svc.manager().adapter("opcua-startup-down");
      if (r != nullptr && r->connectionState() == virtual_factory::ConnectionState::Faulted)
      {
        faultedAgain = true;
      }
    }
    expect(faultedAgain, "second outage FAULTs adapter while ICP stays up");

    expect(server.start(), "restart fixture for second recovery");
    bool recoveredAgain = false;
    for (int i = 0; i < 100 && !recoveredAgain; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto *r = svc.manager().adapter("opcua-startup-down");
      if (r != nullptr
          && r->connectionState() == virtual_factory::ConnectionState::Connected)
      {
        recoveredAgain = true;
      }
    }
    expect(recoveredAgain, "second automatic recovery after startup-down cycle");

    std::size_t faults = 0;
    for (const auto &ev : svc.events(400))
    {
      if (ev.adapterId == "opcua-startup-down" && ev.eventType == "communication_fault")
      {
        ++faults;
      }
    }
    expect(faults >= 2, "repeated faults remain in event history");

    api.stop();
    svc.stop();
    ::unlink(startupPath.c_str());
  }

  {
    // Soft application-level read failure: CONNECTED must not imply HEALTHY.
    const std::string softPath = tempConfigPath() + "-soft-read.json";
    ::unlink(softPath.c_str());
    ApplicationService svc(softPath);
    svc.start();

    virtual_factory::icp::AdapterConfigRecord mock;
    mock.adapterId = "mock-soft-read";
    mock.protocol = "mock";
    mock.enabled = true;
    mock.description = "soft read failure health";
    virtual_factory::icp::EquipmentMappingRecord eq;
    eq.equipmentId = "EQ-SOFT";
    eq.type = "motor";
    eq.capabilities = {"start", "stop"};
    mock.equipment.push_back(eq);
    expect(svc.upsertAdapterConfig(mock).ok, "upsert mock-soft-read");
    expect(svc.connectAdapter("mock-soft-read").ok, "connect mock-soft-read");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto *runtime = dynamic_cast<virtual_factory::MockIndustrialAdapter *>(
        svc.manager().adapter("mock-soft-read"));
    expect(runtime != nullptr, "mock-soft-read runtime present");
    if (runtime != nullptr)
    {
      // TEST 1: valid telemetry path → CONNECTED + HEALTHY
      {
        const auto report = svc.diagnosticsReport();
        bool found = false;
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId != "mock-soft-read")
          {
            continue;
          }
          found = true;
          expect(view.adapter.connectionState == "CONNECTED",
                 "TEST1 communication CONNECTED");
          expect(view.session.health == "HEALTHY",
                 "TEST1 health HEALTHY with successful telemetry");
          expect(view.session.earlyWarning.empty(),
                 "TEST1 no early warning while healthy");
        }
        expect(found, "TEST1 mock-soft-read in diagnostics");
      }

      const std::string softError =
          "OPC UA read failed for ns=2;s=MotorSpeed: BadNodeIdUnknown";
      runtime->simulateApplicationReadFailure(softError);

      bool degraded = false;
      for (int i = 0; i < 40 && !degraded; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto report = svc.diagnosticsReport();
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId == "mock-soft-read"
              && view.session.health == "DEGRADED")
          {
            degraded = true;
          }
        }
      }
      expect(degraded, "TEST2 soft read failure reaches DEGRADED");

      {
        const auto report = svc.diagnosticsReport();
        bool foundAdapter = false;
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId != "mock-soft-read")
          {
            continue;
          }
          foundAdapter = true;
          expect(view.adapter.connectionState == "CONNECTED",
                 "TEST2 session stays CONNECTED on soft read failure");
          expect(view.session.health == "DEGRADED",
                 "TEST2 health DEGRADED (not HEALTHY, not FAULTED)");
          expect(view.adapter.lastError.find("BadNodeIdUnknown") != std::string::npos,
                 "TEST2 lastError retains BadNodeIdUnknown");
          expect(!view.session.earlyWarning.empty(),
                 "TEST2 earlyWarning set for operator attention");
          expect(view.session.healthReason.find("application-level") != std::string::npos
                     || view.session.healthReason.find("BadNodeIdUnknown")
                            != std::string::npos,
                 "TEST2 healthReason explains soft failure");
        }
        expect(foundAdapter, "TEST2 adapter present while degraded");

        bool foundEq = false;
        for (const auto &snap : report.equipment)
        {
          if (snap.equipmentId != "EQ-SOFT")
          {
            continue;
          }
          foundEq = true;
          expect(snap.communicationState == virtual_factory::ConnectionState::Connected,
                 "TEST2 equipment communication stays Connected");
          expect(snap.lastError.find("BadNodeIdUnknown") != std::string::npos,
                 "TEST2 equipment lastError reflects soft failure");
        }
        expect(foundEq, "TEST2 equipment snapshot present");
        expect(report.system.degradedAdapters >= 1
                   || report.system.degradedEquipment >= 1,
               "TEST2 diagnostics counts reflect degradation");
        expect(report.system.overallHealth == "DEGRADED"
                   || !report.activeAlarms.empty(),
               "TEST2 overall attention/degraded consistent");

        bool sawCommAlarm = false;
        for (const auto &alarm : report.activeAlarms)
        {
          if (alarm.category == "COMMUNICATION"
              && (alarm.sourceId == "mock-soft-read" || alarm.sourceId == "EQ-SOFT"))
          {
            sawCommAlarm = true;
            expect(alarm.severity == "WARNING" || alarm.severity == "ERROR",
                   "TEST2 soft-failure alarm severity is set");
          }
        }
        expect(sawCommAlarm, "TEST2 active COMMUNICATION alarm for soft failure");
      }

      // TEST 7: persistent soft failure must not flood health events.
      std::size_t healthEventsBefore = 0;
      for (const auto &ev : svc.events(200))
      {
        if (ev.adapterId == "mock-soft-read" && ev.eventType == "health_changed"
            && ev.newHealth == "DEGRADED")
        {
          ++healthEventsBefore;
        }
      }
      expect(healthEventsBefore >= 1, "TEST7 at least one DEGRADED health_changed");
      std::this_thread::sleep_for(std::chrono::milliseconds(900));
      std::size_t healthEventsAfter = 0;
      for (const auto &ev : svc.events(200))
      {
        if (ev.adapterId == "mock-soft-read" && ev.eventType == "health_changed"
            && ev.newHealth == "DEGRADED")
        {
          ++healthEventsAfter;
        }
      }
      expect(healthEventsAfter == healthEventsBefore,
             "TEST7 no health_changed flood on persistent soft failure");

      // TEST 3: clear soft failure while session remains connected → HEALTHY.
      runtime->clearApplicationReadFailure();
      bool recovered = false;
      for (int i = 0; i < 40 && !recovered; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto report = svc.diagnosticsReport();
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId == "mock-soft-read"
              && view.session.health == "HEALTHY"
              && view.adapter.connectionState == "CONNECTED")
          {
            recovered = true;
          }
        }
      }
      expect(recovered, "TEST3 health returns HEALTHY after soft failure clears");
      {
        const auto report = svc.diagnosticsReport();
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId != "mock-soft-read")
          {
            continue;
          }
          expect(view.session.earlyWarning.empty(),
                 "TEST3 earlyWarning cleared after recovery");
          expect(view.adapter.lastError.empty(),
                 "TEST3 lastError cleared after recovery");
        }
        bool softAlarmRemains = false;
        for (const auto &alarm : report.activeAlarms)
        {
          if ((alarm.sourceId == "mock-soft-read" || alarm.sourceId == "EQ-SOFT")
              && alarm.category == "COMMUNICATION")
          {
            softAlarmRemains = true;
          }
        }
        expect(!softAlarmRemains, "TEST3 soft-failure COMMUNICATION alarm cleared");
      }

      expect(runtime->connectionState() == virtual_factory::ConnectionState::Connected,
             "soft-failure path never leaves Connected for reconnect");
    }

    svc.stop();
    ::unlink(softPath.c_str());
  }

  {
    // OPC UA BadNodeIdUnknown: CONNECTED + DEGRADED (not transport FAULTED).
    virtual_factory::test::OpcUaTestServer server;
    expect(server.start(), "soft-fail OPC UA fixture starts");
    if (server.port() != 0)
    {
      const std::string opcuaPath = tempConfigPath() + "-opcua-soft.json";
      ::unlink(opcuaPath.c_str());
      ApplicationService svc(opcuaPath);
      svc.start();

      virtual_factory::icp::AdapterConfigRecord opcua;
      opcua.adapterId = "opcua-soft-node";
      opcua.protocol = "opcua";
      opcua.enabled = true;
      opcua.connection.endpointUrl = server.endpointUrl();
      opcua.connection.timeoutMs = 1000;
      virtual_factory::icp::EquipmentMappingRecord eq;
      eq.equipmentId = virtual_factory::test::OpcUaTestServer::kMixerId;
      eq.type = "mixer";
      eq.capabilities = {"start", "stop"};
      virtual_factory::icp::TelemetryMappingRecord tel;
      tel.name = "speed";
      tel.unit = "rpm";
      tel.namespaceIndex = 1;
      tel.address = "ns=1;s=Does.Not.Exist.MotorSpeed";
      eq.telemetry.push_back(tel);
      opcua.equipment.push_back(eq);
      expect(svc.upsertAdapterConfig(opcua).ok, "upsert opcua-soft-node");
      expect(svc.connectAdapter("opcua-soft-node").ok, "connect opcua-soft-node");

      bool sawDegraded = false;
      for (int i = 0; i < 50 && !sawDegraded; ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto report = svc.diagnosticsReport();
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId != "opcua-soft-node")
          {
            continue;
          }
          if (view.adapter.connectionState == "CONNECTED"
              && view.session.health == "DEGRADED"
              && view.adapter.lastError.find("BadNodeIdUnknown") != std::string::npos)
          {
            sawDegraded = true;
          }
        }
      }
      expect(sawDegraded,
             "OPC UA invalid NodeId → CONNECTED + DEGRADED + BadNodeIdUnknown");

      auto *rt = svc.manager().adapter("opcua-soft-node");
      expect(rt != nullptr, "opcua-soft-node runtime present");
      if (rt != nullptr)
      {
        expect(rt->connectionState() == virtual_factory::ConnectionState::Connected,
               "invalid NodeId must not FAULT the OPC UA session");
      }

      // Upsert while still CONNECTED — GUI NodeId correction must rematerialize
      // runtime bindings and clear DEGRADED without an explicit disconnect.
      tel.address = std::string("ns=1;s=")
          + virtual_factory::test::OpcUaTestServer::kMixerSpeedActual;
      eq.telemetry.clear();
      eq.telemetry.push_back(tel);
      opcua.equipment.clear();
      opcua.equipment.push_back(eq);
      expect(svc.upsertAdapterConfig(opcua).ok, "upsert fixed NodeId while connected");

      bool healthyAgain = false;
      bool alarmCleared = false;
      for (int i = 0; i < 50 && !(healthyAgain && alarmCleared); ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto report = svc.diagnosticsReport();
        for (const auto &view : report.adapters)
        {
          if (view.adapter.adapterId == "opcua-soft-node"
              && view.adapter.connectionState == "CONNECTED"
              && view.session.health == "HEALTHY"
              && view.adapter.lastError.empty())
          {
            healthyAgain = true;
          }
        }
        bool softAlarmRemains = false;
        for (const auto &alarm : report.activeAlarms)
        {
          if ((alarm.sourceId == "opcua-soft-node"
               || alarm.sourceId == virtual_factory::test::OpcUaTestServer::kMixerId)
              && alarm.category == "COMMUNICATION")
          {
            softAlarmRemains = true;
          }
        }
        alarmCleared = !softAlarmRemains;
      }
      expect(healthyAgain,
             "upsert-while-connected rematerializes NodeId → CONNECTED + HEALTHY");
      expect(alarmCleared,
             "upsert-while-connected clears COMMUNICATION soft-failure alarms");

      svc.stop();
      ::unlink(opcuaPath.c_str());
    }
  }

  if (failures == 0)
  {
    std::cout << "icp_application_api_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_application_api_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
