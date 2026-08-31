#include <virtual_factory/icp/app/ApplicationService.hh>

#include <virtual_factory/icp/AdapterFactory.hh>
#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>
#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>
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

}  // namespace

ApplicationService::ApplicationService(std::string configurationPath)
    : configuration_path_(std::move(configurationPath))
{
}

ApplicationService::~ApplicationService()
{
  this->stop();
}

void ApplicationService::start()
{
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->running_)
    {
      return;
    }
    this->scheduler_ = std::make_unique<PollScheduler>(
        this->manager_, this->cache_, std::chrono::milliseconds(250));
    this->scheduler_->start();
    this->running_ = true;
  }
  this->recordEvent("info", "runtime", "ICP application service started");
}

void ApplicationService::stop()
{
  std::unique_ptr<PollScheduler> scheduler;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->running_)
    {
      return;
    }
    this->running_ = false;
    scheduler = std::move(this->scheduler_);
  }
  if (scheduler)
  {
    scheduler->stop();
  }
  this->manager_.disconnectAll();
  this->recordEvent("info", "runtime", "ICP application service stopped");
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

  for (const std::string &adapterId : this->manager_.adapterIds())
  {
    const IndustrialAdapter *adapter = this->manager_.adapter(adapterId);
    if (adapter == nullptr)
    {
      continue;
    }
    switch (adapter->connectionState())
    {
      case ConnectionState::Connected:
        ++out.connectedAdapters;
        break;
      case ConnectionState::Faulted:
        ++out.faultedAdapters;
        break;
      case ConnectionState::Disconnected:
        ++out.disconnectedAdapters;
        break;
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
      {"modbus", "Modbus TCP", true, false},
      {"mqtt", "MQTT", true, false},
      {"rest", "REST", true, false},
      {"ethernetip", "EtherNet/IP", true, false},
      {"profinet", "PROFINET", true, true},
      {"profibus", "PROFIBUS", true, true},
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
    }
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
  const ConfigResult result = this->catalog_.upsertAdapter(std::move(adapter));
  if (result.ok)
  {
    this->recordEvent("info", "configuration", "Adapter upserted", id);
  }
  else
  {
    this->recordEvent("error", "configuration", result.message, id);
  }
  return result;
}

ConfigResult ApplicationService::removeAdapterConfig(const std::string &adapterId)
{
  if (this->manager_.adapter(adapterId) != nullptr)
  {
    this->manager_.removeAdapter(adapterId);
    this->cache_.removeAdapterEquipment(adapterId);
  }
  const ConfigResult result = this->catalog_.removeAdapter(adapterId);
  if (result.ok)
  {
    this->recordEvent("info", "configuration", "Adapter removed", adapterId);
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
    view.connectionState = "NOT_CONFIGURED";
    if (const IndustrialAdapter *runtime = this->manager_.adapter(record.adapterId))
    {
      view.runtimePresent = true;
      view.connectionState = connectionStateName(runtime->connectionState());
      view.lastError = runtime->lastError();
      // IndustrialAdapter::equipment() is non-const; catalog size is authoritative for GUI lists.
    }
    else
    {
      view.connectionState = "DISCONNECTED";
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
    this->recordEvent("error", "connection", result.message, adapterId);
    return result;
  }
  if (!record->enabled)
  {
    AdapterManagerResult result;
    result.ok = false;
    result.message = "adapter '" + adapterId + "' is disabled";
    return result;
  }

  AdapterManagerResult ensured = this->ensureRuntimeAdapter(*record);
  if (!ensured.ok)
  {
    this->recordEvent("error", "connection", ensured.message, adapterId);
    return ensured;
  }

  AdapterManagerResult connected = this->manager_.connectAdapter(adapterId);
  IndustrialAdapter *runtime = this->manager_.adapter(adapterId);
  if (runtime != nullptr)
  {
    if (connected.ok)
    {
      this->cache_.updateFromAdapter(*runtime);
      this->recordEvent("info", "connection", "Adapter connected", adapterId);
    }
    else
    {
      this->cache_.updateFromAdapter(*runtime);
      this->cache_.markAdapterCommunication(
          adapterId, runtime->connectionState(), runtime->lastError());
      std::string message = connected.message;
      if (record->protocol == "profinet" || record->protocol == "profibus")
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
          if (!runtime->lastError().empty() && message.find(runtime->lastError()) == std::string::npos)
          {
            message += " (" + runtime->lastError() + ")";
          }
        }
      }
      connected.message = message;
      this->recordEvent("error", "connection", connected.message, adapterId);
    }
  }
  return connected;
}

AdapterManagerResult ApplicationService::disconnectAdapter(
    const std::string &adapterId)
{
  AdapterManagerResult result = this->manager_.disconnectAdapter(adapterId);
  this->cache_.removeAdapterEquipment(adapterId);
  if (result.ok)
  {
    this->recordEvent("info", "connection", "Adapter disconnected", adapterId);
  }
  return result;
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
    this->recordEvent("error", "command", out.message, {}, equipmentId);
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
    this->recordEvent("error", "command", out.message, {}, equipmentId);
    return out;
  }

  if (owner->connectionState() != ConnectionState::Connected)
  {
    out.message = "adapter '" + owner->id() + "' is not connected";
    this->recordEvent("error", "command", out.message, owner->id(), equipmentId);
    return out;
  }

  const CommandResult executed = equipment->execute(command, parameter);
  this->cache_.updateFromAdapter(*owner);
  out.ok = executed.accepted;
  out.message = executed.message;
  this->recordEvent(
      executed.accepted ? "info" : "error",
      "command",
      command + ": " + executed.message,
      owner->id(),
      equipmentId);
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
  std::lock_guard<std::mutex> lock(this->mutex_);
  ApplicationEvent event;
  event.atUtc = std::chrono::system_clock::now();
  event.level = level;
  event.category = category;
  event.message = message;
  event.adapterId = adapterId;
  event.equipmentId = equipmentId;
  this->events_.push_back(std::move(event));
  while (this->events_.size() > kMaxEvents)
  {
    this->events_.pop_front();
  }
}

AdapterManagerResult ApplicationService::ensureRuntimeAdapter(
    const AdapterConfigRecord &record)
{
  if (this->manager_.adapter(record.adapterId) != nullptr)
  {
    // Rebuild when config may have changed: remove and recreate.
    this->manager_.removeAdapter(record.adapterId);
    this->cache_.removeAdapterEquipment(record.adapterId);
  }

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
    for (const EquipmentMappingRecord &eq : record.equipment)
    {
      OpcUaEquipmentMapping mapped;
      mapped.id = eq.equipmentId;
      mapped.type = eq.type;
      mapped.capabilities = eq.capabilities;
      for (const TelemetryMappingRecord &tel : eq.telemetry)
      {
        mapped.telemetry.push_back(
            {tel.name, OpcUaNodeRef{tel.namespaceIndex, tel.address}, tel.unit});
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        mapped.commands.push_back(
            {command.command,
             OpcUaNodeRef{command.namespaceIndex, command.address}});
      }
      if (eq.state.mapped)
      {
        mapped.stateNode = OpcUaNodeRef{eq.state.namespaceIndex, eq.state.address};
      }
      if (eq.fault.mapped)
      {
        mapped.faultNode =
            OpcUaNodeRef{eq.fault.namespaceIndex, eq.fault.address};
      }
      config.equipment.push_back(std::move(mapped));
    }
    return AdapterFactory::createOpcUa(record.adapterId, std::move(config));
  }

  if (record.protocol == "modbus")
  {
    ModbusAdapterConfig config;
    config.host = record.connection.host;
    config.port = record.connection.port == 0 ? 502 : record.connection.port;
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
            tel.unitId == 0 ? 1 : tel.unitId,
            parseModbusTable(tel.table),
            tel.registerAddress);
        mapped.telemetry.push_back(point);
      }
      for (const CommandMappingRecord &command : eq.commands)
      {
        ModbusCommandMapping mappedCommand;
        mappedCommand.command = command.command;
        mappedCommand.target = makeModbusRef(
            command.unitId == 0 ? 1 : command.unitId,
            parseModbusTable(command.table),
            command.registerAddress);
        mapped.commands.push_back(mappedCommand);
      }
      if (eq.state.mapped)
      {
        mapped.stateCoil = makeModbusRef(
            eq.state.unitId == 0 ? 1 : eq.state.unitId,
            parseModbusTable(eq.state.table.empty() ? "coil" : eq.state.table),
            eq.state.registerAddress);
      }
      if (eq.fault.mapped)
      {
        mapped.faultCoil = makeModbusRef(
            eq.fault.unitId == 0 ? 1 : eq.fault.unitId,
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

  if (record.protocol == "profibus")
  {
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

}  // namespace icp
}  // namespace virtual_factory
