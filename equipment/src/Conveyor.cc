#include <virtual_factory/equipment/Conveyor.hh>

#include <utility>

namespace virtual_factory
{

Conveyor::Conveyor(std::string id)
    : id_(std::move(id))
{
}

std::string Conveyor::id() const
{
  return this->id_;
}

std::string Conveyor::type() const
{
  return "belt_conveyor";
}

OperationalState Conveyor::operationalState() const
{
  return this->operational_state_;
}

bool Conveyor::fault() const
{
  return this->fault_;
}

std::vector<std::string> Conveyor::capabilities() const
{
  return {"start", "stop", "speed_control"};
}

std::vector<std::string> Conveyor::commands() const
{
  return {"start", "stop", "set_speed"};
}

CommandResult Conveyor::execute(
    const std::string &command, double parameter)
{
  if (command == "start")
  {
    this->operational_state_ = OperationalState::Running;
    if (this->speed_ <= 0.0)
    {
      this->speed_ = kDefaultSpeedMetersPerSecond;
    }
    return {true, "started"};
  }

  if (command == "stop")
  {
    this->operational_state_ = OperationalState::Stopped;
    this->speed_ = 0.0;
    return {true, "stopped"};
  }

  if (command == "set_speed")
  {
    if (!this->setSpeed(parameter))
    {
      return {false, "invalid speed"};
    }
    return {true, "speed set"};
  }

  return {false, "unknown command"};
}

std::vector<TelemetryPoint> Conveyor::telemetry() const
{
  return {{"speed", this->speed_, "m/s"}};
}

EquipmentStatus Conveyor::status() const
{
  EquipmentStatus snapshot;
  snapshot.id = this->id_;
  snapshot.type = this->type();
  snapshot.operational_state = this->operational_state_;
  snapshot.fault = this->fault_;
  return snapshot;
}

double Conveyor::speed() const
{
  return this->speed_;
}

bool Conveyor::setSpeed(double speedMetersPerSecond)
{
  if (speedMetersPerSecond < 0.0)
  {
    return false;
  }

  this->speed_ = speedMetersPerSecond;
  return true;
}

void Conveyor::setFault(bool fault)
{
  this->fault_ = fault;
}

}  // namespace virtual_factory
