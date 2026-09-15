#include <virtual_factory/icp/app/ApplicationService.hh>

#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/config/AdapterImplementation.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>
#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>
#include <virtual_factory/icp/history/SqliteHistoryRepository.hh>
#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/EtherNetIpIndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>
#include <virtual_factory/industrial/ModbusIndustrialAdapter.hh>
#include <virtual_factory/industrial/MqttIndustrialAdapter.hh>
#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>
#include <virtual_factory/industrial/RestIndustrialAdapter.hh>

#include "hilscher/hilscher_availability.hh"
#include "hilscher/hilscher_hardware_readiness.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

namespace virtual_factory
{
namespace icp
{

namespace
{

ModbusTable parseModbusTable(const std::string &table)
{
  if (table == "coil")
  {
    return ModbusTable::Coil;
  }
  if (table == "discrete" || table == "discreteInput")
  {
    return ModbusTable::DiscreteInput;
  }
  if (table == "input" || table == "inputRegister")
  {
    return ModbusTable::InputRegister;
  }
  return ModbusTable::HoldingRegister;
}

MqttPayloadEncoding parseMqttEncoding(const std::string &encoding)
{
  if (encoding == "number" || encoding == "NumberText")
  {
    return MqttPayloadEncoding::NumberText;
  }
  if (encoding == "boolean" || encoding == "BooleanText")
  {
    return MqttPayloadEncoding::BooleanText;
  }
  return MqttPayloadEncoding::JsonPointer;
}

EtherNetIpValueType parseEipType(const std::string &valueType)
{
  if (valueType == "Bool" || valueType == "bool")
  {
    return EtherNetIpValueType::Bool;
  }
  if (valueType == "Real" || valueType == "real" || valueType == "Float")
  {
    return EtherNetIpValueType::Real;
  }
  return EtherNetIpValueType::Dint;
}

RestHttpMethod parseRestMethod(const std::string &method)
{
  if (method == "PUT" || method == "put")
  {
    return RestHttpMethod::Put;
  }
  if (method == "PATCH" || method == "patch")
  {
    return RestHttpMethod::Patch;
  }
  if (method == "GET" || method == "get")
  {
    return RestHttpMethod::Get;
  }
  return RestHttpMethod::Post;
}

OpcUaNodeRef mapOpcUaAddress(
    const std::string &equipmentId,
    const std::string &pointName,
    std::uint16_t namespaceIndex,
    const std::string &address)
{
  const OpcUaNodeRef mapped =
      opcUaNodeRefFromConfig(namespaceIndex, address);
  std::fprintf(
      stderr,
      "OPC UA DEBUG CONFIG MAP\n"
      "equipment=%s\n"
      "telemetry=%s\n"
      "config.address=%s\n"
      "config.namespaceIndex=%u\n"
      "mapped.namespaceIndex=%u\n"
      "mapped.identifier=%s\n",
      equipmentId.c_str(),
      pointName.c_str(),
      address.c_str(),
      static_cast<unsigned>(namespaceIndex),
      static_cast<unsigned>(mapped.namespaceIndex),
      mapped.identifier.c_str());
  std::fflush(stderr);
  return mapped;
}

}  // namespace

ApplicationService::ApplicationService(
    std::string configurationPath, std::string historyDatabasePath)
    : configuration_path_(std::move(configurationPath))
    , history_database_path_(
          historyDatabasePath.empty()
              ? defaultHistoryDatabasePath(configuration_path_)
              : std::move(historyDatabasePath))
    , service_started_at_(std::chrono::system_clock::now())
{
  this->lifecycle_.setHandler(
      [this](const LifecycleJob &job) { this->executeLifecycleJob(job); });
}

ApplicationService::~ApplicationService()
{
  this->stop();
}


void ApplicationService::ensureHistoryWriter()
{
  if (this->history_writer_)
  {
    return;
  }
  std::shared_ptr<HistoryRepository> repo;
  auto sqlite = std::make_shared<SqliteHistoryRepository>(this->history_database_path_);
  if (sqlite->openOk())
  {
    repo = std::move(sqlite);
  }
  else
  {
    const HistoryStatus st = sqlite->status();
    repo = std::make_shared<NullHistoryRepository>(
        this->history_database_path_,
        st.message.empty() ? "History database unavailable" : st.message);
  }
  this->history_writer_ = std::make_unique<AsyncHistoryWriter>(std::move(repo));
}

HistoryStatus ApplicationService::historyStatus() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const_cast<ApplicationService *>(this)->ensureHistoryWriter();
  return this->history_writer_->status();
}

HistoryQueryResult ApplicationService::queryHistory(const HistoryQuery &query) const
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    const_cast<ApplicationService *>(this)->ensureHistoryWriter();
  }
  // Query without holding ApplicationService mutex (SQLite may take short locks).
  return this->history_writer_->query(query);
}

const std::string &ApplicationService::historyDatabasePath() const
{
  return this->history_database_path_;
}

std::int64_t ApplicationService::toEpochMs(std::chrono::system_clock::time_point tp)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

std::string ApplicationService::alarmKeyFor(const ActiveAlarmView &alarm)
{
  // Stable identity — must NOT include the changing error message.
  return alarm.sourceType + ":" + alarm.sourceId + ":" + alarm.category;
}

std::int64_t ApplicationService::nextAlarmOccurrenceIdLocked() const
{
  if (this->next_alarm_occurrence_id_ <= 0)
  {
    this->next_alarm_occurrence_id_ = toEpochMs(std::chrono::system_clock::now()) * 1000;
  }
  ++this->next_alarm_occurrence_id_;
  return this->next_alarm_occurrence_id_;
}

std::string ApplicationService::equipmentHistoryState(const EquipmentSnapshot &snap)
{
  if (snap.machineFault)
  {
    return "Faulted";
  }
  if (snap.operationalState == OperationalState::Running)
  {
    return "Running";
  }
  return "Stopped";
}

void ApplicationService::persistEventToHistory(const ApplicationEvent &event) const
{
  if (!this->history_writer_)
  {
    return;
  }
  HistoryEventRecord row;
  row.tsUtcMs = toEpochMs(
      event.atUtc.time_since_epoch().count() == 0 ? std::chrono::system_clock::now()
                                                  : event.atUtc);
  row.level = event.level;
  row.category = event.category;
  row.eventType = event.eventType;
  row.message = event.message;
  row.adapterId = event.adapterId;
  row.equipmentId = event.equipmentId;
  row.protocol = event.protocol;
  row.previousState = event.previousState;
  row.newState = event.newState;
  row.previousHealth = event.previousHealth;
  row.newHealth = event.newHealth;
  row.command = event.command;
  row.reason = event.reason;
  row.errorCode = event.errorCode;
  row.errorDetails = event.errorDetails;
  row.nodeId = event.nodeId;
  row.recovery = event.recovery;
  row.correlationId = event.correlationId;
  row.durationMs = event.durationMs;
  this->history_writer_->enqueueEvent(std::move(row));

  if (event.eventType == "health_changed" && !event.adapterId.empty())
  {
    HealthTransitionRecord health;
    health.adapterId = event.adapterId;
    health.protocol = event.protocol;
    health.previousHealth = event.previousHealth;
    health.newHealth = event.newHealth;
    health.tsUtcMs = row.tsUtcMs;
    health.reason = event.reason.empty() ? event.message : event.reason;
    this->history_writer_->enqueueHealthTransition(std::move(health));
  }
}

void ApplicationService::persistConfigRevision(
    const std::string &action, const std::string &summary) const
{
  if (!this->history_writer_)
  {
    return;
  }
  // exportConfigurationJson stores credential refs only — never plaintext secrets.
  const std::string exported = this->exportConfigurationJson();
  ConfigRevisionRecord row;
  row.tsUtcMs = toEpochMs(std::chrono::system_clock::now());
  row.action = action;
  row.configurationName = this->catalog_.document().name;
  row.contentHash = fingerprintContent(exported);
  row.summary = summary;
  this->history_writer_->enqueueConfigRevision(std::move(row));
}

void ApplicationService::persistCommandAudit(
    const std::string &equipmentId,
    const std::string &adapterId,
    const std::string &command,
    const std::string &result,
    const std::string &errorCode,
    std::int64_t durationMs,
    const std::string &correlationId) const
{
  if (!this->history_writer_)
  {
    return;
  }
  CommandAuditRecord row;
  row.tsUtcMs = toEpochMs(std::chrono::system_clock::now());
  row.equipmentId = equipmentId;
  row.adapterId = adapterId;
  row.command = command;
  row.result = result;
  row.errorCode = errorCode;
  row.durationMs = durationMs;
  row.correlationId = correlationId;
  this->history_writer_->enqueueCommandAudit(std::move(row));
}


void ApplicationService::start()
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->running_)
    {
      return;
    }
    // Open local historian without waiting on remote services. Failure is
    // non-fatal: ICP and protocols continue; history reports degraded.
    this->ensureHistoryWriter();
    this->history_writer_->start();
    // Lifecycle workers own blocking connect/disconnect I/O. Poll only enqueues.
    this->lifecycle_.start(4);
    this->scheduler_ = std::make_unique<PollScheduler>(
        this->manager_, this->cache_, std::chrono::milliseconds(250));
    this->scheduler_->setAfterPollHook([this]() { this->onPollCycle(); });
    this->scheduler_->start();
    this->running_ = true;
  }
  this->recordEvent("info", "runtime", "ICP application service started");
}

void ApplicationService::stop()
{
  std::unique_ptr<PollScheduler> scheduler;
  bool was_running = false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->running_)
    {
      was_running = true;
      this->running_ = false;
      scheduler = std::move(this->scheduler_);
    }
    else if (!this->history_writer_)
    {
      return;
    }
  }
  if (scheduler)
  {
    scheduler->stop();
  }
  // Drain lifecycle workers before destroying/disconnecting adapters.
  this->lifecycle_.stop();
  if (was_running)
  {
    this->manager_.disconnectAll();
    this->recordEvent("info", "runtime", "ICP application service stopped");
  }
  // Flush historian after the final event enqueue.
  if (this->history_writer_)
  {
    this->history_writer_->stop();
  }
}

bool ApplicationService::running() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->running_;
}

ApplicationStatus ApplicationService::status() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  ApplicationStatus out;
  out.schedulerRunning = this->running_ && this->scheduler_
                         && this->scheduler_->running();
  out.configuredAdapterCount = this->catalog_.adapterCount();
  out.runtimeAdapterCount = this->manager_.adapterCount();
  out.configurationPath = this->configuration_path_;
  out.configurationName = this->catalog_.document().name;
  out.configurationLoaded = this->configuration_loaded_;
  out.configurationLoadState = this->configuration_load_state_;

  // Count from poll-maintained diagnostics only. Never call live
  // IndustrialAdapter::connectionState()/lastError() on the HTTP path — those
  // fields are mutated under the per-adapter I/O mutex during connect/poll/
  // teardown (especially OPC UA EventLoop restart on reconnect) and racing
  // them from HTTP workers can stall the GUI control-plane requests.
  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    const std::string &state =
        this->diagnosticsFor(record.adapterId).lastObservedState;
    if (state == "CONNECTED")
    {
      ++out.connectedAdapters;
    }
    else if (state == "FAULTED")
    {
      ++out.faultedAdapters;
    }
    else if (state == "DISCONNECTED" || state == "NOT_CONFIGURED" || state.empty())
    {
      ++out.disconnectedAdapters;
    }
    else
    {
      ++out.disconnectedAdapters;
    }
  }

  const auto snapshots = this->cache_.equipment();
  out.equipmentCount = snapshots.size();
  for (const EquipmentSnapshot &snap : snapshots)
  {
    if (snap.stale)
    {
      ++out.staleEquipment;
    }
    if (snap.machineFault)
    {
      ++out.machineFaultEquipment;
    }
  }
  return out;
}

std::vector<ProtocolCapability> ApplicationService::protocols() const
{
  return {
      {"mock", "Mock", true, false},
      {"opcua", "OPC UA", true, false},
      {"modbus", "Modbus", true, false},
      {"mqtt", "MQTT", true, false},
      {"rest", "REST", true, false},
      {"ethernetip", "EtherNet/IP", true, false},
      {"profinet", "PROFINET", true, false},
      {"profibus", "PROFIBUS", true, false},
  };
}

const IcpConfigurationDocument &ApplicationService::configuration() const
{
  return this->catalog_.document();
}

ConfigResult ApplicationService::setConfiguration(IcpConfigurationDocument document)
{
  const ConfigResult result = this->catalog_.replaceDocument(std::move(document));
  if (result.ok)
  {
    this->recordEvent("info", "configuration", "Configuration replaced in memory");
    this->persistConfigRevision("replace", "Configuration replaced in memory");
  }
  else
  {
    this->recordEvent("error", "configuration", result.message);
  }
  return result;
}

ConfigResult ApplicationService::validateConfiguration() const
{
  return this->catalog_.validate();
}

ConfigResult ApplicationService::saveConfiguration()
{
  JsonFileConfigurationRepository repo(this->configuration_path_);
  const ConfigResult result = this->catalog_.save(repo);
  if (result.ok)
  {
    this->recordEvent(
        "info",
        "configuration",
        "Configuration saved to " + this->configuration_path_);
    this->persistConfigRevision(
        "save", "Configuration saved to " + this->configuration_path_);
  }
  else
  {
    this->recordEvent("error", "configuration", result.message);
  }
  return result;
}

ConfigResult ApplicationService::loadConfiguration()
{
  JsonFileConfigurationRepository repo(this->configuration_path_);
  const ConfigResult result = this->catalog_.load(repo);
  this->configuration_loaded_ = result.ok;
  this->configuration_load_state_ = result.message;
  if (result.ok)
  {
    if (result.message.find("not found") != std::string::npos)
    {
      this->recordEvent(
          "info",
          "configuration",
          "First run: no configuration file; using empty configuration");
    }
    else
    {
      this->recordEvent(
          "info",
          "configuration",
          "Configuration loaded from " + this->configuration_path_);
      this->persistConfigRevision(
          "load", "Configuration loaded from " + this->configuration_path_);
    }
    // Materialize only — never connect here. Peer-down must not block ICP
    // startup / HTTP. Background recovery (onPollCycle) owns first connect.
    this->materializeEnabledAdaptersForRecovery();
  }
  else
  {
    this->recordEvent("error", "configuration", result.message);
  }
  return result;
}

ConfigResult ApplicationService::importConfigurationJson(const std::string &jsonText)
{
  IcpConfigurationDocument document;
  const ConfigResult parsed =
      JsonFileConfigurationRepository::parseText(jsonText, &document);
  if (!parsed.ok)
  {
    this->recordEvent("error", "configuration", parsed.message);
    return parsed;
  }
  return this->setConfiguration(std::move(document));
}

std::string ApplicationService::exportConfigurationJson() const
{
  return JsonFileConfigurationRepository::toJsonText(this->catalog_.document());
}

ConfigResult ApplicationService::upsertAdapterConfig(AdapterConfigRecord adapter)
{
  const std::string id = adapter.adapterId;
  IndustrialAdapter *existing = this->manager_.adapter(id);
  const bool had_runtime = existing != nullptr;
  ConnectionState prior_state = ConnectionState::Disconnected;
  bool auto_connect_desired = false;
  if (had_runtime)
  {
    prior_state = existing->connectionState();
  }
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->adapter_diagnostics_.count(id) != 0)
    {
      auto_connect_desired = this->adapter_diagnostics_.at(id).autoConnectDesired;
    }
  }

  const ConfigResult result = this->catalog_.upsertAdapter(std::move(adapter));
  if (!result.ok)
  {
    this->recordEvent("error", "configuration", result.message, id);
    return result;
  }
  this->recordEvent("info", "configuration", "Adapter upserted", id);
  this->persistConfigRevision("adapter_upsert", "Adapter upserted: " + id);

  // Catalog-only updates leave live protocol bindings (NodeIds, endpoints, etc.)
  // at construction-time values. Soft-failure alarms never clear after a GUI
  // NodeId correction unless the runtime is rematerialized. Rematerialize only
  // when a runtime object already exists - first-time create still waits for
  // Connect / background recovery (ICP must not block on peer availability).
  if (!had_runtime)
  {
    return result;
  }

  const AdapterConfigRecord *record = this->catalog_.adapter(id);
  if (record == nullptr)
  {
    return result;
  }

  if (!record->enabled)
  {
    this->manager_.removeAdapter(id);
    this->cache_.removeAdapterEquipment(id);
    return result;
  }

  const bool was_live = prior_state == ConnectionState::Connected
      || prior_state == ConnectionState::Faulted;
  const bool should_connect = was_live || auto_connect_desired;

  AdapterManagerResult ensured = this->ensureRuntimeAdapter(*record);
  if (!ensured.ok)
  {
    this->recordEvent("error", "configuration", ensured.message, id);
    return result;
  }

  if (should_connect)
  {
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      AdapterSessionDiagnostics &diag = this->diagnosticsFor(id);
      diag.autoConnectDesired = true;
      diag.nextAutoReconnectAt = std::chrono::steady_clock::now();
    }
    LifecycleJob job;
    job.adapterId = id;
    job.op = LifecycleOp::Connect;
    job.generation = this->lifecycle_.generation(id);
    (void)this->lifecycle_.enqueue(std::move(job));
  }
  else
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->observeAdapterStateLocked(id, "DISCONNECTED");
  }

  return result;
}

ConfigResult ApplicationService::removeAdapterConfig(const std::string &adapterId)
{
  // Cancel queued/in-flight recovery for this adapter before teardown.
  (void)this->lifecycle_.bumpGeneration(adapterId);
  if (this->manager_.adapter(adapterId) != nullptr)
  {
    this->manager_.removeAdapter(adapterId);
    this->cache_.removeAdapterEquipment(adapterId);
  }
  const ConfigResult result = this->catalog_.removeAdapter(adapterId);
  if (result.ok)
  {
    this->recordEvent("info", "configuration", "Adapter removed", adapterId);
    this->persistConfigRevision("adapter_remove", "Adapter removed: " + adapterId);
  }
  return result;
}

std::vector<RuntimeAdapterView> ApplicationService::adapters() const
{
  std::vector<RuntimeAdapterView> out;
  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    RuntimeAdapterView view;
    view.adapterId = record.adapterId;
    view.protocol = record.protocol;
    view.configured = true;
    view.enabled = record.enabled;
    view.description = record.description;
    view.equipmentCount = record.equipment.size();
    // Pointer presence is protected by the manager map mutex and does not
    // touch protocol I/O objects. Connection/error text must come from the
    // poll-owned diagnostics snapshot — not live adapter fields — so GUI
    // list/status fetches cannot race reconnect teardown/connect.
    view.runtimePresent = this->manager_.adapter(record.adapterId) != nullptr;
    view.connectionState =
        view.runtimePresent ? "DISCONNECTED" : "NOT_CONFIGURED";
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      const AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
      if (!diag.lastObservedState.empty())
      {
        view.connectionState = diag.lastObservedState;
      }
      else if (!view.runtimePresent)
      {
        view.connectionState = "DISCONNECTED";
      }
      // Current operator-facing error only — do not leak historical lastFaultError
      // into CONNECTED/HEALTHY rows after reconnect recovery.
      if (view.connectionState == "FAULTED")
      {
        view.lastError = diag.lastFaultError;
      }
      else if (!diag.earlyWarning.empty())
      {
        view.lastError = diag.earlyWarning;
      }
      view.health = diag.health;
    }
    view.connectionStateDisplay = connectionStateDisplay(view.protocol, view.connectionState);
    view.implementation = adapterImplementation(record);
    view.connectionSummary = connectionSummary(record);
    if (record.protocol == "modbus")
    {
      const std::string transport =
          record.connection.transport.empty() ? "tcp"
                                              : record.connection.transport;
      view.transport = transport == "rtu" ? "rtu" : "tcp";
    }
    out.push_back(std::move(view));
  }
  return out;
}

std::optional<RuntimeAdapterView> ApplicationService::adapter(
    const std::string &adapterId) const
{
  for (RuntimeAdapterView view : this->adapters())
  {
    if (view.adapterId == adapterId)
    {
      return view;
    }
  }
  return std::nullopt;
}

AdapterManagerResult ApplicationService::connectAdapter(const std::string &adapterId)
{
  const AdapterConfigRecord *record = this->catalog_.adapter(adapterId);
  if (record == nullptr)
  {
    AdapterManagerResult result;
    result.ok = false;
    result.message = "adapter '" + adapterId + "' is not in configuration";
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = "connection";
    ev.eventType = "connect_failed";
    ev.message = result.message;
    ev.adapterId = adapterId;
    ev.reason = result.message;
    this->recordEvent(std::move(ev));
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
      ++diag.connectionAttempts;
      ++diag.failedConnections;
    }
    return result;
  }
  if (!record->enabled)
  {
    AdapterManagerResult result;
    result.ok = false;
    result.message = "adapter '" + adapterId + "' is disabled";
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
    ++diag.connectionAttempts;
    // Explicit Connect arms ICP-owned recovery for later outages.
    diag.autoConnectDesired = true;
  }

  AdapterManagerResult ensured = this->ensureRuntimeAdapter(*record);
  if (!ensured.ok)
  {
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = "connection";
    ev.eventType = "connect_failed";
    ev.message = ensured.message;
    ev.adapterId = adapterId;
    ev.protocol = record->protocol;
    ev.reason = ensured.message;
    this->recordEvent(std::move(ev));
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      // Creation failure is a failed connection attempt, not a live FAULTED session.
      ++this->diagnosticsFor(adapterId).failedConnections;
    }
    return ensured;
  }

  LifecycleJob job;
  job.adapterId = adapterId;
  job.op = LifecycleOp::Connect;
  job.generation = this->lifecycle_.generation(adapterId);
  if (!this->lifecycle_.enqueue(std::move(job)))
  {
    AdapterManagerResult result;
    result.ok = false;
    result.message = "lifecycle executor unavailable";
    return result;
  }

  AdapterManagerResult accepted;
  accepted.ok = true;
  accepted.accepted = true;
  accepted.message = "connect accepted";
  return accepted;
}

AdapterManagerResult ApplicationService::disconnectAdapter(
    const std::string &adapterId)
{
  if (this->manager_.adapter(adapterId) == nullptr
      && this->catalog_.adapter(adapterId) == nullptr)
  {
    return {false, "adapter not found: " + adapterId};
  }

  // Invalidate queued recovery/connect before enqueueing Disconnect.
  const std::uint64_t generation = this->lifecycle_.bumpGeneration(adapterId);
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
    // Explicit Disconnect must remain sticky for this process lifetime.
    diag.autoConnectDesired = false;
    diag.autoReconnectInFlight = false;
    diag.nextAutoReconnectAt = {};
    this->reconnect_in_progress_.erase(adapterId);
  }

  LifecycleJob job;
  job.adapterId = adapterId;
  job.op = LifecycleOp::Disconnect;
  job.generation = generation;
  if (!this->lifecycle_.enqueue(std::move(job)))
  {
    // Executor stopped: best-effort sync disconnect for clean teardown paths.
    AdapterManagerResult result = this->manager_.disconnectAdapter(adapterId);
    this->cache_.removeAdapterEquipment(adapterId);
    if (result.ok)
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->observeAdapterStateLocked(adapterId, "DISCONNECTED");
    }
    return result;
  }

  AdapterManagerResult accepted;
  accepted.ok = true;
  accepted.accepted = true;
  accepted.message = "disconnect accepted";
  return accepted;
}

AdapterManagerResult ApplicationService::reconnectAdapter(const std::string &adapterId)
{
  const AdapterConfigRecord *record = this->catalog_.adapter(adapterId);
  if (record == nullptr)
  {
    return {false, "adapter '" + adapterId + "' is not in configuration"};
  }
  if (!record->enabled)
  {
    return {false, "adapter '" + adapterId + "' is disabled"};
  }

  AdapterManagerResult ensured = this->ensureRuntimeAdapter(*record);
  if (!ensured.ok)
  {
    return ensured;
  }

  // Cancel stale recovery for this adapter; reconnect is a fresh intent.
  const std::uint64_t generation = this->lifecycle_.bumpGeneration(adapterId);
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    ++this->diagnosticsFor(adapterId).reconnectCount;
    ++this->diagnosticsFor(adapterId).connectionAttempts;
    this->diagnosticsFor(adapterId).autoConnectDesired = true;
    this->reconnect_in_progress_[adapterId] = true;
  }

  LifecycleJob job;
  job.adapterId = adapterId;
  job.op = LifecycleOp::Reconnect;
  job.generation = generation;
  if (!this->lifecycle_.enqueue(std::move(job)))
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->reconnect_in_progress_.erase(adapterId);
    return {false, "lifecycle executor unavailable"};
  }

  AdapterManagerResult accepted;
  accepted.ok = true;
  accepted.accepted = true;
  accepted.message = "reconnect accepted";
  return accepted;
}

std::vector<EquipmentSnapshot> ApplicationService::equipment() const
{
  return this->cache_.equipment();
}

std::optional<EquipmentSnapshot> ApplicationService::equipmentById(
    const std::string &id) const
{
  return this->cache_.equipmentById(id);
}

EquipmentCommandResult ApplicationService::executeEquipmentCommand(
    const std::string &equipmentId,
    const std::string &command,
    double parameter)
{
  EquipmentCommandResult out;
  out.equipmentId = equipmentId;
  out.command = command;

  Equipment *equipment = this->manager_.equipmentById(equipmentId);
  if (equipment == nullptr)
  {
    out.message = "equipment '" + equipmentId + "' not found or not connected";
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = "command";
    ev.eventType = "command_failed";
    ev.message = out.message;
    ev.equipmentId = equipmentId;
    ev.command = command;
    ev.reason = out.message;
    ev.errorDetails = out.message;
    this->recordEvent(std::move(ev));
    this->persistCommandAudit(
        equipmentId, {}, command, "FAILED", "equipment_unavailable", -1, {});
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      CommandDiagnostic &rt =
          this->command_runtime_[commandRuntimeKey(equipmentId, command)];
      rt.command = command;
      rt.execution = "FAILED";
      rt.availability = "UNAVAILABLE";
      rt.reason = "Equipment is not present in a connected runtime adapter.";
      rt.lastError = out.message;
      rt.errorMessage = out.message;
      rt.lastExecutionAtUtc = std::chrono::system_clock::now();
      rt.hasLastExecution = true;
    }
    return out;
  }

  IndustrialAdapter *owner = nullptr;
  this->manager_.forEachAdapter([&](IndustrialAdapter &adapter) {
    if (owner == nullptr && adapter.equipmentById(equipmentId) != nullptr)
    {
      owner = &adapter;
    }
  });
  if (owner == nullptr)
  {
    out.message = "no adapter owns equipment '" + equipmentId + "'";
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = "command";
    ev.eventType = "command_failed";
    ev.message = out.message;
    ev.equipmentId = equipmentId;
    ev.command = command;
    ev.reason = out.message;
    this->recordEvent(std::move(ev));
    return out;
  }

  if (owner->connectionState() != ConnectionState::Connected)
  {
    out.message = "adapter '" + owner->id() + "' is not connected";
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = "command";
    ev.eventType = "command_unavailable";
    ev.message = out.message;
    ev.adapterId = owner->id();
    ev.equipmentId = equipmentId;
    ev.command = command;
    ev.reason = "communication lifecycle is not CONNECTED";
    ev.newState = connectionStateName(owner->connectionState());
    this->recordEvent(std::move(ev));
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      CommandDiagnostic &rt =
          this->command_runtime_[commandRuntimeKey(equipmentId, command)];
      rt.command = command;
      rt.availability = "UNAVAILABLE";
      rt.execution = "FAILED";
      rt.reason = "Adapter communication lifecycle is not CONNECTED.";
      rt.lastError = out.message;
      rt.errorMessage = out.message;
      rt.lastExecutionAtUtc = std::chrono::system_clock::now();
      rt.hasLastExecution = true;
    }
    return out;
  }

  const CommandResult executed = equipment->execute(command, parameter);
  this->cache_.updateFromAdapter(*owner);
  out.ok = executed.accepted;
  out.message = executed.message;

  std::string availability = "AVAILABLE";
  std::string execution = executed.accepted ? "SUCCESS" : "FAILED";
  if (!executed.accepted)
  {
    const std::string lower = executed.message;
    if (lower.find("unknown command") != std::string::npos
        || lower.find("unsupported") != std::string::npos)
    {
      availability = "UNSUPPORTED";
      execution = "FAILED";
    }
  }

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    CommandDiagnostic &rt =
        this->command_runtime_[commandRuntimeKey(equipmentId, command)];
    rt.command = command;
    rt.availability = availability;
    rt.execution = execution;
    rt.reason.clear();
    rt.lastError = executed.accepted ? std::string{} : executed.message;
    rt.errorMessage = rt.lastError;
    rt.lastExecutionAtUtc = std::chrono::system_clock::now();
    rt.hasLastExecution = true;
  }

  ApplicationEvent ev;
  ev.level = executed.accepted ? "info" : "error";
  ev.category = "command";
  ev.eventType = executed.accepted ? "command_succeeded" : "command_failed";
  ev.message = command + ": " + executed.message;
  ev.adapterId = owner->id();
  ev.equipmentId = equipmentId;
  ev.command = command;
  ev.reason = executed.message;
  if (!executed.accepted)
  {
    ev.errorDetails = executed.message;
  }
  this->recordEvent(std::move(ev));
  this->persistCommandAudit(
      equipmentId,
      owner->id(),
      command,
      execution,
      executed.accepted ? std::string{} : availability,
      -1,
      {});
  return out;
}

std::string ApplicationService::commandRuntimeKey(
    const std::string &equipmentId, const std::string &command)
{
  return equipmentId + "\x1f" + command;
}

std::string ApplicationService::commandTargetSummary(
    const AdapterConfigRecord &record, const CommandMappingRecord &cmd)
{
  if (!cmd.address.empty())
  {
    return cmd.address;
  }
  if (record.protocol == "modbus")
  {
    std::string target = cmd.table.empty() ? "holding" : cmd.table;
    target += ":" + std::to_string(cmd.registerAddress);
    if (cmd.unitId > 0)
    {
      target += " unit=" + std::to_string(cmd.unitId);
    }
    return target;
  }
  if (!cmd.method.empty())
  {
    return cmd.method;
  }
  if (cmd.outputByteOffset > 0 || !cmd.valueType.empty())
  {
    return "offset=" + std::to_string(cmd.outputByteOffset);
  }
  return {};
}

std::vector<CommandDiagnostic> ApplicationService::commandDiagnosticsForEquipment(
    const std::string &adapterId, const std::string &equipmentId) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<CommandDiagnostic> out;

  const AdapterConfigRecord *record = this->catalog_.adapter(adapterId);
  if (record == nullptr)
  {
    return out;
  }

  const EquipmentMappingRecord *eqRecord = nullptr;
  for (const EquipmentMappingRecord &eq : record->equipment)
  {
    if (eq.equipmentId == equipmentId)
    {
      eqRecord = &eq;
      break;
    }
  }
  if (eqRecord == nullptr)
  {
    return out;
  }

  std::string lifecycle = "DISCONNECTED";
  bool runtimePresent = this->manager_.adapter(adapterId) != nullptr;
  bool connected = false;
  // Prefer configured command names over live equipmentById(). Looking up live
  // equipment takes the adapter io_mutex; if protocol teardown is slow that
  // stalls diagnostics/HTTP while holding ApplicationService::mutex_.
  // Connection lifecycle must also come from diagnostics — never live
  // connectionState() on this HTTP path (reconnect I/O races).
  std::vector<std::string> liveCommands;
  if (this->adapter_diagnostics_.count(adapterId) != 0)
  {
    lifecycle = this->adapter_diagnostics_.at(adapterId).lastObservedState;
    connected = lifecycle == "CONNECTED";
  }
  liveCommands.reserve(eqRecord->commands.size());
  for (const CommandMappingRecord &cmd : eqRecord->commands)
  {
    liveCommands.push_back(cmd.command);
  }

  for (const CommandMappingRecord &cmd : eqRecord->commands)
  {
    CommandDiagnostic view;
    view.command = cmd.command;
    view.target = commandTargetSummary(*record, cmd);
    view.execution = "NOT_EXECUTED";
    view.availability = "CONFIGURED";

    const auto it = this->command_runtime_.find(
        commandRuntimeKey(equipmentId, cmd.command));
    if (it != this->command_runtime_.end())
    {
      view = it->second;
      if (view.target.empty())
      {
        view.target = commandTargetSummary(*record, cmd);
      }
    }

    if (!connected)
    {
      view.availability = "UNAVAILABLE";
      view.reason = "Adapter communication lifecycle is " + lifecycle + ".";
      // Preserve last execution outcome; never-executed stays NOT_EXECUTED.
      if (!view.hasLastExecution)
      {
        view.execution = "NOT_EXECUTED";
        view.lastError.clear();
        view.errorMessage.clear();
      }
    }
    else if (!view.hasLastExecution)
    {
      // Connected and never invoked: CONFIGURED, or AVAILABLE if live equipment
      // lists the command (evidence of usability without inventing execution).
      const bool listed =
          std::find(liveCommands.begin(), liveCommands.end(), cmd.command)
          != liveCommands.end();
      if (listed)
      {
        view.availability = "AVAILABLE";
        view.reason = "Command is present on the connected equipment command list.";
      }
      else if (runtimePresent)
      {
        view.availability = "CONFIGURED";
        view.reason =
            "Command is configured; runtime has not established live availability.";
      }
      else
      {
        view.availability = "CONFIGURED";
      }
      view.execution = "NOT_EXECUTED";
    }
    else if (view.execution == "SUCCESS")
    {
      view.availability = "AVAILABLE";
      view.reason.clear();
    }

    out.push_back(std::move(view));
  }

  return out;
}

HilscherDiagnosticsView ApplicationService::hilscherDiagnostics() const
{
  HilscherDiagnosticsView view;
  view.compiledIn = internal::hilscherCifxSdkAvailable();
  const internal::HilscherReadinessReport report =
      internal::assessHilscherHardwareReadiness({});
  view.readinessState = report.stateLabel;
  view.summary = report.summary;
  view.driverVersion = report.driverVersion;
  view.boardCount = report.inventory.boardCount;
  view.selectedBoard = report.selectedBoardName;
  view.selectedFirmware = report.selectedFirmwareName;
  view.serialNumber = report.selectedSerialNumber;
  view.notes = report.notes;
  view.manualChecks = report.manualChecks;
  if (!view.compiledIn)
  {
    view.readinessState = "SDK_MISSING";
  }
  if (view.boardCount == 0 && view.compiledIn
      && report.state != internal::HilscherReadinessState::DriverInitFailed)
  {
    // Prefer explicit operator wording when no card is present.
    if (view.readinessState == "NO_BOARD" || view.readinessState == "READY_FOR_TEST")
    {
      view.readinessState = "NO_BOARD";
    }
  }
  return view;
}

std::vector<ApplicationEvent> ApplicationService::events(std::size_t limit) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<ApplicationEvent> out;
  if (limit == 0)
  {
    return out;
  }
  const std::size_t count = std::min(limit, this->events_.size());
  out.reserve(count);
  for (std::size_t i = this->events_.size() - count; i < this->events_.size(); ++i)
  {
    out.push_back(this->events_[i]);
  }
  return out;
}

ConfigurationCatalog &ApplicationService::catalog()
{
  return this->catalog_;
}

const ConfigurationCatalog &ApplicationService::catalog() const
{
  return this->catalog_;
}

AdapterManager &ApplicationService::manager()
{
  return this->manager_;
}

LiveStateCache &ApplicationService::cache()
{
  return this->cache_;
}

void ApplicationService::recordEvent(
    const std::string &level,
    const std::string &category,
    const std::string &message,
    const std::string &adapterId,
    const std::string &equipmentId)
{
  ApplicationEvent event;
  event.level = level;
  event.category = category;
  event.message = message;
  event.adapterId = adapterId;
  event.equipmentId = equipmentId;
  this->recordEvent(std::move(event));
}

void ApplicationService::recordEvent(ApplicationEvent event)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->recordEventLocked(std::move(event));
}

void ApplicationService::recordEventLocked(ApplicationEvent event)
{
  if (event.atUtc.time_since_epoch().count() == 0)
  {
    event.atUtc = std::chrono::system_clock::now();
  }
  const std::string level = event.level;
  const std::string adapterId = event.adapterId;
  const std::string message = event.message;
  this->ensureHistoryWriter();
  this->persistEventToHistory(event);
  this->events_.push_back(std::move(event));
  while (this->events_.size() > kMaxEvents)
  {
    this->events_.pop_front();
  }
  if (!adapterId.empty() && (level == "warn" || level == "warning"))
  {
    AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
    ++diag.warningCount;
    diag.lastWarning = message;
  }
}

void ApplicationService::emitLifecycleTransitionLocked(
    const std::string &adapterId,
    const std::string &previousState,
    const std::string &newState,
    std::int64_t durationMs) const
{
  const bool recovering = this->reconnect_in_progress_.count(adapterId) != 0
                          && this->reconnect_in_progress_.at(adapterId)
                          && newState == "CONNECTED";

  AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);

  ApplicationEvent ev;
  ev.adapterId = adapterId;
  ev.previousState = previousState;
  ev.newState = newState;
  if (durationMs >= 0)
  {
    ev.durationMs = durationMs;
  }

  if (const AdapterConfigRecord *record = this->catalog_.adapter(adapterId))
  {
    ev.protocol = record->protocol;
  }

  IndustrialAdapter *runtime =
      const_cast<ApplicationService *>(this)->manager_.adapter(adapterId);
  const std::string runtimeError =
      (runtime != nullptr) ? runtime->lastError() : std::string{};

  if (recovering)
  {
    ev.level = "info";
    ev.category = "recovery";
    ev.eventType = "communication_recovered";
    ev.recovery = "successful";
    ev.message = "OPC UA communication recovered" ;
    if (ev.protocol != "opcua")
    {
      ev.message = "Industrial communication recovered";
    }
    ev.message += " (" + previousState + " → CONNECTED)";
    ev.reason = "Automatic or explicit reconnect completed successfully.";
    if (diag.hasFaultedAt)
    {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now() - diag.faultedAt);
      if (elapsed.count() >= 0)
      {
        ev.durationMs = elapsed.count();
      }
    }
    if (!diag.lastFaultError.empty())
    {
      ev.errorDetails = diag.lastFaultError;
    }
    if (!diag.lastFaultNodeId.empty())
    {
      ev.nodeId = diag.lastFaultNodeId;
    }
  }
  else if (newState == "FAULTED")
  {
    ev.level = "error";
    ev.category = "communication";
    ev.eventType = "communication_fault";
    ev.recovery = "pending";
    ev.message = "Industrial communication fault (" + previousState + " → FAULTED)";
    if (ev.protocol == "opcua")
    {
      ev.message = "OPC UA communication fault (" + previousState + " → FAULTED)";
    }
    if (!runtimeError.empty())
    {
      ev.reason = "OPC UA read/connect failed";
      if (ev.protocol != "opcua")
      {
        ev.reason = "Communication/runtime fault observed.";
      }
      ev.errorDetails = runtimeError;
    }
    else
    {
      ev.reason = "Communication/runtime fault observed.";
    }
    enrichCommunicationErrorFields(&ev);
    diag.faultedAt = std::chrono::system_clock::now();
    diag.hasFaultedAt = true;
    diag.lastFaultError = ev.errorDetails.empty() ? runtimeError : ev.errorDetails;
    diag.lastFaultNodeId = ev.nodeId;
    diag.nextAutoReconnectAt =
        std::chrono::steady_clock::now() + diag.autoReconnectBackoffMs;
  }
  else if (newState == "DISCONNECTED" || newState == "NOT_CONFIGURED")
  {
    ev.level = "info";
    ev.category = "connection";
    ev.eventType = "lifecycle_changed";
    ev.message =
        "Adapter disconnected (" + previousState + " → DISCONNECTED)";
    ev.reason = previousState == "FAULTED"
                    ? "Faulted adapter was disconnected."
                    : "Adapter disconnected.";
    ev.newState = "DISCONNECTED";
  }
  else if (newState == "CONNECTED")
  {
    ev.level = "info";
    ev.category = "connection";
    ev.eventType = "lifecycle_changed";
    ev.message = "Adapter connected (" + previousState + " → CONNECTED)";
    ev.reason = previousState == "FAULTED"
                    ? "Adapter returned to CONNECTED after fault."
                    : "Adapter connected.";
    if (previousState == "FAULTED")
    {
      ev.category = "recovery";
      ev.eventType = "communication_recovered";
      ev.recovery = "successful";
      ev.message = (ev.protocol == "opcua"
                        ? "OPC UA communication recovered"
                        : "Industrial communication recovered")
                   + std::string(" (FAULTED → CONNECTED)");
      if (diag.hasFaultedAt)
      {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - diag.faultedAt);
        if (elapsed.count() >= 0)
        {
          ev.durationMs = elapsed.count();
        }
      }
      if (!diag.lastFaultError.empty())
      {
        ev.errorDetails = diag.lastFaultError;
      }
      if (!diag.lastFaultNodeId.empty())
      {
        ev.nodeId = diag.lastFaultNodeId;
      }
    }
  }
  else
  {
    ev.level = "info";
    ev.category = "connection";
    ev.eventType = "lifecycle_changed";
    ev.message =
        "Adapter lifecycle changed (" + previousState + " → " + newState + ")";
  }

  // Capture health transition context when available.
  if (diag.hasEmittedHealth)
  {
    ev.previousHealth = diag.lastEmittedHealth;
  }
  // newHealth filled after observe updates health below; for FAULTED set now.
  if (newState == "FAULTED")
  {
    ev.newHealth = "FAULTED";
  }
  else if (newState == "CONNECTED" && previousState == "FAULTED")
  {
    ev.previousHealth = "FAULTED";
  }

  const_cast<ApplicationService *>(this)->recordEventLocked(std::move(ev));
}


AdapterSessionDiagnostics &
ApplicationService::diagnosticsFor(const std::string &adapterId) const
{
  AdapterSessionDiagnostics &diag = this->adapter_diagnostics_[adapterId];
  if (diag.sessionStartedAt.time_since_epoch().count() == 0)
  {
    const auto now = std::chrono::system_clock::now();
    diag.sessionStartedAt = this->service_started_at_.time_since_epoch().count() == 0
                                ? now
                                : this->service_started_at_;
    diag.lastStateChangeAt = now;
    diag.disconnectedAt = now;
  }
  return diag;
}


void ApplicationService::enrichCommunicationErrorFields(ApplicationEvent *event)
{
  if (event == nullptr || event->errorDetails.empty())
  {
    return;
  }
  const std::string &detail = event->errorDetails;
  // Typical OPC UA adapter text:
  // "OPC UA read failed for ns=2;s=MotorSpeed: BadSecureChannelClosed"
  const std::string forToken = " for ";
  const std::size_t forPos = detail.find(forToken);
  const std::size_t colonPos = detail.rfind(": ");
  if (forPos != std::string::npos && colonPos != std::string::npos
      && colonPos > forPos + forToken.size())
  {
    if (event->nodeId.empty())
    {
      event->nodeId =
          detail.substr(forPos + forToken.size(), colonPos - (forPos + forToken.size()));
    }
    if (event->errorCode.empty())
    {
      event->errorCode = detail.substr(colonPos + 2);
    }
  }
  else if (event->errorCode.empty())
  {
    // Fallback: last token that looks like Bad*
    const std::size_t badPos = detail.rfind("Bad");
    if (badPos != std::string::npos)
    {
      event->errorCode = detail.substr(badPos);
    }
  }
}

void ApplicationService::materializeEnabledAdaptersForRecovery()
{
  // Create missing enabled adapters without connecting. First/peer-down connect
  // is owned exclusively by onPollCycle so ICP HTTP/control plane can start
  // while industrial endpoints are unavailable.
  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    if (!record.enabled)
    {
      continue;
    }
    if (this->manager_.adapter(record.adapterId) != nullptr)
    {
      continue;
    }

    AdapterManagerResult ensured = this->ensureRuntimeAdapter(record);
    if (!ensured.ok)
    {
      ApplicationEvent ev;
      ev.level = "error";
      ev.category = "configuration";
      ev.eventType = "materialize_failed";
      ev.adapterId = record.adapterId;
      ev.protocol = record.protocol;
      ev.message = ensured.message;
      ev.reason = ensured.message;
      this->recordEvent(std::move(ev));
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
      diag.autoConnectDesired = true;
      // Eligible on the next poll cycle — do not sleep/connect on this thread.
      diag.autoReconnectBackoffMs = std::chrono::milliseconds(1000);
      diag.nextAutoReconnectAt = std::chrono::steady_clock::now();
      this->observeAdapterStateLocked(record.adapterId, "DISCONNECTED");
    }
  }
}


void ApplicationService::syncAlarmAndEquipmentHistoryLocked() const
{
  if (!this->history_writer_)
  {
    return;
  }

  const auto now = std::chrono::system_clock::now();
  const std::int64_t nowMs = toEpochMs(now);
  std::unordered_map<std::string, ActiveAlarmView> current;

  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    const AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
    if (diag.activeFault || diag.lastObservedState == "FAULTED")
    {
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "adapter";
      alarm.sourceId = record.adapterId;
      alarm.protocol = record.protocol;
      alarm.category = "CONNECTION";
      alarm.message =
          diag.lastFaultError.empty() ? "Adapter is faulted" : diag.lastFaultError;
      alarm.sinceUtc = diag.hasFaultedAt ? diag.faultedAt : diag.lastStateChangeAt;
      current.emplace(alarmKeyFor(alarm), alarm);
    }
    else if (!diag.earlyWarning.empty() && diag.health == "DEGRADED")
    {
      ActiveAlarmView alarm;
      alarm.severity = "WARNING";
      alarm.sourceType = "adapter";
      alarm.sourceId = record.adapterId;
      alarm.protocol = record.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message = diag.earlyWarning;
      alarm.sinceUtc = diag.lastStateChangeAt;
      current.emplace(alarmKeyFor(alarm), alarm);
    }
  }

  for (const EquipmentSnapshot &snap : this->cache_.equipment())
  {
    if (snap.machineFault)
    {
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "EQUIPMENT";
      alarm.message =
          snap.lastError.empty() ? "Equipment machine fault active" : snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      current.emplace(alarmKeyFor(alarm), alarm);
    }
    else if (snap.stale && snap.communicationState == ConnectionState::Faulted)
    {
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message =
          snap.lastError.empty() ? "Equipment communication faulted" : snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      current.emplace(alarmKeyFor(alarm), alarm);
    }
    else if (
        !snap.lastError.empty()
        && snap.communicationState == ConnectionState::Connected)
    {
      ActiveAlarmView alarm;
      alarm.severity = "WARNING";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message = snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      current.emplace(alarmKeyFor(alarm), alarm);
    }

    const std::string state = equipmentHistoryState(snap);
    auto prior = this->equipment_history_state_.find(snap.equipmentId);
    if (prior == this->equipment_history_state_.end() || prior->second != state)
    {
      this->history_writer_->enqueueEquipmentStateTransition(
          snap.equipmentId,
          snap.adapterId,
          state,
          nowMs,
          "equipment state " + state);
      this->equipment_history_state_[snap.equipmentId] = state;
    }
  }

  // Raise new occurrences only when identity was not already open (no poll dupes).
  for (const auto &entry : current)
  {
    auto existing = this->open_alarms_.find(entry.first);
    if (existing != this->open_alarms_.end())
    {
      // Same ongoing condition: keep occurrence; refresh latest message in memory.
      existing->second.view.message = entry.second.message;
      existing->second.view.severity = entry.second.severity;
      continue;
    }

    AlarmOccurrenceRecord occurrence;
    occurrence.id = this->nextAlarmOccurrenceIdLocked();
    occurrence.alarmKey = entry.first;
    occurrence.severity = entry.second.severity;
    occurrence.sourceType = entry.second.sourceType;
    occurrence.sourceId = entry.second.sourceId;
    if (entry.second.sourceType == "equipment")
    {
      occurrence.equipmentId = entry.second.sourceId;
    }
    else if (entry.second.sourceType == "adapter")
    {
      occurrence.adapterId = entry.second.sourceId;
    }
    occurrence.protocol = entry.second.protocol;
    occurrence.category = entry.second.category;
    occurrence.message = entry.second.message;
    occurrence.raisedAtUtcMs = toEpochMs(
        entry.second.sinceUtc.time_since_epoch().count() == 0 ? now
                                                             : entry.second.sinceUtc);
    occurrence.status = "active";
    this->history_writer_->enqueueAlarmRaise(occurrence);

    OpenAlarmTracking tracking;
    tracking.view = entry.second;
    tracking.occurrenceId = occurrence.id;
    this->open_alarms_.emplace(entry.first, std::move(tracking));
  }

  // Clear recovered identities against the corresponding occurrence instance.
  std::vector<std::string> toErase;
  for (const auto &entry : this->open_alarms_)
  {
    if (current.count(entry.first) == 0)
    {
      this->history_writer_->enqueueAlarmClear(
          entry.second.occurrenceId, nowMs, entry.second.view.message);
      toErase.push_back(entry.first);
    }
  }
  for (const std::string &key : toErase)
  {
    this->open_alarms_.erase(key);
  }
}

ConfigResult ApplicationService::acknowledgeAlarmOccurrence(
    std::int64_t occurrenceId, const std::string &actorId)
{
  ConfigResult result;
  if (occurrenceId <= 0)
  {
    result.ok = false;
    result.message = "occurrenceId is required";
    return result;
  }
  this->ensureHistoryWriter();
  const std::int64_t ts = toEpochMs(std::chrono::system_clock::now());
  // Acknowledge on the HTTP/control path (not under protocol I/O). Synchronous so
  // the caller can observe the retained historical action immediately.
  const bool ok = this->history_writer_->repository()->acknowledgeAlarmOccurrence(
      occurrenceId, ts, actorId);
  if (!ok)
  {
    result.ok = false;
    result.message = this->history_writer_->status().message.empty()
                         ? "Failed to acknowledge alarm occurrence"
                         : this->history_writer_->status().message;
    return result;
  }

  // If this occurrence is currently open in-process, mark tracking as acknowledged
  // without closing it (clear still happens on condition recovery).
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (auto &entry : this->open_alarms_)
    {
      if (entry.second.occurrenceId == occurrenceId)
      {
        break;
      }
    }
  }

  ApplicationEvent ev;
  ev.level = "info";
  ev.category = "alarm";
  ev.eventType = "alarm_acknowledged";
  ev.message = "Alarm occurrence acknowledged";
  ev.correlationId = std::to_string(occurrenceId);
  this->recordEvent(std::move(ev));

  result.ok = true;
  result.message = "acknowledged";
  return result;
}

void ApplicationService::scheduleAutoReconnectLocked(const std::string &adapterId) const
{
  AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
  if (diag.autoReconnectBackoffMs.count() <= 0)
  {
    diag.autoReconnectBackoffMs = std::chrono::milliseconds(1000);
  }
  diag.nextAutoReconnectAt =
      std::chrono::steady_clock::now() + diag.autoReconnectBackoffMs;
}

void ApplicationService::applyConnectOutcome(
    const std::string &adapterId,
    const AdapterManagerResult &connected,
    bool emitFailureEvent,
    bool reconnectStyleFailure)
{
  IndustrialAdapter *runtime = this->manager_.adapter(adapterId);
  const AdapterConfigRecord *record = this->catalog_.adapter(adapterId);
  if (connected.ok && runtime != nullptr)
  {
    this->cache_.updateFromAdapter(*runtime);
    std::lock_guard<std::mutex> lock(this->mutex_);
    AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
    ++diag.successfulConnections;
    diag.autoReconnectInFlight = false;
    diag.autoReconnectBackoffMs = std::chrono::milliseconds(1000);
    diag.nextAutoReconnectAt = {};
    this->observeAdapterStateLocked(adapterId, "CONNECTED");
    for (const EquipmentSnapshot &snap : this->cache_.equipment())
    {
      if (snap.adapterId == adapterId && snap.hasSuccessfulCommunication)
      {
        diag.hasLastSuccessfulCommunication = true;
        diag.lastSuccessfulCommunicationAt = snap.lastSuccessfulCommunicationUtc;
      }
    }
    this->reconnect_in_progress_.erase(adapterId);
    return;
  }

  if (runtime != nullptr)
  {
    this->cache_.updateFromAdapter(*runtime);
    this->cache_.markAdapterCommunication(
        adapterId, runtime->connectionState(), runtime->lastError());
  }

  if (emitFailureEvent && runtime != nullptr && record != nullptr)
  {
    std::string message = connected.message;
    if (adapterImplementation(*record) == "hilscher_native")
    {
      const HilscherDiagnosticsView hilscher = this->hilscherDiagnostics();
      if (hilscher.boardCount == 0 || !hilscher.compiledIn
          || hilscher.readinessState == "NO_BOARD"
          || hilscher.readinessState == "SDK_MISSING"
          || message.find("BLOCKED") != std::string::npos
          || message.find("HARDWARE") != std::string::npos
          || message.find("no cifX") != std::string::npos
          || message.find("SDK") != std::string::npos
          || message.find("artifact") != std::string::npos
          || message.find("stub") != std::string::npos)
      {
        message = "Hilscher hardware not detected. " + message;
        if (!runtime->lastError().empty()
            && message.find(runtime->lastError()) == std::string::npos)
        {
          message += " (" + runtime->lastError() + ")";
        }
      }
    }
    ApplicationEvent ev;
    ev.level = "error";
    ev.category = reconnectStyleFailure ? "recovery" : "connection";
    ev.eventType =
        reconnectStyleFailure ? "reconnect_failed" : "connect_failed";
    ev.message = reconnectStyleFailure
                     ? ("Adapter reconnect failed: " + message)
                     : message;
    ev.adapterId = adapterId;
    ev.protocol = record->protocol;
    ev.reason = message;
    ev.errorDetails = runtime->lastError();
    if (reconnectStyleFailure)
    {
      ev.recovery = "failed";
    }
    this->recordEvent(std::move(ev));
  }

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
    diag.autoReconnectInFlight = false;
    ++diag.failedConnections;
    this->observeAdapterStateLocked(
        adapterId,
        runtime != nullptr ? connectionStateName(runtime->connectionState())
                           : std::string("FAULTED"));
    if (diag.autoConnectDesired)
    {
      auto next = diag.autoReconnectBackoffMs * 2;
      if (next > std::chrono::milliseconds(10000))
      {
        next = std::chrono::milliseconds(10000);
      }
      if (next < std::chrono::milliseconds(1000))
      {
        next = std::chrono::milliseconds(1000);
      }
      diag.autoReconnectBackoffMs = next;
      diag.nextAutoReconnectAt = std::chrono::steady_clock::now() + next;
    }
    this->reconnect_in_progress_.erase(adapterId);
  }
}

void ApplicationService::executeLifecycleJob(const LifecycleJob &job)
{
  if (job.adapterId.empty())
  {
    return;
  }

  auto clearFlight = [this](const std::string &adapterId) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->diagnosticsFor(adapterId).autoReconnectInFlight = false;
    this->reconnect_in_progress_.erase(adapterId);
  };

  if (job.generation != this->lifecycle_.generation(job.adapterId))
  {
    clearFlight(job.adapterId);
    return;
  }

  if (job.op == LifecycleOp::Disconnect)
  {
    AdapterManagerResult result = this->manager_.disconnectAdapter(job.adapterId);
    this->cache_.removeAdapterEquipment(job.adapterId);
    if (result.ok)
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->diagnosticsFor(job.adapterId).autoConnectDesired = false;
      this->diagnosticsFor(job.adapterId).autoReconnectInFlight = false;
      this->observeAdapterStateLocked(job.adapterId, "DISCONNECTED");
    }
    return;
  }

  if (job.op == LifecycleOp::Reconnect)
  {
    (void)this->manager_.disconnectAdapter(job.adapterId);
    this->cache_.removeAdapterEquipment(job.adapterId);
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->observeAdapterStateLocked(job.adapterId, "DISCONNECTED");
    }
    if (job.generation != this->lifecycle_.generation(job.adapterId))
    {
      clearFlight(job.adapterId);
      return;
    }
    bool desired = false;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      desired = this->diagnosticsFor(job.adapterId).autoConnectDesired;
    }
    if (!desired)
    {
      clearFlight(job.adapterId);
      return;
    }
  }

  if (job.op != LifecycleOp::Connect && job.op != LifecycleOp::RecoveryConnect
      && job.op != LifecycleOp::Reconnect)
  {
    return;
  }

  bool desired = false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    desired = this->diagnosticsFor(job.adapterId).autoConnectDesired;
    if (job.op == LifecycleOp::RecoveryConnect)
    {
      IndustrialAdapter *before = this->manager_.adapter(job.adapterId);
      if (before != nullptr
          && before->connectionState() == ConnectionState::Faulted)
      {
        this->reconnect_in_progress_[job.adapterId] = true;
      }
    }
  }
  if (!desired)
  {
    clearFlight(job.adapterId);
    return;
  }

  AdapterManagerResult connected = this->manager_.connectAdapter(job.adapterId);

  bool stale = job.generation != this->lifecycle_.generation(job.adapterId);
  bool stillDesired = false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    stillDesired = this->diagnosticsFor(job.adapterId).autoConnectDesired;
  }
  if (stale || !stillDesired)
  {
    if (connected.ok)
    {
      (void)this->manager_.disconnectAdapter(job.adapterId);
      this->cache_.removeAdapterEquipment(job.adapterId);
    }
    clearFlight(job.adapterId);
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->observeAdapterStateLocked(job.adapterId, "DISCONNECTED");
    }
    return;
  }

  const bool emitFailure =
      job.op == LifecycleOp::Connect || job.op == LifecycleOp::Reconnect;
  const bool reconnectStyle = job.op == LifecycleOp::Reconnect;
  this->applyConnectOutcome(
      job.adapterId, connected, emitFailure, reconnectStyle);
}

void ApplicationService::onPollCycle()
{
  std::vector<std::string> due;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->running_)
    {
      return;
    }
    this->refreshAllAdapterObservationsLocked();
    this->syncAlarmAndEquipmentHistoryLocked();

    const auto now = std::chrono::steady_clock::now();
    for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
    {
      if (!record.enabled)
      {
        continue;
      }
      IndustrialAdapter *runtime = this->manager_.adapter(record.adapterId);
      if (runtime == nullptr)
      {
        continue;
      }
      AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
      const ConnectionState state = runtime->connectionState();
      const bool eligible =
          state == ConnectionState::Faulted
          || (state == ConnectionState::Disconnected && diag.autoConnectDesired);
      if (!eligible)
      {
        continue;
      }
      if (diag.autoReconnectInFlight)
      {
        continue;
      }
      // Explicit GUI/API reconnect owns the attempt.
      if (this->reconnect_in_progress_.count(record.adapterId) != 0
          && this->reconnect_in_progress_.at(record.adapterId))
      {
        continue;
      }
      if (diag.nextAutoReconnectAt.time_since_epoch().count() == 0)
      {
        diag.nextAutoReconnectAt = now + diag.autoReconnectBackoffMs;
      }
      if (now >= diag.nextAutoReconnectAt)
      {
        diag.autoReconnectInFlight = true;
        due.push_back(record.adapterId);
      }
    }
  }

  for (const std::string &adapterId : due)
  {
    // Enqueue only — never call blocking connect() on the poll thread.
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      ++this->diagnosticsFor(adapterId).autoReconnectAttempts;
      ++this->diagnosticsFor(adapterId).reconnectCount;
    }

    LifecycleJob job;
    job.adapterId = adapterId;
    job.op = LifecycleOp::RecoveryConnect;
    job.generation = this->lifecycle_.generation(adapterId);
    if (!this->lifecycle_.enqueue(std::move(job)))
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->diagnosticsFor(adapterId).autoReconnectInFlight = false;
    }
  }
}

void ApplicationService::observeAdapterStateLocked(
    const std::string &adapterId, const std::string &connectionState) const
{
  AdapterSessionDiagnostics &diag = this->diagnosticsFor(adapterId);
  const auto now = std::chrono::system_clock::now();
  const std::string state =
      connectionState.empty() ? std::string("DISCONNECTED") : connectionState;

  std::int64_t leftStateDurationMs = -1;

  // Accumulate session totals only when leaving a state. Re-adding
  // (now - lastStateChangeAt) on every observation caused epoch-scale garbage
  // in cumulativeConnectedMs / cumulativeDisconnectedMs charts.
  if (state != diag.lastObservedState)
  {
    const std::string previous = diag.lastObservedState;
    if (diag.lastStateChangeAt.time_since_epoch().count() != 0)
    {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - diag.lastStateChangeAt);
      if (elapsed.count() > 0)
      {
        leftStateDurationMs = elapsed.count();
        if (diag.lastObservedState == "CONNECTED")
        {
          diag.cumulativeConnectedMs += elapsed;
        }
        else if (
            diag.lastObservedState == "DISCONNECTED"
            || diag.lastObservedState == "NOT_CONFIGURED"
            || diag.lastObservedState == "FAULTED")
        {
          diag.cumulativeDisconnectedMs += elapsed;
        }
      }
    }

    if (state == "FAULTED" && diag.lastObservedState != "FAULTED")
    {
      ++diag.faultCount;
      diag.activeFault = true;
      ++diag.communicationFailureCount;
      if (!diag.hasFaultedAt)
      {
        diag.faultedAt = now;
        diag.hasFaultedAt = true;
      }
      else
      {
        diag.faultedAt = now;
      }
      IndustrialAdapter *runtime =
          const_cast<ApplicationService *>(this)->manager_.adapter(adapterId);
      if (runtime != nullptr && !runtime->lastError().empty())
      {
        diag.lastFaultError = runtime->lastError();
      }
      if (diag.autoReconnectBackoffMs.count() <= 0)
      {
        diag.autoReconnectBackoffMs = std::chrono::milliseconds(1000);
      }
      diag.nextAutoReconnectAt =
          std::chrono::steady_clock::now() + diag.autoReconnectBackoffMs;
    }
    if (state == "CONNECTED")
    {
      diag.connectedAt = now;
      diag.everConnected = true;
      diag.activeFault = false;
      diag.autoReconnectInFlight = false;
      diag.autoReconnectAttempts = 0;
      diag.autoReconnectBackoffMs = std::chrono::milliseconds(1000);
      diag.nextAutoReconnectAt = {};
      // Do not mark successful communication on connect alone; wait for observed
      // Connected poll/cache refresh evidence.
    }
    if (state == "DISCONNECTED" || state == "NOT_CONFIGURED")
    {
      diag.disconnectedAt = now;
      diag.activeFault = false;
    }
    if (state == "FAULTED")
    {
      diag.disconnectedAt = now;
    }
    diag.lastStateChangeAt = now;
    diag.lastObservedState = state;

    this->emitLifecycleTransitionLocked(
        adapterId, previous, state, leftStateDurationMs);

    if (this->history_writer_)
    {
      std::string protocol;
      if (const AdapterConfigRecord *record = this->catalog_.adapter(adapterId))
      {
        protocol = record->protocol;
      }
      std::string reason = previous + " → " + state;
      std::string errorCode;
      IndustrialAdapter *runtime =
          const_cast<ApplicationService *>(this)->manager_.adapter(adapterId);
      if (runtime != nullptr && state == "FAULTED" && !runtime->lastError().empty())
      {
        ApplicationEvent tmp;
        tmp.errorDetails = runtime->lastError();
        enrichCommunicationErrorFields(&tmp);
        errorCode = tmp.errorCode;
        reason += "; " + runtime->lastError();
        if (reason.size() > 256)
        {
          reason.resize(256);
        }
      }
      this->history_writer_->enqueueCommunicationTransition(
          adapterId, protocol, state, toEpochMs(now), reason, errorCode);
    }
  }
  else if (state == "CONNECTED")
  {
    diag.activeFault = false;
  }

  // Separate communication lifecycle from health.
  if (state == "CONNECTED")
  {
    diag.communicationLifecycleState = "CONNECTED";
  }
  else if (state == "FAULTED")
  {
    diag.communicationLifecycleState = "FAULTED";
  }
  else if (state == "DISCONNECTED" || state == "NOT_CONFIGURED")
  {
    diag.communicationLifecycleState = "DISCONNECTED";
  }
  else
  {
    diag.communicationLifecycleState = "UNKNOWN";
  }

  // Active early warning / DEGRADED must reflect CURRENT conditions so recovery
  // clears stale alarms. Session reconnect/fault counters remain in reliability
  // stats and event history; they alone must not permanently force DEGRADED.
  // Soft application-level failures (e.g. BadNodeIdUnknown) keep CONNECTED and
  // set lastError — that is DEGRADED health, not transport FAULTED.
  diag.earlyWarning.clear();
  diag.healthReason.clear();
  std::string runtimeLastError;
  if (IndustrialAdapter *runtime =
          const_cast<ApplicationService *>(this)->manager_.adapter(adapterId))
  {
    runtimeLastError = runtime->lastError();
  }
  if (state == "FAULTED")
  {
    diag.health = "FAULTED";
    diag.healthReason = "Adapter communication/runtime is faulted.";
  }
  else if (state == "CONNECTED")
  {
    if (!runtimeLastError.empty())
    {
      diag.health = "DEGRADED";
      diag.earlyWarning = runtimeLastError;
      diag.healthReason =
          "Connected, but application-level communication failed: "
          + runtimeLastError;
    }
    else if (!diag.hasLastSuccessfulCommunication)
    {
      diag.health = "UNKNOWN";
      diag.healthReason =
          "Connected, but no successful communication has been observed yet.";
    }
    else if (diag.failedConnections >= 3 && diag.successfulConnections == 0)
    {
      // Defensive: connected without a counted success is unusual; treat as degraded.
      diag.health = "DEGRADED";
      diag.earlyWarning =
          "Communication degradation detected: repeated connection failures "
          "with no successful connection counted in this session.";
      diag.healthReason = diag.earlyWarning;
    }
    else
    {
      diag.health = "HEALTHY";
      diag.healthReason = "Connected with observed successful communication.";
    }
  }
  else
  {
    diag.health = "UNKNOWN";
    diag.healthReason = "Adapter is not connected; health is not evaluated.";
    if (diag.failedConnections >= 3 && diag.successfulConnections == 0)
    {
      diag.earlyWarning =
          "Communication degradation detected: repeated connection failures "
          "with no successful connection in this session.";
    }
  }

  // Health transition events only when health actually changes.
  if (!diag.hasEmittedHealth || diag.health != diag.lastEmittedHealth)
  {
    const std::string previousHealth =
        diag.hasEmittedHealth ? diag.lastEmittedHealth : std::string("UNKNOWN");
    if (diag.hasEmittedHealth || diag.health != "UNKNOWN")
    {
      ApplicationEvent ev;
      ev.level = diag.health == "FAULTED"     ? "error"
                 : diag.health == "DEGRADED"  ? "warning"
                                              : "info";
      ev.category = "health";
      ev.eventType = "health_changed";
      ev.adapterId = adapterId;
      ev.previousHealth = previousHealth;
      ev.newHealth = diag.health;
      ev.reason = diag.healthReason;
      ev.message = "Adapter health " + previousHealth + " → " + diag.health;
      if (const AdapterConfigRecord *record = this->catalog_.adapter(adapterId))
      {
        ev.protocol = record->protocol;
      }
      const_cast<ApplicationService *>(this)->recordEventLocked(std::move(ev));
    }
    diag.lastEmittedHealth = diag.health;
    diag.hasEmittedHealth = true;
  }
}

void ApplicationService::refreshAllAdapterObservationsLocked() const
{
  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    std::string state = "DISCONNECTED";
    IndustrialAdapter *runtime =
        const_cast<ApplicationService *>(this)->manager_.adapter(record.adapterId);
    if (runtime != nullptr)
    {
      state = connectionStateName(runtime->connectionState());
      // Soft application-level failures set lastError while remaining CONNECTED.
      // Sync equipment cache lastError immediately so diagnostics/equipment health
      // do not wait for the next poll refresh (and clear equally promptly).
      if (state == "CONNECTED")
      {
        bool needsSync = false;
        for (const EquipmentSnapshot &snap : this->cache_.equipment())
        {
          if (snap.adapterId == record.adapterId
              && snap.lastError != runtime->lastError())
          {
            needsSync = true;
            break;
          }
        }
        if (needsSync)
        {
          const_cast<LiveStateCache &>(this->cache_)
              .markAdapterCommunication(
                  record.adapterId,
                  ConnectionState::Connected,
                  runtime->lastError());
        }
      }
    }
    this->observeAdapterStateLocked(record.adapterId, state);

    if (state == "CONNECTED")
    {
      AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
      bool sawGood = false;
      auto latestGood = diag.lastSuccessfulCommunicationAt;
      for (const EquipmentSnapshot &snap : this->cache_.equipment())
      {
        if (snap.adapterId != record.adapterId)
        {
          continue;
        }
        if (snap.hasSuccessfulCommunication)
        {
          sawGood = true;
          if (snap.lastSuccessfulCommunicationUtc > latestGood)
          {
            latestGood = snap.lastSuccessfulCommunicationUtc;
          }
        }
      }
      if (sawGood)
      {
        diag.hasLastSuccessfulCommunication = true;
        diag.lastSuccessfulCommunicationAt = latestGood;
        // Recompute health now that communication evidence exists.
        this->observeAdapterStateLocked(record.adapterId, state);
      }
    }
  }
}

DiagnosticsReport ApplicationService::diagnosticsReport() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  // Do not refresh from live adapter fields here. Poll owns
  // refreshAllAdapterObservationsLocked() after releasing io_mutex. HTTP
  // diagnostics must stay on the cached snapshot so reconnect EventLoop/
  // UA_Client work cannot block or race GUI control-plane reads.

  DiagnosticsReport report;
  report.generatedAtUtc = std::chrono::system_clock::now();
  report.system.schedulerRunning = this->running_ && this->scheduler_
                                   && this->scheduler_->running();
  report.system.configuredAdapters = this->catalog_.adapterCount();

  const auto snapshots = this->cache_.equipment();
  report.equipment = snapshots;

  std::size_t historicalFaults = 0;
  std::size_t warningTotal = 0;

  for (const AdapterConfigRecord &record : this->catalog_.document().adapters)
  {
    AdapterDiagnosticsView view;
    view.adapter.adapterId = record.adapterId;
    view.adapter.protocol = record.protocol;
    view.adapter.configured = true;
    view.adapter.enabled = record.enabled;
    view.adapter.description = record.description;
    view.adapter.equipmentCount = record.equipment.size();
    view.adapter.connectionState = "DISCONNECTED";
    view.adapter.implementation = adapterImplementation(record);
    view.adapter.connectionSummary = connectionSummary(record);
    if (record.protocol == "modbus")
    {
      const std::string transport =
          record.connection.transport.empty() ? "tcp" : record.connection.transport;
      view.adapter.transport = transport == "rtu" ? "rtu" : "tcp";
    }

    view.adapter.runtimePresent =
        this->manager_.adapter(record.adapterId) != nullptr;
    const AdapterSessionDiagnostics &diag = this->diagnosticsFor(record.adapterId);
    if (!diag.lastObservedState.empty())
    {
      view.adapter.connectionState = diag.lastObservedState;
    }
    if (view.adapter.connectionState == "FAULTED")
    {
      view.adapter.lastError = diag.lastFaultError;
    }
    else if (!diag.earlyWarning.empty())
    {
      view.adapter.lastError = diag.earlyWarning;
    }
    view.adapter.connectionStateDisplay =
        connectionStateDisplay(view.adapter.protocol, view.adapter.connectionState);

    view.session = diag;

    const auto now = report.generatedAtUtc;
    if (view.session.lastStateChangeAt.time_since_epoch().count() != 0)
    {
      view.currentStateDurationMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now - view.session.lastStateChangeAt)
              .count();
    }
    if (view.adapter.connectionState == "CONNECTED")
    {
      view.currentUptimeMs = view.currentStateDurationMs;
      view.currentDowntimeMs = 0;
      // Include the open CONNECTED interval in session totals for charts.
      if (view.currentStateDurationMs > 0)
      {
        view.session.cumulativeConnectedMs +=
            std::chrono::milliseconds(view.currentStateDurationMs);
      }
      ++report.system.connectedAdapters;
    }
    else if (view.adapter.connectionState == "FAULTED")
    {
      view.currentUptimeMs = 0;
      view.currentDowntimeMs = view.currentStateDurationMs;
      if (view.currentStateDurationMs > 0)
      {
        view.session.cumulativeDisconnectedMs +=
            std::chrono::milliseconds(view.currentStateDurationMs);
      }
      ++report.system.faultedAdapters;
    }
    else
    {
      view.currentUptimeMs = 0;
      view.currentDowntimeMs = view.currentStateDurationMs;
      if (view.currentStateDurationMs > 0)
      {
        view.session.cumulativeDisconnectedMs +=
            std::chrono::milliseconds(view.currentStateDurationMs);
      }
      ++report.system.disconnectedAdapters;
    }

    for (const EquipmentSnapshot &snap : snapshots)
    {
      if (snap.adapterId != record.adapterId)
      {
        continue;
      }
      if (snap.machineFault || snap.communicationState == ConnectionState::Faulted)
      {
        ++view.faultedEquipmentCount;
      }
      else if (
          snap.stale || snap.communicationState != ConnectionState::Connected
          || !snap.lastError.empty())
      {
        ++view.degradedEquipmentCount;
      }
      else
      {
        ++view.healthyEquipmentCount;
      }
    }

    if (view.session.faultCount > 0
        && view.session.cumulativeConnectedMs.count() > 0)
    {
      view.sessionMtbfStatus = "session_only";
      view.sessionMtbfMs =
          view.session.cumulativeConnectedMs.count()
          / static_cast<std::int64_t>(view.session.faultCount);
    }
    else
    {
      view.sessionMtbfStatus = "insufficient_data";
      view.sessionMtbfMs = 0;
    }

    // Observational DEGRADED: connected with prior success, but live equipment
    // evidence shows stale/incomplete communication (intermittent path).
    if (view.session.health == "HEALTHY"
        && (view.degradedEquipmentCount > 0 || view.faultedEquipmentCount > 0))
    {
      view.session.health = "DEGRADED";
      view.session.earlyWarning =
          "Communication degradation detected: one or more associated equipment "
          "points are stale or not fully communicating while the adapter is connected.";
      view.session.healthReason = view.session.earlyWarning;
    }

    if (view.session.health == "HEALTHY")
    {
      ++report.system.healthyAdapters;
    }
    else if (view.session.health == "DEGRADED")
    {
      ++report.system.degradedAdapters;
    }
    else if (view.session.health == "FAULTED")
    {
      ++report.system.faultedHealthAdapters;
    }
    else
    {
      ++report.system.unknownHealthAdapters;
    }

    historicalFaults += view.session.faultCount;
    warningTotal += view.session.warningCount;

    if (view.session.activeFault || view.adapter.connectionState == "FAULTED")
    {
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "adapter";
      alarm.sourceId = view.adapter.adapterId;
      alarm.protocol = view.adapter.protocol;
      alarm.category = "CONNECTION";
      alarm.message = view.adapter.lastError.empty() ? "Adapter is faulted"
                                                     : view.adapter.lastError;
      alarm.sinceUtc = view.session.lastStateChangeAt;
      report.activeAlarms.push_back(std::move(alarm));
    }
    else if (
        !view.session.earlyWarning.empty() && view.session.health == "DEGRADED")
    {
      ActiveAlarmView alarm;
      alarm.severity = "WARNING";
      alarm.sourceType = "adapter";
      alarm.sourceId = view.adapter.adapterId;
      alarm.protocol = view.adapter.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message = view.session.earlyWarning;
      alarm.sinceUtc = view.session.lastStateChangeAt;
      report.activeAlarms.push_back(std::move(alarm));
    }

    report.adapters.push_back(std::move(view));
  }

  for (const EquipmentSnapshot &snap : snapshots)
  {
    if (snap.machineFault)
    {
      ++report.system.faultedEquipment;
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "EQUIPMENT";
      alarm.message = snap.lastError.empty() ? "Equipment machine fault active"
                                             : snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      report.activeAlarms.push_back(std::move(alarm));
    }
    else if (snap.stale && snap.communicationState == ConnectionState::Faulted)
    {
      ++report.system.degradedEquipment;
      ActiveAlarmView alarm;
      alarm.severity = "ERROR";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message = snap.lastError.empty() ? "Equipment communication faulted"
                                             : snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      report.activeAlarms.push_back(std::move(alarm));
    }
    else if (
        snap.communicationState == ConnectionState::Connected
        && !snap.lastError.empty())
    {
      ++report.system.degradedEquipment;
      ActiveAlarmView alarm;
      alarm.severity = "WARNING";
      alarm.sourceType = "equipment";
      alarm.sourceId = snap.equipmentId;
      alarm.protocol = snap.protocol;
      alarm.category = "COMMUNICATION";
      alarm.message = snap.lastError;
      alarm.sinceUtc = snap.observedAtUtc;
      report.activeAlarms.push_back(std::move(alarm));
    }
    else if (snap.stale)
    {
      ++report.system.degradedEquipment;
    }
    else
    {
      ++report.system.healthyEquipment;
    }
  }

  const ConfigResult validation = this->catalog_.validate();
  if (!validation.ok)
  {
    ActiveAlarmView alarm;
    alarm.severity = "ERROR";
    alarm.sourceType = "configuration";
    alarm.sourceId = this->configuration_path_;
    alarm.category = "CONFIGURATION";
    alarm.message = validation.message.empty() ? "Configuration validation failed"
                                               : validation.message;
    alarm.sinceUtc = report.generatedAtUtc;
    report.activeAlarms.push_back(std::move(alarm));
  }

  report.system.warningCount = warningTotal;
  report.system.historicalFaultCount = historicalFaults;
  report.system.activeAlarmCount = report.activeAlarms.size();
  report.system.mtbfStatus = "insufficient_data";
  report.system.mtbfNote =
      "Long-term MTBF requires persistent history across sessions. "
      "Only session-scoped estimates are available in this process.";

  if (report.system.faultedAdapters > 0 || report.system.faultedHealthAdapters > 0
      || report.system.faultedEquipment > 0)
  {
    report.system.overallHealth = "FAULTED";
  }
  else if (
      report.system.degradedAdapters > 0 || report.system.degradedEquipment > 0
      || !report.activeAlarms.empty())
  {
    report.system.overallHealth = "DEGRADED";
  }
  else if (report.system.configuredAdapters == 0)
  {
    report.system.overallHealth = "UNKNOWN";
  }
  else if (report.system.unknownHealthAdapters > 0 && report.system.healthyAdapters == 0)
  {
    report.system.overallHealth = "UNKNOWN";
  }
  else
  {
    report.system.overallHealth = "HEALTHY";
  }

  // ICP software self-diagnostics (independent of industrial adapter connection).
  report.icp.serviceRunning = this->running_;
  report.icp.schedulerRunning = report.system.schedulerRunning;
  report.icp.apiReachable = true;
  report.icp.configurationValid = validation.ok;
  report.icp.configurationMessage =
      validation.ok ? "Configuration validates." : validation.message;
  report.icp.eventBufferSize = this->events_.size();
  report.icp.eventBufferCapacity = kMaxEvents;
  report.icp.configuredAdapters = this->catalog_.adapterCount();
  report.icp.runtimeAdapters = this->manager_.adapterCount();
  report.icp.liveEquipmentCount = snapshots.size();
  report.icp.applicationUptimeMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          report.generatedAtUtc - this->service_started_at_)
          .count();
  report.icp.checks.clear();
  report.icp.checks.push_back(
      report.icp.serviceRunning ? "ICP service: running" : "ICP service: stopped");
  report.icp.checks.push_back(
      report.icp.schedulerRunning ? "Poll scheduler: running"
                                  : "Poll scheduler: not running");
  report.icp.checks.push_back("Application API: reachable");
  report.icp.checks.push_back(
      report.icp.configurationValid ? "Configuration: valid"
                                    : "Configuration: invalid");
  report.icp.checks.push_back(
      "Event buffer: " + std::to_string(report.icp.eventBufferSize) + "/"
      + std::to_string(report.icp.eventBufferCapacity));
  report.icp.checks.push_back(
      "Live-state cache equipment: " + std::to_string(report.icp.liveEquipmentCount));
  report.icp.checks.push_back(
      "Runtime adapters: " + std::to_string(report.icp.runtimeAdapters) + "/"
      + std::to_string(report.icp.configuredAdapters) + " configured");

  if (!report.icp.serviceRunning || !report.icp.configurationValid)
  {
    report.icp.overallHealth = "FAULTED";
  }
  else if (this->running_ && !report.icp.schedulerRunning)
  {
    report.icp.overallHealth = "DEGRADED";
  }
  else if (!report.icp.serviceRunning)
  {
    report.icp.overallHealth = "UNKNOWN";
  }
  else
  {
    report.icp.overallHealth = "HEALTHY";
  }

  // Newest events last in deque; expose newest-first for operators.
  report.recentEvents.assign(this->events_.rbegin(), this->events_.rend());
  if (report.recentEvents.size() > 100)
  {
    report.recentEvents.resize(100);
  }
  return report;
}

AdapterManagerResult ApplicationService::ensureRuntimeAdapter(
    const AdapterConfigRecord &record)
{
  // Build the replacement first. Never remove the live adapter until the new
  // instance is ready — otherwise a create failure leaves runtimeAdapters=0 and
  // the GUI reports DISCONNECTED with no recoverable runtime object.
  std::string error;
  std::unique_ptr<IndustrialAdapter> adapter =
      this->createRuntimeAdapter(record, &error);
  if (!adapter)
  {
    AdapterManagerResult result;
    result.ok = false;
    result.message = error.empty() ? "failed to create runtime adapter" : error;
    return result;
  }

  if (this->manager_.adapter(record.adapterId) != nullptr)
  {
    this->manager_.removeAdapter(record.adapterId);
    this->cache_.removeAdapterEquipment(record.adapterId);
  }

  return this->manager_.addAdapter(std::move(adapter));
}

std::unique_ptr<IndustrialAdapter> ApplicationService::createRuntimeAdapter(
    const AdapterConfigRecord &record, std::string *error) const
{
  if (record.protocol == "mock")
  {
    auto mock = AdapterFactory::createMock(record.adapterId);
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      mock->addDevice(eq.equipmentId, eq.type.empty() ? "device" : eq.type);
      for (const std::string &capability : eq.capabilities)
      {
        mock->addCapability(eq.equipmentId, capability);
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        mock->addCapability(eq.equipmentId, command.command);
      }
      double seed = 1.0;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        mock->setSourceTelemetry(
            eq.equipmentId, tel.name, seed, tel.unit.empty() ? "" : tel.unit);
        seed += 1.25;
      }
      if (eq.telemetry.empty())
      {
        mock->setSourceTelemetry(eq.equipmentId, "value", 1.0, "");
      }
    }
    if (record.equipment.empty())
    {
      mock->addDevice(record.adapterId + "-EQ-001", "mock_device");
      mock->addCapability(record.adapterId + "-EQ-001", "start");
      mock->addCapability(record.adapterId + "-EQ-001", "stop");
      mock->setSourceTelemetry(record.adapterId + "-EQ-001", "value", 42.0, "");
    }
    return mock;
  }

  if (record.protocol == "opcua")
  {
    OpcUaAdapterConfig config;
    config.endpointUrl = record.connection.endpointUrl;
    config.timeoutMs =
        record.connection.timeoutMs > 0 ? record.connection.timeoutMs : 2000;
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      OpcUaEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        mapped.telemetry.push_back(
            {tel.name,
             mapOpcUaAddress(
                 eq.equipmentId, tel.name, tel.namespaceIndex, tel.address),
             tel.unit});
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        mapped.commands.push_back(
            {command.command,
             mapOpcUaAddress(
                 eq.equipmentId, command.command, command.namespaceIndex,
                 command.address)});
      }
      if (eq.state.mapped)
      {
        mapped.stateNode = mapOpcUaAddress(
            eq.equipmentId, "state", eq.state.namespaceIndex, eq.state.address);
      }
      if (eq.fault.mapped)
      {
        mapped.faultNode = mapOpcUaAddress(
            eq.equipmentId, "fault", eq.fault.namespaceIndex, eq.fault.address);
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createOpcUa(record.adapterId, std::move(config));
  }

  if (record.protocol == "modbus")
  {
    ModbusAdapterConfig config;
    const std::string transport =
        record.connection.transport.empty() ? "tcp" : record.connection.transport;
    if (transport == "rtu")
    {
      config.transport = ModbusTransport::Rtu;
      config.serialDevice = record.connection.serialDevice;
      config.baudRate =
          record.connection.baudRate > 0 ? record.connection.baudRate : 9600;
      config.dataBits =
          record.connection.dataBits > 0 ? record.connection.dataBits : 8;
      config.stopBits =
          record.connection.stopBits > 0 ? record.connection.stopBits : 1;
      const std::string parity = record.connection.parity;
      if (parity == "even" || parity == "E" || parity == "e")
      {
        config.parity = 'E';
      }
      else if (parity == "odd" || parity == "O" || parity == "o")
      {
        config.parity = 'O';
      }
      else
      {
        config.parity = 'N';
      }
      config.linkUnitId =
          record.connection.unitId == 0 ? 1 : record.connection.unitId;
    }
    else
    {
      config.transport = ModbusTransport::Tcp;
      config.host = record.connection.host;
      config.port = record.connection.port == 0 ? 502 : record.connection.port;
    }
    config.timeoutMs =
        record.connection.timeoutMs > 0 ? record.connection.timeoutMs : 2000;
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      ModbusEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        ModbusTelemetryMapping point;
        point.name = tel.name;
        point.unit = tel.unit;
        point.source = makeModbusRef(
            tel.unitId == 0 ? (record.connection.unitId == 0 ? 1 : record.connection.unitId)
                            : tel.unitId,
            parseModbusTable(tel.table),
            tel.registerAddress);
        mapped.telemetry.push_back(point);
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        ModbusCommandMapping mappedCommand;
        mappedCommand.command = command.command;
        mappedCommand.target = makeModbusRef(
            command.unitId == 0
                ? (record.connection.unitId == 0 ? 1 : record.connection.unitId)
                : command.unitId,
            parseModbusTable(command.table),
            command.registerAddress);
        mapped.commands.push_back(mappedCommand);
      }
      if (eq.state.mapped)
      {
        mapped.stateCoil = makeModbusRef(
            eq.state.unitId == 0
                ? (record.connection.unitId == 0 ? 1 : record.connection.unitId)
                : eq.state.unitId,
            parseModbusTable(eq.state.table.empty() ? "coil" : eq.state.table),
            eq.state.registerAddress);
      }
      if (eq.fault.mapped)
      {
        mapped.faultCoil = makeModbusRef(
            eq.fault.unitId == 0
                ? (record.connection.unitId == 0 ? 1 : record.connection.unitId)
                : eq.fault.unitId,
            parseModbusTable(eq.fault.table.empty() ? "coil" : eq.fault.table),
            eq.fault.registerAddress);
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createModbus(record.adapterId, std::move(config));
  }

  if (record.protocol == "mqtt")
  {
    MqttAdapterConfig config;
    config.host = record.connection.host;
    config.port = record.connection.port == 0 ? 1883 : record.connection.port;
    config.clientId = record.connection.clientId.empty()
                          ? record.adapterId
                          : record.connection.clientId;
    config.keepaliveSeconds = record.connection.keepaliveSeconds > 0
                                  ? record.connection.keepaliveSeconds
                                  : 30;
    config.pollTimeoutMs = record.connection.pollTimeoutMs > 0
                               ? record.connection.pollTimeoutMs
                               : 100;
    config.useTls = record.connection.useTls;
    config.tlsVerify = record.connection.tlsVerify;
    config.username = record.credentials.username;
    // Secrets are references only — not resolved in this GUI milestone.
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      MqttEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        MqttTelemetryMapping point;
        point.name = tel.name;
        point.topic = tel.address;
        point.unit = tel.unit;
        point.encoding = parseMqttEncoding(tel.encoding);
        point.jsonPointer = tel.jsonPointer;
        point.qos = tel.qos;
        mapped.telemetry.push_back(point);
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        MqttCommandMapping mappedCommand;
        mappedCommand.command = command.command;
        mappedCommand.topic = command.address;
        mappedCommand.bodyTemplate = command.bodyTemplate;
        mappedCommand.qos = command.qos;
        mappedCommand.retain = command.retain;
        mapped.commands.push_back(mappedCommand);
      }
      if (eq.state.mapped)
      {
        mapped.state = makeMqttSignal(
            eq.state.address,
            parseMqttEncoding(eq.state.encoding),
            eq.state.jsonPointer);
        mapped.state.qos = eq.state.qos;
      }
      if (eq.fault.mapped)
      {
        mapped.fault = makeMqttSignal(
            eq.fault.address,
            parseMqttEncoding(eq.fault.encoding),
            eq.fault.jsonPointer);
        mapped.fault.qos = eq.fault.qos;
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createMqtt(record.adapterId, std::move(config));
  }

  if (record.protocol == "rest")
  {
    RestAdapterConfig config;
    config.scheme =
        record.connection.scheme.empty() ? "http" : record.connection.scheme;
    config.host = record.connection.host;
    config.port = record.connection.port;
    config.basePath = record.connection.basePath;
    config.healthPath = record.connection.healthPath;
    config.timeoutMs =
        record.connection.timeoutMs > 0 ? record.connection.timeoutMs : 2000;
    if (!record.credentials.username.empty())
    {
      config.auth.kind = RestAuthKind::Basic;
      config.auth.username = record.credentials.username;
    }
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      RestEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      mapped.telemetryPath = eq.telemetryPath;
      mapped.statePointer = eq.statePointer;
      mapped.faultPointer = eq.faultPointer;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        mapped.telemetry.push_back(
            {tel.name,
             tel.jsonPointer.empty() ? tel.address : tel.jsonPointer,
             tel.unit});
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        RestCommandMapping mappedCommand;
        mappedCommand.command = command.command;
        mappedCommand.method = parseRestMethod(command.method);
        mappedCommand.path = command.address;
        mappedCommand.bodyTemplate = command.bodyTemplate;
        mapped.commands.push_back(mappedCommand);
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createRest(record.adapterId, std::move(config));
  }

  if (record.protocol == "ethernetip")
  {
    EtherNetIpAdapterConfig config;
    config.host = record.connection.host;
    config.port = record.connection.port == 0 ? 44818 : record.connection.port;
    config.path = record.connection.path.empty() ? "1,0" : record.connection.path;
    config.plcType =
        record.connection.plcType.empty() ? "controllogix" : record.connection.plcType;
    config.timeoutMs =
        record.connection.timeoutMs > 0 ? record.connection.timeoutMs : 2000;
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      EtherNetIpEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        mapped.telemetry.push_back(
            {tel.name, tel.address, parseEipType(tel.valueType), tel.unit});
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        mapped.commands.push_back(
            {command.command, command.address, parseEipType(command.valueType)});
      }
      if (eq.state.mapped)
      {
        mapped.state =
            makeEtherNetIpSignal(eq.state.address, parseEipType(eq.state.valueType));
      }
      if (eq.fault.mapped)
      {
        mapped.fault =
            makeEtherNetIpSignal(eq.fault.address, parseEipType(eq.fault.valueType));
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createEtherNetIp(record.adapterId, std::move(config));
  }

  if (record.protocol == "profinet" || record.protocol == "profibus")
  {
    const std::string impl = resolveAdapterImplementation(record);
    if (impl == "gateway")
    {
      if (error != nullptr)
      {
        const std::string protoLabel =
            record.protocol == "profibus" ? "PROFIBUS" : "PROFINET";
        *error =
            "Gateway runtime for " + protoLabel
            + " is not currently available. Configure a supported gateway path using "
              "OPC UA, Modbus, MQTT or REST.";
      }
      return nullptr;
    }
    if (impl == "softing_native")
    {
      if (error != nullptr)
      {
        *error = "Softing native implementation is not available in this ICP release";
      }
      return nullptr;
    }
    if (impl != "hilscher_native")
    {
      if (error != nullptr)
      {
        *error = "invalid implementation '" + impl + "' for protocol '" + record.protocol + "'";
      }
      return nullptr;
    }
    if (record.protocol == "profinet")
    {
      ConfigResult mapped;
      auto adapter = AdapterFactory::createProfinetFromRecord(record, &mapped);
      if (!adapter && error != nullptr)
      {
        *error = mapped.message.empty() ? "invalid PROFINET configuration" : mapped.message;
      }
      return adapter;
    }
    ConfigResult mapped;
    auto adapter = AdapterFactory::createProfibusFromRecord(record, &mapped);
    if (!adapter && error != nullptr)
    {
      *error = mapped.message.empty() ? "invalid PROFIBUS configuration" : mapped.message;
    }
    return adapter;
  }

  if (error != nullptr)
  {
    *error = "unsupported protocol '" + record.protocol + "'";
  }
  return nullptr;
}

std::string ApplicationService::connectionStateName(ConnectionState state)
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

std::string ApplicationService::connectionStateDisplay(
    const std::string &protocol, const std::string &connectionState)
{
  if (protocol == "mock" && connectionState == "CONNECTED")
  {
    return "SIMULATED_ACTIVE";
  }
  return connectionState;
}

std::string ApplicationService::connectionSummary(const AdapterConfigRecord &record)
{
  const AdapterConnectionRecord &c = record.connection;
  const std::string impl = resolveAdapterImplementation(record);
  if (record.protocol == "mock")
  {
    return "in-process simulation";
  }
  if (record.protocol == "modbus")
  {
    const std::string transport = c.transport.empty() ? "tcp" : c.transport;
    if (transport == "rtu")
    {
      std::string summary = "RTU " + c.serialDevice;
      if (c.baudRate > 0)
      {
        summary += " @ " + std::to_string(c.baudRate);
      }
      std::string parity = c.parity.empty() ? "none" : c.parity;
      summary += " " + std::to_string(c.dataBits > 0 ? c.dataBits : 8)
          + parity.substr(0, 1) + std::to_string(c.stopBits > 0 ? c.stopBits : 1);
      return summary;
    }
    std::string summary = c.host;
    if (c.port != 0)
    {
      summary += ":" + std::to_string(c.port);
    }
    return summary.empty() ? "Modbus TCP" : summary;
  }
  if (impl == "hilscher_native")
  {
    if (!c.boardId.empty())
    {
      return "board " + c.boardId;
    }
    return "native fieldbus";
  }
  if (!c.endpointUrl.empty())
  {
    return c.endpointUrl;
  }
  if (!c.host.empty())
  {
    std::string summary = c.host;
    if (c.port != 0)
    {
      summary += ":" + std::to_string(c.port);
    }
    return summary;
  }
  if (!c.scheme.empty() && !c.host.empty())
  {
    return c.scheme + "://" + c.host;
  }
  return "";
}

std::string ApplicationService::adapterImplementation(
    const AdapterConfigRecord &record)
{
  return resolveAdapterImplementation(record);
}

}  // namespace icp
}  // namespace virtual_factory
