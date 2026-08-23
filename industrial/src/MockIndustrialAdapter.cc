#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include <utility>

namespace virtual_factory
{

class MockIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      MockIndustrialAdapter *adapter, MockIndustrialAdapter::DeviceConfig *config)
      : adapter_(adapter), config_(config), inner_(config->id, config->type)
  {
    for (const auto &capability : config->capabilities)
    {
      this->inner_.addCapability(capability);
    }
    this->copySourceTelemetry();
  }

  std::string id() const override
  {
    return this->inner_.id();
  }

  std::string type() const override
  {
    return this->inner_.type();
  }

  OperationalState operationalState() const override
  {
    return this->inner_.operationalState();
  }

  bool fault() const override
  {
    return this->inner_.fault();
  }

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

    if (command.size() > 4 && command.compare(0, 4, "set_") == 0 &&
        this->inner_.hasCapability(command))
    {
      const std::string pointName = command.substr(4);
      std::string unit;
      for (const auto &point : this->inner_.telemetry())
      {
        if (point.name == pointName)
        {
          unit = point.unit;
          break;
        }
      }
      this->inner_.setTelemetry(pointName, parameter, unit);
      this->adapter_->writeSourceTelemetry(
          this->inner_.id(), pointName, parameter, unit);
      return {true, "set"};
    }

    return this->inner_.execute(command, parameter);
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  void copySourceTelemetry()
  {
    for (const auto &entry : this->config_->sourceTelemetry)
    {
      this->inner_.setTelemetry(
          entry.second.name, entry.second.value, entry.second.unit);
    }
  }

private:
  MockIndustrialAdapter *adapter_;
  MockIndustrialAdapter::DeviceConfig *config_;
  GenericEquipment inner_;
};

MockIndustrialAdapter::MockIndustrialAdapter(std::string id)
    : id_(std::move(id))
{
}

MockIndustrialAdapter::~MockIndustrialAdapter() = default;

void MockIndustrialAdapter::addDevice(std::string equipmentId, std::string type)
{
  if (this->findConfig(equipmentId) != nullptr)
  {
    return;
  }

  DeviceConfig config;
  config.id = std::move(equipmentId);
  config.type = std::move(type);
  this->devices_.push_back(std::move(config));
}

void MockIndustrialAdapter::addCapability(
    const std::string &equipmentId, std::string capability)
{
  DeviceConfig *config = this->findConfig(equipmentId);
  if (config == nullptr)
  {
    return;
  }

  for (const auto &existing : config->capabilities)
  {
    if (existing == capability)
    {
      return;
    }
  }
  config->capabilities.push_back(std::move(capability));
}

void MockIndustrialAdapter::setSourceTelemetry(
    const std::string &equipmentId,
    std::string name,
    double value,
    std::string unit)
{
  DeviceConfig *config = this->findConfig(equipmentId);
  if (config == nullptr)
  {
    return;
  }

  TelemetryPoint point;
  point.name = std::move(name);
  point.value = value;
  point.unit = std::move(unit);
  config->sourceTelemetry[point.name] = point;
}

void MockIndustrialAdapter::simulateCommunicationFailure(std::string reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = std::move(reason);
}

void MockIndustrialAdapter::clearCommunicationFailure()
{
  if (this->connection_state_ == ConnectionState::Faulted)
  {
    this->connection_state_ = ConnectionState::Disconnected;
    this->bound_.clear();
    this->last_error_.clear();
  }
}

std::string MockIndustrialAdapter::id() const
{
  return this->id_;
}

std::string MockIndustrialAdapter::protocol() const
{
  return "mock";
}

ConnectionState MockIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string MockIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool MockIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->connection_state_ == ConnectionState::Faulted)
  {
    this->last_error_ = "cannot connect while faulted";
    return false;
  }

  this->bindDevices();
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void MockIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> MockIndustrialAdapter::equipment()
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

Equipment *MockIndustrialAdapter::equipmentById(const std::string &id)
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

void MockIndustrialAdapter::poll()
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

  for (auto &item : this->bound_)
  {
    item->copySourceTelemetry();
  }
}

MockIndustrialAdapter::DeviceConfig *MockIndustrialAdapter::findConfig(
    const std::string &equipmentId)
{
  for (auto &config : this->devices_)
  {
    if (config.id == equipmentId)
    {
      return &config;
    }
  }
  return nullptr;
}

const MockIndustrialAdapter::DeviceConfig *MockIndustrialAdapter::findConfig(
    const std::string &equipmentId) const
{
  for (const auto &config : this->devices_)
  {
    if (config.id == equipmentId)
    {
      return &config;
    }
  }
  return nullptr;
}

void MockIndustrialAdapter::bindDevices()
{
  this->bound_.clear();
  for (auto &config : this->devices_)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &config));
  }
}

void MockIndustrialAdapter::writeSourceTelemetry(
    const std::string &equipmentId,
    const std::string &name,
    double value,
    const std::string &unit)
{
  DeviceConfig *config = this->findConfig(equipmentId);
  if (config == nullptr)
  {
    return;
  }

  TelemetryPoint point;
  point.name = name;
  point.value = value;
  point.unit = unit;
  config->sourceTelemetry[name] = std::move(point);
}

}  // namespace virtual_factory
