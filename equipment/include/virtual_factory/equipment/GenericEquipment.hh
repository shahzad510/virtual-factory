#ifndef VIRTUAL_FACTORY_GENERIC_EQUIPMENT_HH_
#define VIRTUAL_FACTORY_GENERIC_EQUIPMENT_HH_

#include <map>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>

namespace virtual_factory
{

/// Configurable equipment: any type string and capability set, no new C++ class.
///
/// Use this for ordinary machines that can be represented as identity + state +
/// capabilities + telemetry. Specialized subclasses (Conveyor) exist only when
/// custom rules are required (e.g. default belt speed).
class GenericEquipment : public Equipment
{
public:
  GenericEquipment(std::string id, std::string type);

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

  void addCapability(std::string name);
  void setTelemetry(std::string name, double value, std::string unit);
  void setFault(bool fault);

  /// Map protocol/process state into the model without executing a command.
  /// Adapters use this after reading a state node; it must not write to a PLC.
  void setOperationalState(OperationalState state);

private:
  std::string id_;
  std::string type_;
  OperationalState operational_state_{OperationalState::Stopped};
  bool fault_{false};
  std::vector<std::string> capabilities_;
  std::map<std::string, TelemetryPoint> telemetry_;
};

}  // namespace virtual_factory

#endif
