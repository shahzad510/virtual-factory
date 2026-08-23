#ifndef VIRTUAL_FACTORY_EQUIPMENT_HH_
#define VIRTUAL_FACTORY_EQUIPMENT_HH_

#include <algorithm>
#include <string>
#include <vector>

namespace virtual_factory
{

/// Coarse operating mode shared by industrial equipment.
/// Type-specific quantities belong in telemetry, not here.
enum class OperationalState
{
  Stopped,
  Running
};

/// One generic measurement. Names are open-ended (speed, pressure, rpm, …).
struct TelemetryPoint
{
  std::string name;
  double value{0.0};
  std::string unit;
};

/// Result of a generic named command.
struct CommandResult
{
  bool accepted{false};
  std::string message;
};

/// Snapshot of normalized equipment state. No machine-specific fields.
struct EquipmentStatus
{
  std::string id;
  std::string type;
  OperationalState operational_state{OperationalState::Stopped};
  bool fault{false};
};

/// Gazebo-independent generic industrial equipment contract (SoT Phase 5).
///
/// This is an OPEN-ENDED normalized model, not a catalog of machine classes.
/// `type()` is metadata (e.g. "belt_conveyor", "special_processing_machine"),
/// not a switch the MES/SCADA core should branch on.
///
/// Common operations: identity, operational state, start/stop, fault,
/// capabilities, named commands, telemetry.
///
/// Two ways to exist in the system:
/// - GenericEquipment: arbitrary type + capabilities, no new C++ class
/// - Specialized class (e.g. Conveyor): only when custom logic is required
///
/// Must not include Gazebo, OPC UA, Modbus, REST, or vendor SDKs.
class Equipment
{
public:
  virtual ~Equipment() = default;

  virtual std::string id() const = 0;

  /// Equipment kind as metadata. Not a closed enum of C++ types.
  virtual std::string type() const = 0;

  virtual OperationalState operationalState() const = 0;
  virtual bool fault() const = 0;

  bool running() const
  {
    return this->operationalState() == OperationalState::Running;
  }

  /// Feature tags this instance supports, e.g. "start", "stop",
  /// "speed_control". Open-ended strings; not a fixed C++ catalog.
  virtual std::vector<std::string> capabilities() const = 0;

  bool hasCapability(const std::string &name) const
  {
    const auto caps = this->capabilities();
    return std::find(caps.begin(), caps.end(), name) != caps.end();
  }

  /// Invocable command names, e.g. "start", "stop", "set_speed".
  virtual std::vector<std::string> commands() const = 0;

  /// Generic command path. Unknown names are rejected.
  /// `parameter` is optional (e.g. speed for "set_speed").
  virtual CommandResult execute(
      const std::string &command, double parameter = 0.0) = 0;

  void start()
  {
    (void)this->execute("start");
  }

  void stop()
  {
    (void)this->execute("stop");
  }

  virtual std::vector<TelemetryPoint> telemetry() const = 0;
  virtual EquipmentStatus status() const = 0;
};

}  // namespace virtual_factory

#endif
