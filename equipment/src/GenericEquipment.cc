#include <virtual_factory/equipment/GenericEquipment.hh>

#include <utility>

namespace virtual_factory
{

GenericEquipment::GenericEquipment(std::string id, std::string type)
    : id_(std::move(id)), type_(std::move(type))
{
}

std::string GenericEquipment::id() const
{
  return this->id_;
}

std::string GenericEquipment::type() const
{
  return this->type_;
}

OperationalState GenericEquipment::operationalState() const
{
  return this->operational_state_;
}

bool GenericEquipment::fault() const
{
  return this->fault_;
}

std::vector<std::string> GenericEquipment::capabilities() const
{
  return this->capabilities_;
}

std::vector<std::string> GenericEquipment::commands() const
{
  return this->capabilities_;
}

CommandResult GenericEquipment::execute(
    const std::string &command, double /*parameter*/)
{
  if (command == "start")
  {
    if (!this->hasCapability("start"))
    {
      return {false, "capability start not present"};
    }
    this->operational_state_ = OperationalState::Running;
    return {true, "started"};
  }

  if (command == "stop")
  {
    if (!this->hasCapability("stop"))
    {
      return {false, "capability stop not present"};
    }
    this->operational_state_ = OperationalState::Stopped;
    return {true, "stopped"};
  }

  if (this->hasCapability(command))
  {
    return {true, "accepted"};
  }

  return {false, "unknown command"};
}

std::vector<TelemetryPoint> GenericEquipment::telemetry() const
{
  std::vector<TelemetryPoint> points;
  points.reserve(this->telemetry_.size());
  for (const auto &entry : this->telemetry_)
  {
    points.push_back(entry.second);
  }
  return points;
}

EquipmentStatus GenericEquipment::status() const
{
  EquipmentStatus snapshot;
  snapshot.id = this->id_;
  snapshot.type = this->type_;
  snapshot.operational_state = this->operational_state_;
  snapshot.fault = this->fault_;
  return snapshot;
}

void GenericEquipment::addCapability(std::string name)
{
  if (!this->hasCapability(name))
  {
    this->capabilities_.push_back(std::move(name));
  }
}

void GenericEquipment::setTelemetry(
    std::string name, double value, std::string unit)
{
  TelemetryPoint point;
  point.name = std::move(name);
  point.value = value;
  point.unit = std::move(unit);
  this->telemetry_[point.name] = std::move(point);
}

void GenericEquipment::setFault(bool fault)
{
  this->fault_ = fault;
}

void GenericEquipment::setOperationalState(OperationalState state)
{
  this->operational_state_ = state;
}

}  // namespace virtual_factory
