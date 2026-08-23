#include <virtual_factory/equipment/Conveyor.hh>
#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/equipment/GenericEquipment.hh>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

const virtual_factory::TelemetryPoint *findTelemetry(
    const std::vector<virtual_factory::TelemetryPoint> &points,
    const std::string &name)
{
  for (const auto &point : points)
  {
    if (point.name == name)
    {
      return &point;
    }
  }
  return nullptr;
}

}  // namespace

int main()
{
  static_assert(
      !std::is_abstract<virtual_factory::GenericEquipment>::value,
      "GenericEquipment must be instantiable");

  virtual_factory::Conveyor conveyor("CV-001");

  expect(conveyor.id() == "CV-001", "id is CV-001");
  expect(conveyor.type() == "belt_conveyor", "type is metadata belt_conveyor");
  expect(conveyor.hasCapability("start"), "conveyor has start");
  expect(conveyor.hasCapability("stop"), "conveyor has stop");
  expect(conveyor.hasCapability("speed_control"), "conveyor has speed_control");
  expect(!conveyor.hasCapability("load_recipe"), "no invented capabilities");

  expect(conveyor.operationalState() ==
             virtual_factory::OperationalState::Stopped,
         "initial operational state is Stopped");
  expect(!conveyor.running(), "initial running is false");
  expect(conveyor.speed() == 0.0, "initial conveyor speed is 0");
  expect(!conveyor.fault(), "initial fault is false");

  conveyor.start();
  expect(conveyor.running(), "start sets running");
  expect(conveyor.speed() ==
             virtual_factory::Conveyor::kDefaultSpeedMetersPerSecond,
         "conveyor start applies default speed 0.5 m/s");

  expect(conveyor.setSpeed(1.25), "conveyor setSpeed(1.25) accepted");
  expect(conveyor.speed() == 1.25, "conveyor speed is 1.25 m/s");

  const auto viaCommand = conveyor.execute("set_speed", 0.8);
  expect(viaCommand.accepted, "generic execute set_speed accepted");
  expect(conveyor.speed() == 0.8, "execute set_speed updates conveyor speed");

  expect(!conveyor.execute("set_speed", -0.1).accepted,
         "generic execute rejects negative speed");
  expect(conveyor.speed() == 0.8, "rejected execute leaves speed unchanged");
  expect(!conveyor.execute("calibrate").accepted,
         "unknown conveyor command rejected");

  conveyor.setFault(true);
  expect(conveyor.fault(), "setFault(true) is visible");

  const auto *speedPoint = findTelemetry(conveyor.telemetry(), "speed");
  expect(speedPoint != nullptr && speedPoint->value == 0.8 &&
             speedPoint->unit == "m/s",
         "conveyor speed is telemetry, not a generic Equipment field");

  conveyor.stop();
  expect(!conveyor.running(), "stop clears running");
  expect(conveyor.speed() == 0.0, "stop sets conveyor speed to 0");
  expect(conveyor.fault(), "stop does not clear fault");

  // Generic contract: Equipment* has no speed API.
  std::unique_ptr<virtual_factory::Equipment> asContract =
      std::make_unique<virtual_factory::Conveyor>("CV-002");
  asContract->start();
  expect(asContract->running(), "Equipment* start");
  expect(findTelemetry(asContract->telemetry(), "speed") != nullptr,
         "Equipment* sees speed only via telemetry");
  asContract->stop();
  expect(!asContract->running(), "Equipment* stop");

  // Arbitrary machine without a dedicated C++ class.
  virtual_factory::GenericEquipment mystery("MX-017",
                                            "special_processing_machine");
  mystery.addCapability("start");
  mystery.addCapability("stop");
  mystery.addCapability("load_recipe");
  mystery.setTelemetry("chamber_temperature", 850.0, "degC");
  mystery.setTelemetry("pressure", 4.2, "bar");
  mystery.setTelemetry("material_level", 62.0, "%");
  mystery.setFault(false);

  virtual_factory::Equipment *generic = &mystery;
  expect(generic->id() == "MX-017", "generic id");
  expect(generic->type() == "special_processing_machine",
         "type is metadata, not a C++ class name");
  expect(generic->hasCapability("load_recipe"), "arbitrary capability");
  expect(!generic->hasCapability("speed_control"),
         "generic machine does not require speed");
  expect(findTelemetry(generic->telemetry(), "speed") == nullptr,
         "generic telemetry need not include speed");
  expect(findTelemetry(generic->telemetry(), "chamber_temperature") != nullptr &&
             findTelemetry(generic->telemetry(), "chamber_temperature")->value ==
                 850.0,
         "arbitrary telemetry name chamber_temperature");
  expect(findTelemetry(generic->telemetry(), "pressure") != nullptr,
         "arbitrary telemetry name pressure");

  const auto started = generic->execute("start");
  expect(started.accepted, "generic start accepted");
  expect(generic->running(), "generic start runs");
  expect(generic->execute("load_recipe").accepted, "named command via capability");
  expect(!generic->execute("set_speed", 0.5).accepted,
         "generic machine rejects conveyor set_speed");
  generic->stop();
  expect(!generic->running(), "generic stop");

  mystery.setFault(true);
  expect(generic->fault(), "generic fault");

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "equipment_test: all assertions passed" << std::endl;
  return EXIT_SUCCESS;
}
