#include <virtual_factory/industrial/ProfinetIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "hilscher/process_image_codec.hh"
#include "hilscher/process_image_mapping.hh"
#include "hilscher/profinet_session.hh"

#include <algorithm>
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
    case ProfinetValueType::Int16:
      return internal::ProcessValueType::Int16;
    case ProfinetValueType::Uint16:
      return internal::ProcessValueType::Uint16;
    case ProfinetValueType::Int32:
      return internal::ProcessValueType::Int32;
    case ProfinetValueType::Real:
      return internal::ProcessValueType::Real;
  }
  return internal::ProcessValueType::Int16;
}

bool isSetCommand(const std::string &command)
{
  return command.size() > 4 && command.compare(0, 4, "set_") == 0;
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

    const double value = isSetCommand(command) ? parameter
                                                : (parameter != 0.0 ? 1.0 : 1.0);
    std::string error;
    if (!this->adapter_->writeMappedCommand(*mapped, value, &error))
    {
      return {false, error};
    }
    return {true, ""};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  void applyInput(const std::vector<std::uint8_t> &image)
  {
    for (const auto &point : this->mapping_->telemetry)
    {
      internal::applyTelemetryFromImage(
          &this->inner_,
          point.name,
          toProcessType(point.valueType),
          point.inputByteOffset,
          point.bitOffset,
          point.unit,
          image);
    }
    if (this->mapping_->state.mapped)
    {
      internal::applyStateFromImage(
          &this->inner_,
          toProcessType(this->mapping_->state.valueType),
          this->mapping_->state.inputByteOffset,
          this->mapping_->state.bitOffset,
          image);
    }
    if (this->mapping_->fault.mapped)
    {
      internal::applyFaultFromImage(
          &this->inner_,
          toProcessType(this->mapping_->fault.valueType),
          this->mapping_->fault.inputByteOffset,
          this->mapping_->fault.bitOffset,
          image);
    }
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

const ProfinetIndustrialAdapter::AdapterConfig &
ProfinetIndustrialAdapter::config() const
{
  return this->config_;
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
  this->output_image_.clear();

  internal::ProfinetSessionConfig sessionConfig;
  sessionConfig.boardId = this->config_.boardId;
  sessionConfig.channel = this->config_.channel;
  sessionConfig.stationName = this->config_.stationName;
  sessionConfig.configArtifactPath = this->config_.configArtifactPath;
  sessionConfig.ioTimeoutMs =
      static_cast<unsigned>(this->operationTimeoutMs());

  if (!this->session_->session.open(sessionConfig))
  {
    this->enterFault(this->session_->session.lastError());
    return false;
  }

  const std::size_t outBytes = std::max(
      this->session_->session.outputAreaBytes(), this->requiredOutputBytes());
  this->output_image_.assign(outBytes, 0);

  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void ProfinetIndustrialAdapter::disconnect()
{
  this->session_->session.close();
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

  (void)this->session_->session.triggerWatchdog();

  std::size_t length = this->session_->session.inputAreaBytes();
  if (length == 0)
  {
    length = this->requiredInputBytes();
  }
  if (length == 0)
  {
    return;
  }

  std::vector<std::uint8_t> image;
  if (!this->session_->session.readInputArea(0, length, &image))
  {
    this->enterFault(this->session_->session.lastError());
    return;
  }

  for (const auto &entry : this->bound_)
  {
    entry->applyInput(image);
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

bool ProfinetIndustrialAdapter::writeMappedCommand(
    const ProfinetCommandMapping &mapped, double parameter, std::string *error)
{
  if (this->output_image_.empty())
  {
    this->output_image_.assign(this->requiredOutputBytes(), 0);
  }
  if (!internal::processImageWrite(
          &this->output_image_,
          toProcessType(mapped.valueType),
          mapped.outputByteOffset,
          mapped.bitOffset,
          parameter))
  {
    if (error != nullptr)
    {
      *error = "PROFINET command does not fit the process output image";
    }
    return false;
  }
  if (!this->session_->session.writeOutputArea(0, this->output_image_))
  {
    const std::string reason = this->session_->session.lastError();
    this->enterFault(reason);
    if (error != nullptr)
    {
      *error = reason;
    }
    return false;
  }
  return true;
}

std::size_t ProfinetIndustrialAdapter::requiredInputBytes() const
{
  std::size_t size = 0;
  auto consider = [&](ProfinetValueType type, std::size_t offset) {
    size = std::max(
        size, offset + internal::processValueSize(toProcessType(type)));
  };
  for (const auto &mapping : this->config_.equipment)
  {
    for (const auto &point : mapping.telemetry)
    {
      consider(point.valueType, point.inputByteOffset);
    }
    if (mapping.state.mapped)
    {
      consider(mapping.state.valueType, mapping.state.inputByteOffset);
    }
    if (mapping.fault.mapped)
    {
      consider(mapping.fault.valueType, mapping.fault.inputByteOffset);
    }
  }
  return size;
}

std::size_t ProfinetIndustrialAdapter::requiredOutputBytes() const
{
  std::size_t size = 0;
  auto consider = [&](ProfinetValueType type, std::size_t offset) {
    size = std::max(
        size, offset + internal::processValueSize(toProcessType(type)));
  };
  for (const auto &mapping : this->config_.equipment)
  {
    for (const auto &command : mapping.commands)
    {
      consider(command.valueType, command.outputByteOffset);
    }
    for (const auto &point : mapping.telemetry)
    {
      consider(point.valueType, point.outputByteOffset);
    }
  }
  return size;
}

}  // namespace virtual_factory
