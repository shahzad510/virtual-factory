#include <virtual_factory/icp/app/HttpApiServer.hh>

#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include <httplib.h>

namespace virtual_factory
{
namespace icp
{

namespace
{

using json = nlohmann::json;

std::string operationalStateName(OperationalState state)
{
  switch (state)
  {
    case OperationalState::Running:
      return "RUNNING";
    case OperationalState::Stopped:
      return "STOPPED";
  }
  return "STOPPED";
}

std::string connectionStateName(ConnectionState state)
{
  switch (state)
  {
    case ConnectionState::Connected:
      return "CONNECTED";
    case ConnectionState::Disconnected:
      return "DISCONNECTED";
    case ConnectionState::Faulted:
      return "FAULTED";
  }
  return "DISCONNECTED";
}

std::string iso8601(const std::chrono::system_clock::time_point &tp)
{
  if (tp.time_since_epoch().count() == 0)
  {
    return "";
  }
  const std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

json configResultToJson(const ConfigResult &result)
{
  json issues = json::array();
  for (const ConfigIssue &issue : result.issues)
  {
    issues.push_back({{"path", issue.path}, {"message", issue.message}});
  }
  return {
      {"ok", result.ok},
      {"message", result.message},
      {"issues", issues},
  };
}

json managerResultToJson(const AdapterManagerResult &result)
{
  return {{"ok", result.ok}, {"message", result.message}};
}

json equipmentToJson(const EquipmentSnapshot &snap)
{
  const std::string commState = connectionStateName(snap.communicationState);
  std::string commDisplay = commState;
  if (snap.protocol == "mock" && commState == "CONNECTED")
  {
    commDisplay = "SIMULATED_ACTIVE";
  }
  json telemetry = json::array();
  for (const CachedTelemetryPoint &point : snap.telemetry)
  {
    telemetry.push_back(
        {{"name", point.name}, {"value", point.value}, {"unit", point.unit}});
  }
  return {
      {"equipmentId", snap.equipmentId},
      {"type", snap.type},
      {"adapterId", snap.adapterId},
      {"protocol", snap.protocol},
      {"communicationState", commState},
      {"communicationStateDisplay", commDisplay},
      {"machineState", operationalStateName(snap.operationalState)},
      {"machineFault", snap.machineFault},
      {"stale", snap.stale},
      {"lastError", snap.lastError},
      {"observedAtUtc", iso8601(snap.observedAtUtc)},
      {"telemetry", telemetry},
  };
}

json redactCredentials(json document)
{
  if (!document.contains("adapters") || !document["adapters"].is_array())
  {
    return document;
  }
  for (auto &adapter : document["adapters"])
  {
    if (!adapter.contains("credentials") || !adapter["credentials"].is_object())
    {
      continue;
    }
    auto &creds = adapter["credentials"];
    // Keep references only; strip any accidental secret-like keys.
    json safe = json::object();
    if (creds.contains("username"))
    {
      safe["username"] = creds["username"];
    }
    if (creds.contains("passwordRef"))
    {
      safe["passwordRef"] = creds["passwordRef"];
    }
    if (creds.contains("tokenRef"))
    {
      safe["tokenRef"] = creds["tokenRef"];
    }
    adapter["credentials"] = safe;
  }
  return document;
}

json parseJsonBody(const std::string &body, std::string *error)
{
  if (body.empty())
  {
    *error = "request body is empty";
    return nullptr;
  }
  try
  {
    return json::parse(body);
  }
  catch (const std::exception &ex)
  {
    *error = std::string("invalid JSON: ") + ex.what();
    return nullptr;
  }
}

ConfigResult parseAdapterBody(const std::string &body, AdapterConfigRecord *out)
{
  IcpConfigurationDocument wrapper;
  wrapper.name = "api-adapter-upsert";
  wrapper.adapters.push_back({});
  // Reuse document parser by wrapping the adapter object.
  std::string error;
  json root = parseJsonBody(body, &error);
  if (root.is_null())
  {
    ConfigResult result;
    result.ok = false;
    result.message = error;
    return result;
  }
  json document = {
      {"schema", IcpConfigurationDocument::kSchemaId},
      {"version", IcpConfigurationDocument::kCurrentVersion},
      {"name", "api-adapter-upsert"},
      {"adapters", json::array({root})},
  };
  ConfigResult parsed =
      JsonFileConfigurationRepository::parseText(document.dump(), &wrapper);
  if (!parsed.ok || wrapper.adapters.empty())
  {
    return parsed;
  }
  *out = wrapper.adapters.front();
  ConfigResult ok;
  ok.ok = true;
  ok.message = "ok";
  return ok;
}

void setJson(httplib::Response &res, int status, const json &body)
{
  res.status = status;
  res.set_content(body.dump(2), "application/json");
}

}  // namespace

class HttpApiServer::Impl
{
public:
  ApplicationService &service;
  std::string staticRoot;
  std::string host;
  int port;
  httplib::Server server;
  std::thread thread;
  std::atomic<bool> running{false};

  Impl(ApplicationService &svc, std::string root, std::string bindHost, int bindPort)
      : service(svc)
      , staticRoot(std::move(root))
      , host(std::move(bindHost))
      , port(bindPort)
  {
  }

  void registerRoutes()
  {
    server.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                {"Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS"},
                                {"Access-Control-Allow-Headers", "Content-Type"}});

    server.Options(R"(/api/v1/.*)", [](const httplib::Request &, httplib::Response &res) {
      res.status = 204;
    });

    server.Get("/api/v1/status", [this](const httplib::Request &, httplib::Response &res) {
      const ApplicationStatus st = service.status();
      std::map<std::string, std::size_t> protocols;
      for (const RuntimeAdapterView &adapter : service.adapters())
      {
        ++protocols[adapter.protocol];
      }
      json protocolDistribution = json::object();
      for (const auto &entry : protocols)
      {
        protocolDistribution[entry.first] = entry.second;
      }
      const auto recent = service.events(10);
      json recentEvents = json::array();
      for (const ApplicationEvent &event : recent)
      {
        recentEvents.push_back(
            {{"atUtc", iso8601(event.atUtc)},
             {"level", event.level},
             {"category", event.category},
             {"message", event.message},
             {"adapterId", event.adapterId},
             {"equipmentId", event.equipmentId}});
      }
      setJson(
          res,
          200,
          {{"product", st.product},
           {"version", st.version},
           {"apiVersion", st.apiVersion},
           {"mesDependency", st.mesDependency},
           {"cicDependency", st.cicDependency},
           {"schedulerRunning", st.schedulerRunning},
           {"configuredAdapterCount", st.configuredAdapterCount},
           {"runtimeAdapterCount", st.runtimeAdapterCount},
           {"connectedAdapters", st.connectedAdapters},
           {"disconnectedAdapters", st.disconnectedAdapters},
           {"faultedAdapters", st.faultedAdapters},
           {"equipmentCount", st.equipmentCount},
           {"connectedEquipment", st.equipmentCount - st.staleEquipment},
           {"staleEquipment", st.staleEquipment},
           {"machineFaultEquipment", st.machineFaultEquipment},
           {"activeCommunications", st.connectedAdapters},
           {"protocolDistribution", protocolDistribution},
           {"configurationPath", st.configurationPath},
           {"configurationName", st.configurationName},
           {"configurationLoaded", st.configurationLoaded},
           {"configurationLoadState", st.configurationLoadState},
           {"recentEvents", recentEvents}});
    });

    server.Get("/api/v1/protocols", [this](const httplib::Request &, httplib::Response &res) {
      json arr = json::array();
      for (const ProtocolCapability &cap : service.protocols())
      {
        arr.push_back(
            {{"id", cap.id},
             {"label", cap.label},
             {"configurableWithoutHardware", cap.configurableWithoutHardware},
             {"requiresHilscherHardware", cap.requiresHilscherHardware}});
      }
      setJson(res, 200, {{"protocols", arr}});
    });

    server.Get("/api/v1/configuration", [this](const httplib::Request &, httplib::Response &res) {
      const std::string text =
          JsonFileConfigurationRepository::toJsonText(service.configuration());
      json document = json::parse(text);
      setJson(res, 200, redactCredentials(document));
    });

    server.Put("/api/v1/configuration", [this](const httplib::Request &req, httplib::Response &res) {
      IcpConfigurationDocument document;
      ConfigResult parsed =
          JsonFileConfigurationRepository::parseText(req.body, &document);
      if (!parsed.ok)
      {
        setJson(res, 400, configResultToJson(parsed));
        return;
      }
      ConfigResult result = service.setConfiguration(std::move(document));
      setJson(res, result.ok ? 200 : 400, configResultToJson(result));
    });

    server.Post(
        "/api/v1/configuration/validate",
        [this](const httplib::Request &, httplib::Response &res) {
          setJson(res, 200, configResultToJson(service.validateConfiguration()));
        });

    server.Post(
        "/api/v1/configuration/save",
        [this](const httplib::Request &, httplib::Response &res) {
          ConfigResult result = service.saveConfiguration();
          setJson(res, result.ok ? 200 : 400, configResultToJson(result));
        });

    server.Post(
        "/api/v1/configuration/load",
        [this](const httplib::Request &, httplib::Response &res) {
          ConfigResult result = service.loadConfiguration();
          setJson(res, result.ok ? 200 : 400, configResultToJson(result));
        });

    server.Post(
        "/api/v1/configuration/import",
        [this](const httplib::Request &req, httplib::Response &res) {
          ConfigResult result = service.importConfigurationJson(req.body);
          setJson(res, result.ok ? 200 : 400, configResultToJson(result));
        });

    server.Get(
        "/api/v1/configuration/export",
        [this](const httplib::Request &, httplib::Response &res) {
          const std::string text = service.exportConfigurationJson();
          json document = json::parse(text);
          setJson(res, 200, redactCredentials(document));
        });

    server.Get("/api/v1/adapters", [this](const httplib::Request &, httplib::Response &res) {
      json arr = json::array();
      for (const RuntimeAdapterView &view : service.adapters())
      {
        arr.push_back(
            {{"adapterId", view.adapterId},
             {"protocol", view.protocol},
             {"configured", view.configured},
             {"enabled", view.enabled},
             {"runtimePresent", view.runtimePresent},
             {"connectionState", view.connectionState},
             {"connectionStateDisplay", view.connectionStateDisplay.empty()
                                              ? view.connectionState
                                              : view.connectionStateDisplay},
             {"lastError", view.lastError},
             {"description", view.description},
             {"equipmentCount", view.equipmentCount}});
      }
      setJson(res, 200, {{"adapters", arr}});
    });

    server.Get(
        R"(/api/v1/adapters/([^/]+))",
        [this](const httplib::Request &req, httplib::Response &res) {
          const std::string id = req.matches[1];
          auto view = service.adapter(id);
          if (!view)
          {
            setJson(res, 404, {{"ok", false}, {"message", "adapter not found"}});
            return;
          }
          const AdapterConfigRecord *record = service.catalog().adapter(id);
          json body = {
              {"adapterId", view->adapterId},
              {"protocol", view->protocol},
              {"configured", view->configured},
              {"enabled", view->enabled},
              {"runtimePresent", view->runtimePresent},
              {"connectionState", view->connectionState},
              {"connectionStateDisplay",
               view->connectionStateDisplay.empty() ? view->connectionState
                                                      : view->connectionStateDisplay},
              {"lastError", view->lastError},
              {"description", view->description},
              {"equipmentCount", view->equipmentCount},
          };
          if (record != nullptr)
          {
            IcpConfigurationDocument wrapper;
            wrapper.name = "adapter-view";
            wrapper.adapters.push_back(*record);
            json document = json::parse(
                JsonFileConfigurationRepository::toJsonText(wrapper));
            document = redactCredentials(document);
            if (!document["adapters"].empty())
            {
              body["configuration"] = document["adapters"].front();
            }
          }
          setJson(res, 200, body);
        });

    server.Post("/api/v1/adapters", [this](const httplib::Request &req, httplib::Response &res) {
      AdapterConfigRecord adapter;
      ConfigResult parsed = parseAdapterBody(req.body, &adapter);
      if (!parsed.ok)
      {
        setJson(res, 400, configResultToJson(parsed));
        return;
      }
      ConfigResult result = service.upsertAdapterConfig(std::move(adapter));
      setJson(res, result.ok ? 200 : 400, configResultToJson(result));
    });

    server.Put(
        R"(/api/v1/adapters/([^/]+))",
        [this](const httplib::Request &req, httplib::Response &res) {
          AdapterConfigRecord adapter;
          ConfigResult parsed = parseAdapterBody(req.body, &adapter);
          if (!parsed.ok)
          {
            setJson(res, 400, configResultToJson(parsed));
            return;
          }
          if (adapter.adapterId.empty())
          {
            adapter.adapterId = req.matches[1];
          }
          if (adapter.adapterId != req.matches[1].str())
          {
            setJson(
                res,
                400,
                {{"ok", false},
                 {"message", "adapterId in body must match path"},
                 {"issues", json::array()}});
            return;
          }
          ConfigResult result = service.upsertAdapterConfig(std::move(adapter));
          setJson(res, result.ok ? 200 : 400, configResultToJson(result));
        });

    server.Delete(
        R"(/api/v1/adapters/([^/]+))",
        [this](const httplib::Request &req, httplib::Response &res) {
          ConfigResult result = service.removeAdapterConfig(req.matches[1]);
          setJson(res, result.ok ? 200 : 400, configResultToJson(result));
        });

    server.Post(
        R"(/api/v1/adapters/([^/]+)/connect)",
        [this](const httplib::Request &req, httplib::Response &res) {
          AdapterManagerResult result = service.connectAdapter(req.matches[1]);
          setJson(res, result.ok ? 200 : 400, managerResultToJson(result));
        });

    server.Post(
        R"(/api/v1/adapters/([^/]+)/disconnect)",
        [this](const httplib::Request &req, httplib::Response &res) {
          AdapterManagerResult result = service.disconnectAdapter(req.matches[1]);
          setJson(res, result.ok ? 200 : 400, managerResultToJson(result));
        });

    server.Post(
        R"(/api/v1/adapters/([^/]+)/reconnect)",
        [this](const httplib::Request &req, httplib::Response &res) {
          service.disconnectAdapter(req.matches[1]);
          AdapterManagerResult result = service.connectAdapter(req.matches[1]);
          setJson(res, result.ok ? 200 : 400, managerResultToJson(result));
        });

    server.Get("/api/v1/equipment", [this](const httplib::Request &, httplib::Response &res) {
      json arr = json::array();
      for (const EquipmentSnapshot &snap : service.equipment())
      {
        arr.push_back(equipmentToJson(snap));
      }
      setJson(res, 200, {{"equipment", arr}});
    });

    server.Get(
        R"(/api/v1/equipment/([^/]+))",
        [this](const httplib::Request &req, httplib::Response &res) {
          auto snap = service.equipmentById(req.matches[1]);
          if (!snap)
          {
            setJson(res, 404, {{"ok", false}, {"message", "equipment not found"}});
            return;
          }
          setJson(res, 200, equipmentToJson(*snap));
        });

    server.Post(
        R"(/api/v1/equipment/([^/]+)/command)",
        [this](const httplib::Request &req, httplib::Response &res) {
          std::string error;
          json body = parseJsonBody(req.body, &error);
          if (body.is_null())
          {
            setJson(res, 400, {{"ok", false}, {"message", error}});
            return;
          }
          if (!body.contains("command") || !body["command"].is_string())
          {
            setJson(
                res,
                400,
                {{"ok", false}, {"message", "field 'command' is required"}});
            return;
          }
          double parameter = 0.0;
          if (body.contains("parameter") && body["parameter"].is_number())
          {
            parameter = body["parameter"].get<double>();
          }
          const EquipmentCommandResult result = service.executeEquipmentCommand(
              req.matches[1].str(), body["command"].get<std::string>(), parameter);
          setJson(
              res,
              result.ok ? 200 : 400,
              {{"ok", result.ok},
               {"message", result.message},
               {"equipmentId", result.equipmentId},
               {"command", result.command}});
        });

    server.Get("/api/v1/mappings", [this](const httplib::Request &, httplib::Response &res) {
      json adapters = json::array();
      for (const AdapterConfigRecord &adapter : service.configuration().adapters)
      {
        json equipment = json::array();
        for (const EquipmentMappingRecord &eq : adapter.equipment)
        {
          json telemetry = json::array();
          for (const TelemetryMappingRecord &tel : eq.telemetry)
          {
            telemetry.push_back(
                {{"name", tel.name},
                 {"address", tel.address},
                 {"unit", tel.unit},
                 {"valueType", tel.valueType},
                 {"inputByteOffset", tel.inputByteOffset},
                 {"outputByteOffset", tel.outputByteOffset},
                 {"bitOffset", tel.bitOffset},
                 {"direction", "input"}});
          }
          json commands = json::array();
          for (const CommandMappingRecord &cmd : eq.commands)
          {
            commands.push_back(
                {{"command", cmd.command},
                 {"address", cmd.address},
                 {"outputByteOffset", cmd.outputByteOffset},
                 {"bitOffset", cmd.bitOffset},
                 {"valueType", cmd.valueType},
                 {"direction", "output"}});
          }
          equipment.push_back(
              {{"equipmentId", eq.equipmentId},
               {"type", eq.type},
               {"telemetry", telemetry},
               {"commands", commands},
               {"state",
                {{"mapped", eq.state.mapped},
                 {"address", eq.state.address},
                 {"inputByteOffset", eq.state.inputByteOffset},
                 {"valueType", eq.state.valueType}}},
               {"fault",
                {{"mapped", eq.fault.mapped},
                 {"address", eq.fault.address},
                 {"inputByteOffset", eq.fault.inputByteOffset},
                 {"valueType", eq.fault.valueType}}}});
        }
        adapters.push_back(
            {{"adapterId", adapter.adapterId},
             {"protocol", adapter.protocol},
             {"equipment", equipment}});
      }
      setJson(res, 200, {{"mappings", adapters}});
    });

    server.Get("/api/v1/diagnostics", [this](const httplib::Request &, httplib::Response &res) {
      const ApplicationStatus st = service.status();
      const HilscherDiagnosticsView hilscher = service.hilscherDiagnostics();
      json adapters = json::array();
      for (const RuntimeAdapterView &view : service.adapters())
      {
        adapters.push_back(
            {{"adapterId", view.adapterId},
             {"protocol", view.protocol},
             {"description", view.description},
             {"configured", view.configured},
             {"runtimePresent", view.runtimePresent},
             {"connectionState", view.connectionState},
             {"connectionStateDisplay",
              view.connectionStateDisplay.empty() ? view.connectionState
                                                  : view.connectionStateDisplay},
             {"lastError", view.lastError},
             {"enabled", view.enabled},
             {"equipmentCount", view.equipmentCount}});
      }
      json equipment = json::array();
      json stale = json::array();
      for (const EquipmentSnapshot &snap : service.equipment())
      {
        json tel = json::array();
        for (const CachedTelemetryPoint &point : snap.telemetry)
        {
          tel.push_back(
              {{"name", point.name}, {"value", point.value}, {"unit", point.unit}});
        }
        const std::string observed = iso8601(snap.observedAtUtc);
        const json eqEntry =
            {{"equipmentId", snap.equipmentId},
             {"adapterId", snap.adapterId},
             {"protocol", snap.protocol},
             {"type", snap.type},
             {"communicationState", connectionStateName(snap.communicationState)},
             {"communicationStateDisplay",
              snap.protocol == "mock" &&
                      snap.communicationState == ConnectionState::Connected
                  ? "SIMULATED_ACTIVE"
                  : connectionStateName(snap.communicationState)},
             {"machineState", operationalStateName(snap.operationalState)},
             {"machineFault", snap.machineFault},
             {"stale", snap.stale},
             {"lastError", snap.lastError},
             {"observedAtUtc", observed},
             {"lastSuccessfulTelemetryUtc", observed},
             {"telemetry", tel}};
        equipment.push_back(eqEntry);
        if (snap.stale)
        {
          stale.push_back(eqEntry);
        }
      }
      json recentErrors = json::array();
      for (const ApplicationEvent &ev : service.events(100))
      {
        if (ev.level == "error" || ev.level == "warn")
        {
          recentErrors.push_back(
              {{"atUtc", iso8601(ev.atUtc)},
               {"level", ev.level},
               {"category", ev.category},
               {"message", ev.message},
               {"adapterId", ev.adapterId},
               {"equipmentId", ev.equipmentId}});
        }
      }
      json protocolDistribution = json::object();
      {
        std::map<std::string, std::size_t> protocols;
        for (const RuntimeAdapterView &view : service.adapters())
        {
          ++protocols[view.protocol];
        }
        for (const auto &entry : protocols)
        {
          protocolDistribution[entry.first] = entry.second;
        }
      }
      json validation = configResultToJson(service.validateConfiguration());
      std::string hardwareLabel = "NOT DETECTED";
      if (hilscher.boardCount > 0)
      {
        hardwareLabel = "DETECTED";
      }
      else if (!hilscher.compiledIn)
      {
        hardwareLabel = "NOT DETECTED";
      }
      // Never report READY/CONNECTED solely because SDK is present.
      if (hilscher.readinessState == "READY_FOR_TEST" && hilscher.boardCount == 0)
      {
        hardwareLabel = "NOT DETECTED";
      }
      setJson(
          res,
          200,
          {{"runtime",
            {{"schedulerRunning", st.schedulerRunning},
             {"configuredAdapterCount", st.configuredAdapterCount},
             {"runtimeAdapterCount", st.runtimeAdapterCount},
             {"connectedAdapters", st.connectedAdapters},
             {"disconnectedAdapters", st.disconnectedAdapters},
             {"faultedAdapters", st.faultedAdapters},
             {"equipmentCount", st.equipmentCount},
             {"connectedEquipment", st.equipmentCount - st.staleEquipment},
             {"staleEquipment", st.staleEquipment},
             {"machineFaultEquipment", st.machineFaultEquipment},
             {"activeCommunications", st.connectedAdapters},
             {"protocolDistribution", protocolDistribution},
             {"configurationPath", st.configurationPath},
             {"configurationLoadState", st.configurationLoadState},
             {"mesDependency", false},
             {"cicDependency", false}}},
           {"adapters", adapters},
           {"equipment", equipment},
           {"configurationValidation", validation},
           {"staleEquipment", stale},
           {"recentErrors", recentErrors},
           {"hilscher",
            {{"compiledIn", hilscher.compiledIn},
             {"readinessState", hilscher.readinessState},
             {"summary", hilscher.summary},
             {"driverVersion", hilscher.driverVersion},
             {"boardCount", hilscher.boardCount},
             {"hardware", hardwareLabel},
             {"selectedBoard", hilscher.selectedBoard},
             {"selectedFirmware", hilscher.selectedFirmware},
             {"serialNumber", hilscher.serialNumber},
             {"notes", hilscher.notes},
             {"manualChecks", hilscher.manualChecks}}}});
    });

    server.Get("/api/v1/events", [this](const httplib::Request &req, httplib::Response &res) {
      std::size_t limit = 100;
      if (req.has_param("limit"))
      {
        try
        {
          limit = static_cast<std::size_t>(std::stoul(req.get_param_value("limit")));
        }
        catch (...)
        {
        }
      }
      json arr = json::array();
      for (const ApplicationEvent &event : service.events(limit))
      {
        arr.push_back(
            {{"atUtc", iso8601(event.atUtc)},
             {"level", event.level},
             {"category", event.category},
             {"message", event.message},
             {"adapterId", event.adapterId},
             {"equipmentId", event.equipmentId}});
      }
      setJson(res, 200, {{"events", arr}});
    });

    server.Get("/api/v1/health", [](const httplib::Request &, httplib::Response &res) {
      setJson(res, 200, {{"ok", true}, {"service", "icp-application-api"}});
    });

    if (!staticRoot.empty())
    {
      server.set_mount_point("/", staticRoot);
    }
  }
};

HttpApiServer::HttpApiServer(
    ApplicationService &service,
    std::string staticRoot,
    std::string bindHost,
    int bindPort)
    : impl_(std::make_unique<Impl>(
          service, std::move(staticRoot), std::move(bindHost), bindPort))
{
}

HttpApiServer::~HttpApiServer()
{
  stop();
}

bool HttpApiServer::start()
{
  if (impl_->running.load())
  {
    return true;
  }
  impl_->registerRoutes();
  impl_->running = true;
  const std::string host = impl_->host;
  const int port = impl_->port;
  impl_->thread = std::thread([this, host, port]() {
    if (!impl_->server.listen(host.c_str(), port))
    {
      std::cerr << "ICP HTTP API failed to listen on " << host << ":" << port
                << std::endl;
      impl_->running = false;
    }
  });
  // Brief wait for listen.
  for (int i = 0; i < 50; ++i)
  {
    if (impl_->server.is_running())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return impl_->running.load();
}

void HttpApiServer::stop()
{
  if (!impl_)
  {
    return;
  }
  if (impl_->server.is_running())
  {
    impl_->server.stop();
  }
  if (impl_->thread.joinable())
  {
    impl_->thread.join();
  }
  impl_->running = false;
}

bool HttpApiServer::running() const
{
  return impl_ && impl_->running.load() && impl_->server.is_running();
}

int HttpApiServer::port() const
{
  return impl_ ? impl_->port : 0;
}

const std::string &HttpApiServer::host() const
{
  static const std::string empty;
  return impl_ ? impl_->host : empty;
}

}  // namespace icp
}  // namespace virtual_factory
