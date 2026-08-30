#include <virtual_factory/industrial/ProfibusIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "hilscher/profibus_session.hh"

#include <utility>

namespace virtual_factory
{

namespace
{

bool isSetCommand(const std::string &command)
{
  return command.size() > 4 && command.compare(0, 4, "set_") == 0;
}

}  // namespace

struct ProfibusIndustrialAdapter::SessionHandle
{
  internal::ProfibusSession session;
};

class ProfibusIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      ProfibusIndustrialAdapter *adapter,
      const ProfibusEquipmentMapping *mapping)
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

    const ProfibusCommandMapping *mapped = nullptr;
    for (const auto &entry : this->mapping_->commands)
    {
      if (entry.command == command)
      {
        mapped = &entry;
        break;
      }
    }
    if (mapped == nullptr)
    {
      return {false, "unknown command"};
    }

    (void)parameter;
    (void)isSetCommand;
    return {false, "PROFIBUS output write not available (SDK blocked)"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

private:
  ProfibusIndustrialAdapter *adapter_;
  const ProfibusEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

ProfibusIndustrialAdapter::ProfibusIndustrialAdapter(
    std::string id, AdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      session_(std::make_unique<SessionHandle>())
{
  this->bindEquipment();
}

ProfibusIndustrialAdapter::~ProfibusIndustrialAdapter()
{
  this->disconnect();
}

std::string ProfibusIndustrialAdapter::id() const
{
  return this->id_;
}

std::string ProfibusIndustrialAdapter::protocol() const
{
  return "profibus";
}

ConnectionState ProfibusIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string ProfibusIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool ProfibusIndustrialAdapter::hilscherSdkPresent() const
{
  return this->session_->session.sdkAvailable();
}

bool ProfibusIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.configArtifactPath.empty())
  {
    this->enterFault("missing PROFIBUS configuration artifact path");
    return false;
  }

  this->session_->session.close();

  internal::ProfibusSessionConfig sessionConfig;
  sessionConfig.boardId = this->config_.boardId;
  sessionConfig.channel = this->config_.channel;
  sessionConfig.masterAddress = this->config_.masterAddress;
  sessionConfig.baudRateKbps = this->config_.baudRateKbps;
  sessionConfig.configArtifactPath = this->config_.configArtifactPath;

  if (!this->session_->session.open(sessionConfig))
  {
    this->enterFault(this->session_->session.lastError());
    return false;
  }

  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void ProfibusIndustrialAdapter::disconnect()
{
  this->session_->session.close();
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> ProfibusIndustrialAdapter::equipment()
{
  std::vector<Equipment *> result;
  result.reserve(this->bound_.size());
  for (const auto &entry : this->bound_)
  {
    result.push_back(entry.get());
  }
  return result;
}

Equipment *ProfibusIndustrialAdapter::equipmentById(const std::string &id)
{
  for (const auto &entry : this->bound_)
  {
    if (entry->id() == id)
    {
      return entry.get();
    }
  }
  return nullptr;
}

void ProfibusIndustrialAdapter::poll()
{
  if (this->connection_state_ != ConnectionState::Connected)
  {
    return;
  }

  this->enterFault("PROFIBUS cyclic poll not available (SDK blocked)");
}

void ProfibusIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(
        std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void ProfibusIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
}

int ProfibusIndustrialAdapter::operationTimeoutMs() const
{
  return this->config_.pollTimeoutMs > 0 ? this->config_.pollTimeoutMs : 2000;
}

}  // namespace virtual_factory
