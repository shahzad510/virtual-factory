#ifndef VIRTUAL_FACTORY_CONVEYOR_HH_
#define VIRTUAL_FACTORY_CONVEYOR_HH_

#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>

namespace virtual_factory
{

/// Specialized equipment implementation for the current Gazebo conveyor.
///
/// Architectural role: one concrete example, not the equipment architecture.
/// Speed stays here. Generic clients see speed only as telemetry "speed" (m/s)
/// and capability "speed_control". Gazebo lives in ConveyorSystem, not here.
class Conveyor : public Equipment
{
public:
  static constexpr double kDefaultSpeedMetersPerSecond = 0.5;

  explicit Conveyor(std::string id);

  std::string id() const override;
  std::string type() const override;
  OperationalState operationalState() const override;
  bool fault() const override;

  std::vector<std::string> capabilities() const override;
  std::vector<std::string> commands() const override;
  CommandResult execute(
      const std::string &command, double parameter = 0.0) override;

  std::vector<TelemetryPoint> telemetry() const override;
  EquipmentStatus status() const override;

  double speed() const;
  bool setSpeed(double speedMetersPerSecond);
  void setFault(bool fault);

private:
  std::string id_;
  OperationalState operational_state_{OperationalState::Stopped};
  double speed_{0.0};
  bool fault_{false};
};

}  // namespace virtual_factory

#endif
