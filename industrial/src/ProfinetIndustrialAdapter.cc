#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "hilscher/process_image_codec.hh"
#include "hilscher/profinet_session.hh"

#include <utility>

namespace virtual_factory
{

namespace
{

internal::ProcessValueType toProcessType(ProfinetValueType type)
{
  switch (type)
  {
    case ProfinetValueType::Bool:
      return internal::ProcessValueType::Bool;
    case ProfinetValueType::Uint8:
      return internal::ProcessValueType::Uint8;
    case ProfinetValueType::Uint16:
      return internal::ProcessValueType::Uint16;
    case ProfinetValueType::Int32:
      return internal::ProcessValueType::Int32;
    case ProfinetValueType::Real:
      return internal::ProcessValueType::Real;
    case ProfinetValueType::Int16:
    default:
      return internal::ProcessValueType::Int16;
  }
}

}  // namespace

struct ProfinetIndustrialAdapter::SessionHandle
{
  internal::ProfinetSession session;
};

class ProfinetIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      ProfinetIndustrialAdapter *adapter,
      const ProfinetEquipmentMapping *mapping)
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

    const ProfinetCommandMapping *mapped = nullptr;
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

    if (!this->adapter_->writeMappedCommand(*mapped, parameter))
    {
      return {false, this->adapter_->lastError()};
    }
    return {true, "written"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  bool refreshFromImage(const std::vector<std::uint8_t> &input)
  {
    for (const auto &point : this->mapping_->telemetry)
    {
      double value = 0.0;
      if (!internal::processImageRead(
              input,
              toProcessType(point.valueType),
              point.inputByteOffset,
              point.bitOffset,
              &value))
      {
        return false;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (this->mapping_->state.mapped)
    {
      double running = 0.0;
      if (!internal::processImageRead(
              input,
              toProcessType(this->mapping_->state.valueType),
              this->mapping_->state.inputByteOffset,
              this->mapping_->state.bitOffset,
              &running))
      {
        return false;
      }
      this->inner_.setOperationalState(
          running != 0.0 ? OperationalState::Running
                           : OperationalState::Stopped);
    }

    if (this->mapping_->fault.mapped)
    {
      double fault = 0.0;
      if (!internal::processImageRead(
              input,
              toProcessType(this->mapping_->fault.valueType),
              this->mapping_->fault.inputByteOffset,
              this->mapping_->fault.bitOffset,
              &fault))
      {
        return false;
      }
      this->inner_.setFault(fault != 0.0);
    }

    return true;
  }

private:
  ProfinetIndustrialAdapter *adapter_;
  const ProfinetEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

ProfinetIndustrialAdapter::ProfinetIndustrialAdapter(
    std::string id, AdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      session_(std::make_unique<SessionHandle>())
{
  this->bindEquipment();
}

ProfinetIndustrialAdapter::~ProfinetIndustrialAdapter()
{
  this->disconnect();
}

std::string ProfinetIndustrialAdapter::id() const
{
  return this->id_;
}

std::string ProfinetIndustrialAdapter::protocol() const
{
  return "profinet";
}

ConnectionState ProfinetIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string ProfinetIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool ProfinetIndustrialAdapter::hilscherSdkPresent() const
{
  return this->session_->session.sdkAvailable();
}

bool ProfinetIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.configArtifactPath.empty())
  {
    this->enterFault("missing PROFINET configuration artifact path");
    return false;
  }

  this->session_->session.close();

  internal::ProfinetSessionConfig sessionConfig;
  sessionConfig.boardId = this->config_.boardId;
  sessionConfig.channel = this->config_.channel;
  sessionConfig.configArtifactPath = this->config_.configArtifactPath;
  sessionConfig.expectedFirmwareName = this->config_.expectedFirmwareName;
  sessionConfig.ioTimeoutMs = static_cast<unsigned>(this->operationTimeoutMs());

  if (!this->session_->session.open(sessionConfig))
  {
    this->enterFault(this->session_->session.lastError());
    return false;
  }

  const std::size_t bytes =
      this->config_.processImageBytes > 0 ? this->config_.processImageBytes : 256;
  this->input_image_.assign(bytes, 0);
  this->output_image_.assign(bytes, 0);
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void ProfinetIndustrialAdapter::disconnect()
{
  this->session_->session.close();
  this->input_image_.clear();
  this->output_image_.clear();
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> ProfinetIndustrialAdapter::equipment()
{
  std::vector<Equipment *> result;
  result.reserve(this->bound_.size());
  for (const auto &entry : this->bound_)
  {
    result.push_back(entry.get());
  }
  return result;
}

Equipment *ProfinetIndustrialAdapter::equipmentById(const std::string &id)
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

void ProfinetIndustrialAdapter::poll()
{
  if (this->connection_state_ != ConnectionState::Connected)
  {
    return;
  }

  if (!this->refreshEquipment())
  {
    this->enterFault(this->session_->session.lastError());
  }
}

void ProfinetIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(
        std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void ProfinetIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
}

int ProfinetIndustrialAdapter::operationTimeoutMs() const
{
  return this->config_.pollTimeoutMs > 0 ? this->config_.pollTimeoutMs : 2000;
}

bool ProfinetIndustrialAdapter::refreshEquipment()
{
  if (!this->session_->session.readInputArea(
          0, this->input_image_.size(), &this->input_image_))
  {
    return false;
  }

  for (auto &entry : this->bound_)
  {
    if (!entry->refreshFromImage(this->input_image_))
    {
      this->last_error_ = "PROFINET process-image mapping out of range";
      return false;
    }
  }
  return true;
}

bool ProfinetIndustrialAdapter::writeMappedCommand(
    const ProfinetCommandMapping &mapping, double parameter)
{
  if (!internal::processImageWrite(
          &this->output_image_,
          toProcessType(mapping.valueType),
          mapping.outputByteOffset,
          mapping.bitOffset,
          parameter))
  {
    this->last_error_ = "PROFINET output mapping out of range";
    return false;
  }
  return this->session_->session.writeOutputArea(0, this->output_image_);
}

}  // namespace virtual_factory
