#include <virtual_factory/industrial/EtherNetIpIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "eip_session.hh"

#include <cmath>
#include <string>
#include <utility>

namespace virtual_factory
{

namespace
{

bool isSetCommand(const std::string &command)
{
  return command.size() > 4 && command.compare(0, 4, "set_") == 0;
}

internal::EipTagValueType toSessionType(EtherNetIpValueType type)
{
  switch (type)
  {
    case EtherNetIpValueType::Bool:
      return internal::EipTagValueType::Bool;
    case EtherNetIpValueType::Real:
      return internal::EipTagValueType::Real;
    case EtherNetIpValueType::Dint:
    default:
      return internal::EipTagValueType::Dint;
  }
}

}  // namespace

struct EtherNetIpIndustrialAdapter::ClientHandle
{
  internal::EipSession session;
};

class EtherNetIpIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      EtherNetIpIndustrialAdapter *adapter,
      const EtherNetIpEquipmentMapping *mapping)
      : adapter_(adapter), mapping_(mapping), inner_(mapping->id, mapping->type)
  {
    for (const auto &capability : mapping->capabilities)
    {
      this->inner_.addCapability(capability);
    }
    for (const auto &command : mapping->commands)
    {
      this->inner_.addCapability(command.command);
    }
  }

  std::string id() const override { return this->inner_.id(); }
  std::string type() const override { return this->inner_.type(); }
  OperationalState operationalState() const override
  {
    return this->inner_.operationalState();
  }
  bool fault() const override { return this->inner_.fault(); }
  std::vector<std::string> capabilities() const override
  {
    return this->inner_.capabilities();
  }
  std::vector<std::string> commands() const override
  {
    return this->inner_.commands();
  }

  CommandResult execute(
      const std::string &command, double parameter) override
  {
    if (this->adapter_->connectionState() != ConnectionState::Connected)
    {
      if (this->adapter_->connectionState() == ConnectionState::Faulted)
      {
        return {false, "communication fault"};
      }
      return {false, "adapter not connected"};
    }

    const EtherNetIpCommandMapping *mapped = nullptr;
    std::size_t index = 0;
    for (std::size_t i = 0; i < this->mapping_->commands.size(); ++i)
    {
      if (this->mapping_->commands[i].command == command)
      {
        mapped = &this->mapping_->commands[i];
        index = i;
        break;
      }
    }

    if (mapped == nullptr)
    {
      return {false, "unknown command"};
    }

    const std::string key = EtherNetIpIndustrialAdapter::tagKey(
        this->mapping_->id, "cmd", index);
    bool ok = false;
    if (isSetCommand(command))
    {
      ok = this->adapter_->writeDouble(key, mapped->valueType, parameter);
    }
    else if (mapped->valueType == EtherNetIpValueType::Bool)
    {
      ok = this->adapter_->writeBoolean(key, mapped->valueType, true);
    }
    else
    {
      ok = this->adapter_->writeDouble(key, mapped->valueType, 1.0);
    }

    if (!ok)
    {
      return {false, "ethernetip write failed"};
    }
    return {true, "written"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }
  EquipmentStatus status() const override { return this->inner_.status(); }

  bool refreshFromDevice()
  {
    for (std::size_t i = 0; i < this->mapping_->telemetry.size(); ++i)
    {
      const auto &point = this->mapping_->telemetry[i];
      const std::string key = EtherNetIpIndustrialAdapter::tagKey(
          this->mapping_->id, "tel", i);
      double value = 0.0;
      if (!this->adapter_->readDouble(key, point.valueType, &value))
      {
        return false;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (this->mapping_->state.mapped)
    {
      const std::string key = EtherNetIpIndustrialAdapter::tagKey(
          this->mapping_->id, "state", 0);
      bool running = false;
      if (!this->adapter_->readBoolean(
              key, this->mapping_->state.valueType, &running))
      {
        return false;
      }
      this->inner_.setOperationalState(
          running ? OperationalState::Running : OperationalState::Stopped);
    }

    if (this->mapping_->fault.mapped)
    {
      const std::string key = EtherNetIpIndustrialAdapter::tagKey(
          this->mapping_->id, "fault", 0);
      bool fault = false;
      if (!this->adapter_->readBoolean(
              key, this->mapping_->fault.valueType, &fault))
      {
        return false;
      }
      this->inner_.setFault(fault);
    }

    return true;
  }

private:
  EtherNetIpIndustrialAdapter *adapter_;
  const EtherNetIpEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

EtherNetIpIndustrialAdapter::EtherNetIpIndustrialAdapter(
    std::string id, EtherNetIpAdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      client_(std::make_unique<ClientHandle>())
{
}

EtherNetIpIndustrialAdapter::~EtherNetIpIndustrialAdapter()
{
  this->disconnect();
}

std::string EtherNetIpIndustrialAdapter::id() const { return this->id_; }
std::string EtherNetIpIndustrialAdapter::protocol() const
{
  return "ethernetip";
}
ConnectionState EtherNetIpIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}
std::string EtherNetIpIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

int EtherNetIpIndustrialAdapter::operationTimeoutMs() const
{
  if (this->config_.pollTimeoutMs > 0)
  {
    return this->config_.pollTimeoutMs;
  }
  return this->config_.timeoutMs < 1 ? 1 : this->config_.timeoutMs;
}

bool EtherNetIpIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.host.empty() || this->config_.port == 0)
  {
    this->enterFault("missing EtherNet/IP host or port");
    return false;
  }
  if (this->config_.plcType.empty())
  {
    this->enterFault("missing EtherNet/IP plc type");
    return false;
  }

  this->client_->session.close();

  internal::EipSessionConfig sessionConfig;
  sessionConfig.host = this->config_.host;
  sessionConfig.port = this->config_.port;
  sessionConfig.path = this->config_.path;
  sessionConfig.plcType = this->config_.plcType;
  sessionConfig.timeoutMs =
      this->config_.timeoutMs < 1 ? 1 : this->config_.timeoutMs;

  if (!this->client_->session.open(sessionConfig))
  {
    this->enterFault(
        std::string("EtherNet/IP session open failed: ") +
        this->client_->session.lastError());
    return false;
  }

  if (!this->createMappedTags())
  {
    this->client_->session.close();
    this->enterFault(
        std::string("EtherNet/IP tag create failed: ") +
        this->client_->session.lastError());
    return false;
  }

  this->bindEquipment();
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void EtherNetIpIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  if (this->client_)
  {
    this->client_->session.close();
  }
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> EtherNetIpIndustrialAdapter::equipment()
{
  std::vector<Equipment *> result;
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return result;
  }
  result.reserve(this->bound_.size());
  for (auto &item : this->bound_)
  {
    result.push_back(item.get());
  }
  return result;
}

Equipment *EtherNetIpIndustrialAdapter::equipmentById(const std::string &id)
{
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return nullptr;
  }
  for (auto &item : this->bound_)
  {
    if (item->id() == id)
    {
      return item.get();
    }
  }
  return nullptr;
}

void EtherNetIpIndustrialAdapter::poll()
{
  if (this->connection_state_ != ConnectionState::Connected)
  {
    if (this->connection_state_ == ConnectionState::Faulted &&
        this->last_error_.empty())
    {
      this->last_error_ = "poll failed: communication fault";
    }
    return;
  }

  this->client_->session.setTimeoutMs(this->operationTimeoutMs());

  for (auto &item : this->bound_)
  {
    if (!item->refreshFromDevice())
    {
      this->enterFault(
          std::string("EtherNet/IP read failed during poll: ") +
          this->client_->session.lastError());
      return;
    }
  }
}

void EtherNetIpIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void EtherNetIpIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
}

bool EtherNetIpIndustrialAdapter::createMappedTags()
{
  this->client_->session.setTimeoutMs(
      this->config_.timeoutMs < 1 ? 1 : this->config_.timeoutMs);

  for (const auto &mapping : this->config_.equipment)
  {
    for (std::size_t i = 0; i < mapping.telemetry.size(); ++i)
    {
      const auto &point = mapping.telemetry[i];
      if (!this->client_->session.createTag(
              tagKey(mapping.id, "tel", i),
              point.tag,
              toSessionType(point.valueType)))
      {
        return false;
      }
    }
    for (std::size_t i = 0; i < mapping.commands.size(); ++i)
    {
      const auto &command = mapping.commands[i];
      if (!this->client_->session.createTag(
              tagKey(mapping.id, "cmd", i),
              command.tag,
              toSessionType(command.valueType)))
      {
        return false;
      }
    }
    if (mapping.state.mapped)
    {
      if (!this->client_->session.createTag(
              tagKey(mapping.id, "state", 0),
              mapping.state.tag,
              toSessionType(mapping.state.valueType)))
      {
        return false;
      }
    }
    if (mapping.fault.mapped)
    {
      if (!this->client_->session.createTag(
              tagKey(mapping.id, "fault", 0),
              mapping.fault.tag,
              toSessionType(mapping.fault.valueType)))
      {
        return false;
      }
    }
  }
  return true;
}

std::string EtherNetIpIndustrialAdapter::tagKey(
    const std::string &equipmentId, const std::string &role, std::size_t index)
{
  return equipmentId + "|" + role + "|" + std::to_string(index);
}

bool EtherNetIpIndustrialAdapter::readDouble(
    const std::string &tagKey,
    EtherNetIpValueType valueType,
    double *value)
{
  if (value == nullptr || !this->client_->session.connected())
  {
    return false;
  }

  if (valueType == EtherNetIpValueType::Bool)
  {
    bool bit = false;
    if (!this->client_->session.readBool(tagKey, &bit))
    {
      return false;
    }
    *value = bit ? 1.0 : 0.0;
    return true;
  }
  if (valueType == EtherNetIpValueType::Real)
  {
    float real = 0.0f;
    if (!this->client_->session.readReal(tagKey, &real))
    {
      return false;
    }
    *value = static_cast<double>(real);
    return true;
  }

  std::int32_t dint = 0;
  if (!this->client_->session.readDint(tagKey, &dint))
  {
    return false;
  }
  *value = static_cast<double>(dint);
  return true;
}

bool EtherNetIpIndustrialAdapter::readBoolean(
    const std::string &tagKey,
    EtherNetIpValueType valueType,
    bool *value)
{
  if (value == nullptr || !this->client_->session.connected())
  {
    return false;
  }

  if (valueType == EtherNetIpValueType::Bool)
  {
    return this->client_->session.readBool(tagKey, value);
  }
  if (valueType == EtherNetIpValueType::Real)
  {
    float real = 0.0f;
    if (!this->client_->session.readReal(tagKey, &real))
    {
      return false;
    }
    *value = real != 0.0f;
    return true;
  }

  std::int32_t dint = 0;
  if (!this->client_->session.readDint(tagKey, &dint))
  {
    return false;
  }
  *value = dint != 0;
  return true;
}

bool EtherNetIpIndustrialAdapter::writeDouble(
    const std::string &tagKey,
    EtherNetIpValueType valueType,
    double value)
{
  if (!this->client_->session.connected())
  {
    this->enterFault("EtherNet/IP write failed: session closed");
    return false;
  }

  this->client_->session.setTimeoutMs(this->operationTimeoutMs());

  bool ok = false;
  if (valueType == EtherNetIpValueType::Bool)
  {
    ok = this->client_->session.writeBool(tagKey, value != 0.0);
  }
  else if (valueType == EtherNetIpValueType::Real)
  {
    ok = this->client_->session.writeReal(tagKey, static_cast<float>(value));
  }
  else
  {
    ok = this->client_->session.writeDint(
        tagKey, static_cast<std::int32_t>(std::lround(value)));
  }

  if (!ok)
  {
    this->enterFault(
        std::string("EtherNet/IP write failed: ") +
        this->client_->session.lastError());
    return false;
  }
  return true;
}

bool EtherNetIpIndustrialAdapter::writeBoolean(
    const std::string &tagKey,
    EtherNetIpValueType valueType,
    bool value)
{
  if (!this->client_->session.connected())
  {
    this->enterFault("EtherNet/IP write failed: session closed");
    return false;
  }

  this->client_->session.setTimeoutMs(this->operationTimeoutMs());

  bool ok = false;
  if (valueType == EtherNetIpValueType::Bool)
  {
    ok = this->client_->session.writeBool(tagKey, value);
  }
  else if (valueType == EtherNetIpValueType::Real)
  {
    ok = this->client_->session.writeReal(tagKey, value ? 1.0f : 0.0f);
  }
  else
  {
    ok = this->client_->session.writeDint(tagKey, value ? 1 : 0);
  }

  if (!ok)
  {
    this->enterFault(
        std::string("EtherNet/IP write failed: ") +
        this->client_->session.lastError());
    return false;
  }
  return true;
}

}  // namespace virtual_factory
