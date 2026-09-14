#include <virtual_factory/icp/app/HttpApiServer.hh>

#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <optional>
#include <set>
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
      {"lastSuccessfulTelemetryUtc",
       snap.hasSuccessfulCommunication
           ? json(iso8601(snap.lastSuccessfulCommunicationUtc))
           : json(nullptr)},
      {"hasSuccessfulCommunication", snap.hasSuccessfulCommunication},
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

json hilscherHardwareToJson(const HilscherDiagnosticsView &hilscher)
{
  std::string hardwareLabel = "NOT DETECTED";
  if (hilscher.boardCount > 0)
  {
    hardwareLabel = "DETECTED";
  }
  else if (!hilscher.compiledIn)
  {
    hardwareLabel = "NOT DETECTED";
  }
  if (hilscher.readinessState == "READY_FOR_TEST" && hilscher.boardCount == 0)
  {
    hardwareLabel = "NOT DETECTED";
  }
  return {
      {"compiledIn", hilscher.compiledIn},
      {"readinessState", hilscher.readinessState},
      {"summary", hilscher.summary},
      {"driverVersion", hilscher.driverVersion},
      {"boardCount", hilscher.boardCount},
      {"hardware", hardwareLabel},
      {"selectedBoard", hilscher.selectedBoard},
      {"selectedFirmware", hilscher.selectedFirmware},
      {"serialNumber", hilscher.serialNumber},
      {"notes", hilscher.notes},
      {"manualChecks", hilscher.manualChecks}};
}

json adapterDiagnosticsJson(const RuntimeAdapterView &view)
{
  return {
      {"adapterId", view.adapterId},
      {"protocol", view.protocol},
      {"implementation", view.implementation},
      {"transport", view.transport},
      {"description", view.description},
      {"configured", view.configured},
      {"runtimePresent", view.runtimePresent},
      {"connectionState", view.connectionState},
      {"connectionStateDisplay",
       view.connectionStateDisplay.empty() ? view.connectionState
                                            : view.connectionStateDisplay},
      {"lastError", view.lastError},
      {"enabled", view.enabled},
      {"equipmentCount", view.equipmentCount},
      {"connectionSummary", view.connectionSummary}};
}

json adapterDiagnosticsJson(const AdapterDiagnosticsView &view)
{
  json base = adapterDiagnosticsJson(view.adapter);
  base["health"] = view.session.health;
  base["healthReason"] = view.session.healthReason;
  // Backward-compatible alias used by older GUI bindings.
  base["communicationHealth"] = view.session.health;
  base["communicationLifecycleState"] = view.session.communicationLifecycleState;
  base["earlyWarning"] =
      view.session.earlyWarning.empty() ? json(nullptr) : json(view.session.earlyWarning);
  base["activeFault"] = view.session.activeFault;
  base["connectionAttempts"] = view.session.connectionAttempts;
  base["successfulConnections"] = view.session.successfulConnections;
  base["failedConnections"] = view.session.failedConnections;
  base["reconnectCount"] = view.session.reconnectCount;
  base["faultCount"] = view.session.faultCount;
  base["warningCount"] = view.session.warningCount;
  base["communicationFailureCount"] = view.session.communicationFailureCount;
  base["uptimeMs"] = view.currentUptimeMs;
  base["downtimeMs"] = view.currentDowntimeMs;
  base["currentStateDurationMs"] = view.currentStateDurationMs;
  base["cumulativeConnectedMs"] = view.session.cumulativeConnectedMs.count();
  base["cumulativeDisconnectedMs"] = view.session.cumulativeDisconnectedMs.count();
  base["connectedAtUtc"] = iso8601(view.session.connectedAt);
  base["disconnectedAtUtc"] = iso8601(view.session.disconnectedAt);
  base["lastSuccessfulCommunicationUtc"] =
      view.session.hasLastSuccessfulCommunication
          ? json(iso8601(view.session.lastSuccessfulCommunicationAt))
          : json(nullptr);
  base["hasSuccessfulCommunication"] = view.session.hasLastSuccessfulCommunication;
  base["lastWarning"] =
      view.session.lastWarning.empty() ? json(nullptr) : json(view.session.lastWarning);
  base["healthyEquipmentCount"] = view.healthyEquipmentCount;
  base["degradedEquipmentCount"] = view.degradedEquipmentCount;
  base["faultedEquipmentCount"] = view.faultedEquipmentCount;
  base["sessionMtbfStatus"] = view.sessionMtbfStatus;
  base["sessionMtbfMs"] =
      view.sessionMtbfStatus == "session_only" ? json(view.sessionMtbfMs) : json(nullptr);
  base["protocolSpecific"] = {
      {"available", false},
      {"detail",
       "The adapter provides the common diagnostics shown above; no additional "
       "protocol-specific metrics are exposed by the runtime by this adapter."}};
  return base;
}

json applicationEventToJson(const ApplicationEvent &ev)
{
  const std::string severity = ev.level == "error"       ? "ERROR"
                               : ev.level == "warn" || ev.level == "warning" ? "WARNING"
                               : ev.level == "critical"  ? "CRITICAL"
                                                         : "INFO";
  json out = {
      {"atUtc", iso8601(ev.atUtc)},
      {"level", ev.level},
      {"severity", severity},
      {"category", ev.category},
      {"message", ev.message},
      {"adapterId", ev.adapterId},
      {"equipmentId", ev.equipmentId},
      {"sourceType",
       !ev.equipmentId.empty() ? "equipment"
       : !ev.adapterId.empty() ? "adapter"
                               : "system"}};
  auto put = [&](const char *key, const std::string &value) {
    if (!value.empty())
    {
      out[key] = value;
    }
    else
    {
      out[key] = nullptr;
    }
  };
  put("eventType", ev.eventType);
  put("protocol", ev.protocol);
  put("reason", ev.reason);
  put("errorCode", ev.errorCode);
  put("errorDetails", ev.errorDetails);
  put("nodeId", ev.nodeId);
  put("previousState", ev.previousState);
  put("newState", ev.newState);
  put("previousHealth", ev.previousHealth);
  put("newHealth", ev.newHealth);
  put("command", ev.command);
  put("recovery", ev.recovery);
  put("correlationId", ev.correlationId);
  if (ev.durationMs >= 0)
  {
    out["durationMs"] = ev.durationMs;
  }
  else
  {
    out["durationMs"] = nullptr;
  }
  return out;
}


json historyStatusToJson(const HistoryStatus &st)
{
  return {
      {"available", st.available},
      {"degraded", st.degraded},
      {"path", st.path},
      {"message", st.message},
      {"schemaVersion", st.schemaVersion},
      {"droppedWrites", st.droppedWrites},
  };
}

std::optional<std::int64_t> parseOptionalInt64Param(
    const httplib::Request &req, const char *key)
{
  if (!req.has_param(key))
  {
    return std::nullopt;
  }
  try
  {
    return static_cast<std::int64_t>(std::stoll(req.get_param_value(key)));
  }
  catch (...)
  {
    return std::nullopt;
  }
}

HistoryQuery historyQueryFromRequest(const httplib::Request &req)
{
  HistoryQuery q;
  if (req.has_param("kind"))
  {
    q.kind = req.get_param_value("kind");
  }
  q.startUtcMs = parseOptionalInt64Param(req, "start");
  if (!q.startUtcMs.has_value())
  {
    q.startUtcMs = parseOptionalInt64Param(req, "startUtcMs");
  }
  q.endUtcMs = parseOptionalInt64Param(req, "end");
  if (!q.endUtcMs.has_value())
  {
    q.endUtcMs = parseOptionalInt64Param(req, "endUtcMs");
  }
  if (req.has_param("adapterId"))
  {
    q.adapterId = req.get_param_value("adapterId");
  }
  else if (req.has_param("adapter_id"))
  {
    q.adapterId = req.get_param_value("adapter_id");
  }
  if (req.has_param("equipmentId"))
  {
    q.equipmentId = req.get_param_value("equipmentId");
  }
  else if (req.has_param("equipment_id"))
  {
    q.equipmentId = req.get_param_value("equipment_id");
  }
  if (req.has_param("protocol"))
  {
    q.protocol = req.get_param_value("protocol");
  }
  if (req.has_param("category"))
  {
    q.category = req.get_param_value("category");
  }
  if (req.has_param("eventType"))
  {
    q.eventType = req.get_param_value("eventType");
  }
  else if (req.has_param("event_type"))
  {
    q.eventType = req.get_param_value("event_type");
  }
  if (req.has_param("severity"))
  {
    q.severity = req.get_param_value("severity");
  }
  if (req.has_param("limit"))
  {
    try
    {
      q.limit = static_cast<std::size_t>(std::stoul(req.get_param_value("limit")));
    }
    catch (...)
    {
    }
  }
  return q;
}

json historyQueryResultToJson(const HistoryQueryResult &result)
{
  json body = {
      {"historian", historyStatusToJson(result.status)},
      {"kind", result.kind},
      {"authorizationNote",
       "Milestone 2 will protect this endpoint with history.view"},
  };
  json items = json::array();
  if (result.kind == "events")
  {
    for (const auto &row : result.events)
    {
      items.push_back({
          {"id", row.id},
          {"tsUtcMs", row.tsUtcMs},
          {"level", row.level},
          {"category", row.category},
          {"eventType", row.eventType},
          {"message", row.message},
          {"adapterId", row.adapterId},
          {"equipmentId", row.equipmentId},
          {"protocol", row.protocol},
          {"previousState", row.previousState},
          {"newState", row.newState},
          {"previousHealth", row.previousHealth},
          {"newHealth", row.newHealth},
          {"command", row.command},
          {"reason", row.reason},
          {"errorCode", row.errorCode},
          {"errorDetails", row.errorDetails},
          {"nodeId", row.nodeId},
          {"recovery", row.recovery},
          {"correlationId", row.correlationId},
          {"durationMs", row.durationMs},
          {"actorId", row.actorId},
      });
    }
  }
  else if (result.kind == "communication_intervals")
  {
    for (const auto &row : result.communicationIntervals)
    {
      items.push_back({
          {"id", row.id},
          {"adapterId", row.adapterId},
          {"protocol", row.protocol},
          {"state", row.state},
          {"startedAtUtcMs", row.startedAtUtcMs},
          {"endedAtUtcMs",
           row.endedAtUtcMs.has_value() ? json(*row.endedAtUtcMs) : json(nullptr)},
          {"reason", row.reason},
          {"errorCode", row.errorCode},
      });
    }
  }
  else if (result.kind == "health_transitions")
  {
    for (const auto &row : result.healthTransitions)
    {
      items.push_back({
          {"id", row.id},
          {"adapterId", row.adapterId},
          {"protocol", row.protocol},
          {"previousHealth", row.previousHealth},
          {"newHealth", row.newHealth},
          {"tsUtcMs", row.tsUtcMs},
          {"reason", row.reason},
      });
    }
  }
  else if (result.kind == "alarm_events")
  {
    for (const auto &row : result.alarmEvents)
    {
      items.push_back({
          {"id", row.id},
          {"alarmKey", row.alarmKey},
          {"action", row.action},
          {"severity", row.severity},
          {"sourceType", row.sourceType},
          {"sourceId", row.sourceId},
          {"equipmentId", row.equipmentId},
          {"adapterId", row.adapterId},
          {"protocol", row.protocol},
          {"category", row.category},
          {"message", row.message},
          {"tsUtcMs", row.tsUtcMs},
          {"correlationId", row.correlationId},
          {"actorId", row.actorId},
      });
    }
  }
  else if (result.kind == "equipment_state_intervals")
  {
    for (const auto &row : result.equipmentStateIntervals)
    {
      items.push_back({
          {"id", row.id},
          {"equipmentId", row.equipmentId},
          {"adapterId", row.adapterId},
          {"state", row.state},
          {"startedAtUtcMs", row.startedAtUtcMs},
          {"endedAtUtcMs",
           row.endedAtUtcMs.has_value() ? json(*row.endedAtUtcMs) : json(nullptr)},
          {"reason", row.reason},
      });
    }
  }
  else if (result.kind == "command_audit")
  {
    for (const auto &row : result.commandAudits)
    {
      items.push_back({
          {"id", row.id},
          {"tsUtcMs", row.tsUtcMs},
          {"equipmentId", row.equipmentId},
          {"adapterId", row.adapterId},
          {"command", row.command},
          {"result", row.result},
          {"errorCode", row.errorCode},
          {"durationMs", row.durationMs},
          {"correlationId", row.correlationId},
          {"actorId", row.actorId},
      });
    }
  }
  else if (result.kind == "config_revisions")
  {
    for (const auto &row : result.configRevisions)
    {
      items.push_back({
          {"id", row.id},
          {"tsUtcMs", row.tsUtcMs},
          {"action", row.action},
          {"configurationName", row.configurationName},
          {"contentHash", row.contentHash},
          {"summary", row.summary},
          {"actorId", row.actorId},
      });
    }
  }
  body["items"] = std::move(items);
  return body;
}


json commandDiagnosticToJson(const CommandDiagnostic &cmd)
{
  json out = {
      {"command", cmd.command},
      {"name", cmd.command},
      {"target", cmd.target.empty() ? json(nullptr) : json(cmd.target)},
      {"availability", cmd.availability},
      {"execution", cmd.execution},
      {"reason", cmd.reason.empty() ? json(nullptr) : json(cmd.reason)},
      {"lastError", cmd.lastError.empty() ? json(nullptr) : json(cmd.lastError)},
      {"errorCode", cmd.errorCode.empty() ? json(nullptr) : json(cmd.errorCode)},
      {"errorMessage",
       cmd.errorMessage.empty() ? json(nullptr) : json(cmd.errorMessage)},
      {"lastExecutionAtUtc",
       cmd.hasLastExecution ? json(iso8601(cmd.lastExecutionAtUtc)) : json(nullptr)},
      {"hasLastExecution", cmd.hasLastExecution}};
  return out;
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
        recentEvents.push_back(applicationEventToJson(event));
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
             {"health", view.health},
             {"description", view.description},
             {"equipmentCount", view.equipmentCount},
             {"implementation", view.implementation},
             {"transport", view.transport},
             {"connectionSummary", view.connectionSummary}});
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
              {"health", view->health},
              {"description", view->description},
              {"equipmentCount", view->equipmentCount},
              {"implementation", view->implementation},
              {"transport", view->transport},
              {"connectionSummary", view->connectionSummary},
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
          AdapterManagerResult result = service.reconnectAdapter(req.matches[1]);
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
      const DiagnosticsReport report = service.diagnosticsReport();
      const std::vector<RuntimeAdapterView> adapterViews = service.adapters();

      json adapters = json::array();
      json gatewayAdapters = json::array();
      json hilscherAdapters = json::array();
      std::set<std::string> gatewayProtocols;
      std::size_t gatewayConnected = 0;
      std::size_t gatewayFaulted = 0;

      for (const AdapterDiagnosticsView &view : report.adapters)
      {
        json adapterJson = adapterDiagnosticsJson(view);
        json associated = json::array();
        for (const EquipmentSnapshot &snap : report.equipment)
        {
          if (snap.adapterId != view.adapter.adapterId)
          {
            continue;
          }
          associated.push_back(
              {{"equipmentId", snap.equipmentId},
               {"name", snap.equipmentId},
               {"type", snap.type},
               {"operationalState", operationalStateName(snap.operationalState)},
               {"operationalStateDisplay",
                snap.machineFault ? "FAULTED"
                                  : operationalStateName(snap.operationalState)},
               {"online", snap.communicationState == ConnectionState::Connected},
               {"communicationState", connectionStateName(snap.communicationState)},
               {"machineFault", snap.machineFault},
               {"stale", snap.stale},
               {"lastError", snap.lastError},
               {"hasSuccessfulCommunication", snap.hasSuccessfulCommunication}});
        }
        adapterJson["associatedEquipment"] = std::move(associated);
        adapters.push_back(adapterJson);
        if (view.adapter.implementation == "gateway")
        {
          gatewayAdapters.push_back(adapterJson);
          gatewayProtocols.insert(view.adapter.protocol);
          if (view.adapter.connectionState == "CONNECTED"
              || view.adapter.connectionState == "SIMULATED_ACTIVE")
          {
            ++gatewayConnected;
          }
          if (view.adapter.connectionState == "FAULTED")
          {
            ++gatewayFaulted;
          }
        }
        else if (view.adapter.implementation == "hilscher_native")
        {
          hilscherAdapters.push_back(adapterJson);
        }
      }

      json equipment = json::array();
      json stale = json::array();
      for (const EquipmentSnapshot &snap : report.equipment)
      {
        json eqEntry = equipmentToJson(snap);
        std::string health = "UNKNOWN";
        std::string healthReason = "Insufficient communication evidence.";
        std::string operationalState = operationalStateName(snap.operationalState);
        std::string operationalStateDisplay = operationalState;
        if (snap.machineFault)
        {
          operationalStateDisplay = "FAULTED";
          health = "FAULTED";
          healthReason = "Equipment machine fault is active.";
        }
        else if (snap.communicationState == ConnectionState::Faulted)
        {
          health = "FAULTED";
          healthReason = "Equipment communication is faulted.";
        }
        else if (snap.stale)
        {
          health = "DEGRADED";
          healthReason = "Equipment communication is stale or not currently connected.";
        }
        else if (snap.communicationState == ConnectionState::Connected)
        {
          if (!snap.lastError.empty())
          {
            health = "DEGRADED";
            healthReason =
                "Connected, but telemetry/read failed: " + snap.lastError;
          }
          else if (snap.hasSuccessfulCommunication)
          {
            health = "HEALTHY";
            healthReason = "Equipment communication is connected with observed telemetry.";
          }
          else
          {
            health = "UNKNOWN";
            healthReason =
                "Connected, but no successful communication has been observed yet.";
          }
        }
        // Operational model only exposes Running/Stopped (+ fault overlay).
        // Do not invent IDLE or other states.
        eqEntry["health"] = health;
        eqEntry["healthReason"] = healthReason;
        eqEntry["operationalState"] = operationalState;
        eqEntry["operationalStateDisplay"] = operationalStateDisplay;
        eqEntry["communicationLifecycleState"] =
            snap.communicationState == ConnectionState::Connected   ? "CONNECTED"
            : snap.communicationState == ConnectionState::Faulted   ? "FAULTED"
            : snap.communicationState == ConnectionState::Disconnected ? "DISCONNECTED"
                                                                      : "UNKNOWN";

        json configuredCommands = json::array();
        json commands = json::array();
        const std::vector<CommandDiagnostic> cmdDiags =
            service.commandDiagnosticsForEquipment(snap.adapterId, snap.equipmentId);
        for (const CommandDiagnostic &cmd : cmdDiags)
        {
          configuredCommands.push_back(cmd.command);
          commands.push_back(commandDiagnosticToJson(cmd));
        }
        eqEntry["configuredCommands"] = configuredCommands;
        eqEntry["commands"] = commands;
        // Backward-compatible summary: never-executed is not an error.
        if (commands.empty())
        {
          eqEntry["commandRuntimeState"] = "NONE_CONFIGURED";
        }
        else
        {
          bool anyFailed = false;
          bool anySuccess = false;
          bool anyExecuted = false;
          for (const CommandDiagnostic &cmd : cmdDiags)
          {
            if (cmd.hasLastExecution)
            {
              anyExecuted = true;
            }
            if (cmd.execution == "FAILED")
            {
              anyFailed = true;
            }
            if (cmd.execution == "SUCCESS")
            {
              anySuccess = true;
            }
          }
          if (!anyExecuted)
          {
            eqEntry["commandRuntimeState"] = "CONFIGURED_NOT_EXECUTED";
          }
          else if (anyFailed)
          {
            eqEntry["commandRuntimeState"] = "FAILED";
          }
          else if (anySuccess)
          {
            eqEntry["commandRuntimeState"] = "SUCCESS";
          }
          else
          {
            eqEntry["commandRuntimeState"] = "UNKNOWN";
          }
        }
        eqEntry["name"] = snap.equipmentId;
        equipment.push_back(eqEntry);
        if (snap.stale)
        {
          stale.push_back(eqEntry);
        }
      }

      json recentErrors = json::array();
      json recentEvents = json::array();
      for (const ApplicationEvent &ev : report.recentEvents)
      {
        const json eventJson = applicationEventToJson(ev);
        recentEvents.push_back(eventJson);
        if (ev.level == "error" || ev.level == "warn" || ev.level == "warning"
            || ev.level == "critical")
        {
          recentErrors.push_back(eventJson);
        }
      }

      json activeAlarms = json::array();
      for (const ActiveAlarmView &alarm : report.activeAlarms)
      {
        activeAlarms.push_back(
            {{"severity", alarm.severity},
             {"sourceType", alarm.sourceType},
             {"sourceId", alarm.sourceId},
             {"protocol", alarm.protocol},
             {"category", alarm.category},
             {"message", alarm.message},
             {"sinceUtc", iso8601(alarm.sinceUtc)}});
      }

      json protocolDistribution = json::object();
      {
        std::map<std::string, std::size_t> protocols;
        for (const RuntimeAdapterView &view : adapterViews)
        {
          ++protocols[view.protocol];
        }
        for (const auto &entry : protocols)
        {
          protocolDistribution[entry.first] = entry.second;
        }
      }

      json validation = configResultToJson(service.validateConfiguration());

      json implementations = json::object();
      if (!gatewayAdapters.empty())
      {
        json protocols = json::array();
        for (const std::string &proto : gatewayProtocols)
        {
          protocols.push_back(proto);
        }
        std::string gatewayLabel =
            "Industrial gateway (OPC UA / Modbus / MQTT / REST / EtherNet/IP";
        if (gatewayProtocols.count("profinet") || gatewayProtocols.count("profibus"))
        {
          gatewayLabel += " / PROFINET / PROFIBUS via gateway";
        }
        gatewayLabel += ")";
        implementations["gateway"] =
            {{"active", true},
             {"label", gatewayLabel},
             {"adapterCount", gatewayAdapters.size()},
             {"connectedCount", gatewayConnected},
             {"faultedCount", gatewayFaulted},
             {"protocols", protocols},
             {"adapters", gatewayAdapters}};
      }
      if (!hilscherAdapters.empty())
      {
        const HilscherDiagnosticsView hilscher = service.hilscherDiagnostics();
        implementations["hilscher_native"] =
            {{"active", true},
             {"label", "Hilscher native fieldbus (PROFINET / PROFIBUS)"},
             {"adapterCount", hilscherAdapters.size()},
             {"adapters", hilscherAdapters},
             {"hardware", hilscherHardwareToJson(hilscher)}};
      }

      const auto icpStatusLabel = [](bool ok, const char *good, const char *bad) {
        return ok ? std::string(good) : std::string(bad);
      };
      json icpChecks = json::array();
      for (const std::string &check : report.icp.checks)
      {
        icpChecks.push_back(check);
      }

      setJson(
          res,
          200,
          {{"generatedAtUtc", iso8601(report.generatedAtUtc)},
           {"icp",
            {{"overallHealth", report.icp.overallHealth},
             {"serviceStatus",
              icpStatusLabel(report.icp.serviceRunning, "RUNNING", "STOPPED")},
             {"httpApiStatus",
              icpStatusLabel(report.icp.apiReachable, "REACHABLE", "UNREACHABLE")},
             {"schedulerStatus",
              icpStatusLabel(report.icp.schedulerRunning, "RUNNING", "STOPPED")},
             {"adapterManagerStatus",
              report.icp.serviceRunning ? "OK" : "STOPPED"},
             {"liveStateCacheStatus", "OK"},
             {"configurationStatus",
              icpStatusLabel(report.icp.configurationValid, "VALID", "INVALID")},
             {"configurationMessage", report.icp.configurationMessage},
             {"eventSystemStatus", "OK"},
             {"selfTestResult", report.icp.overallHealth},
             {"selfTestDetail",
              report.icp.overallHealth == "HEALTHY"
                  ? "ICP software self-checks passed."
                  : "One or more ICP software self-checks require attention."},
             {"applicationUptimeMs", report.icp.applicationUptimeMs},
             {"configuredAdapterCount", report.icp.configuredAdapters},
             {"runtimeAdapterCount", report.icp.runtimeAdapters},
             {"liveEquipmentCount", report.icp.liveEquipmentCount},
             {"eventBufferSize", report.icp.eventBufferSize},
             {"eventBufferCapacity", report.icp.eventBufferCapacity},
             {"checks", icpChecks},
             {"notes",
              "ICP System Health reflects ICP software subsystems only. "
              "Industrial adapter connectedness does not by itself fault ICP."}}},
           {"system",
            {{"overallHealth", report.system.overallHealth},
             {"configuredAdapters", report.system.configuredAdapters},
             {"connectedAdapters", report.system.connectedAdapters},
             {"disconnectedAdapters", report.system.disconnectedAdapters},
             {"faultedAdapters", report.system.faultedAdapters},
             {"warningCount", report.system.warningCount},
             {"activeAlarmCount", report.system.activeAlarmCount},
             {"historicalFaultCount", report.system.historicalFaultCount},
             {"healthyAdapters", report.system.healthyAdapters},
             {"degradedAdapters", report.system.degradedAdapters},
             {"faultedHealthAdapters", report.system.faultedHealthAdapters},
             {"unknownHealthAdapters", report.system.unknownHealthAdapters},
             {"failedAdapters", report.system.faultedHealthAdapters},
             {"healthyEquipment", report.system.healthyEquipment},
             {"degradedEquipment", report.system.degradedEquipment},
             {"faultedEquipment", report.system.faultedEquipment},
             {"mtbfStatus", report.system.mtbfStatus},
             {"mtbfNote", report.system.mtbfNote},
             {"schedulerRunning", report.system.schedulerRunning}}},
           {"runtime",
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
           {"activeAlarms", activeAlarms},
           {"recentEvents", recentEvents},
           {"configurationValidation", validation},
           {"staleEquipment", stale},
           {"recentErrors", recentErrors},
           {"implementations", implementations}});
    });

    server.Get("/api/v1/history", [this](const httplib::Request &req, httplib::Response &res) {
      // Read-only historian. Authz (history.view) is Milestone 2.
      const HistoryQuery query = historyQueryFromRequest(req);
      const HistoryQueryResult result = service.queryHistory(query);
      setJson(res, 200, historyQueryResultToJson(result));
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
        arr.push_back(applicationEventToJson(event));
      }
      setJson(
          res,
          200,
          {{"events", arr},
           {"retention",
            {{"bounded", true},
             {"scope", "runtime_session"},
             {"note",
              "Recent in-memory events for the current process. Durable history "
              "is available at GET /api/v1/history (kind=events)."}}}});
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
