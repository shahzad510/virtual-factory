#include "opcua_test_server.hh"

#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
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
    expect(!connected.ok, "connect to dead endpoint fails");
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
    view = life.adapter("opcua-life");
    expect(view.has_value() && view->connectionState == "DISCONNECTED",
           "explicit disconnect yields DISCONNECTED");
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
      expect(life.status().runtimeAdapterCount == 1,
             "runtimeAdapters stays 1 across failed reconnect attempts");
      expect(life.disconnectAdapter("opcua-life").ok, "disconnect between attempts");
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
    expect(svc.reconnectAdapter("mock-cmd").ok, "explicit reconnect mock-cmd");
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

  if (failures == 0)
  {
    std::cout << "icp_application_api_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_application_api_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
