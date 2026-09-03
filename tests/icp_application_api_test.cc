#include <virtual_factory/icp/app/ApplicationService.hh>
#include <virtual_factory/icp/app/HttpApiServer.hh>

#include <chrono>
#include <cstdlib>
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
    }
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

  if (failures == 0)
  {
    std::cout << "icp_application_api_test: OK" << std::endl;
    return 0;
  }
  std::cerr << "icp_application_api_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
