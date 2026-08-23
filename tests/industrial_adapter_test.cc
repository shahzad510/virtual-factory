#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>
#include <virtual_factory/industrial/MockIndustrialAdapter.hh>

#include <cstdlib>
#include <iostream>
#include <string>
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

/// MES/SCADA-shaped client: IndustrialAdapter + Equipment only.
/// Does not include mock-specific APIs or protocol headers.
void useAsMes(virtual_factory::IndustrialAdapter &adapter)
{
  expect(adapter.connect(), "MES client can connect");
  expect(adapter.connected(), "MES client sees connected");
  expect(adapter.protocol() == "mock",
         "protocol is metadata, not a machine class");

  virtual_factory::Equipment *pump = adapter.equipmentById("P-001");
  expect(pump != nullptr, "MES client finds equipment by id");
  expect(pump->type() == "centrifugal_pump", "type is metadata");
  expect(pump->hasCapability("start"), "MES sees start capability");
  expect(pump->hasCapability("set_speed"), "MES sees set_speed capability");
  expect(!pump->hasCapability("load_recipe"),
         "MES does not assume conveyor/recipe commands");

  adapter.poll();
  const auto pumpTelemetry = pump->telemetry();
  const auto *pressure = findTelemetry(pumpTelemetry, "pressure");
  expect(pressure != nullptr && pressure->value == 4.2 &&
             pressure->unit == "bar",
         "MES reads pressure telemetry without a Pump class");
  const auto *flow = findTelemetry(pumpTelemetry, "flow_rate");
  expect(flow != nullptr && flow->value == 20.0,
         "MES reads arbitrary telemetry name flow_rate");
  expect(findTelemetry(pumpTelemetry, "speed") == nullptr,
         "MES does not require speed on every machine");

  expect(pump->execute("start").accepted, "MES start via Equipment");
  expect(pump->running(), "MES sees running after start");
  expect(pump->execute("set_speed", 1450.0).accepted,
         "MES set_speed via generic execute");
  const auto pumpTelemetryAfter = pump->telemetry();
  const auto *rpm = findTelemetry(pumpTelemetryAfter, "speed");
  expect(rpm != nullptr && rpm->value == 1450.0,
         "set_speed updates telemetry through the adapter");

  expect(!pump->execute("calibrate").accepted,
         "MES unsupported command rejected");

  virtual_factory::Equipment *mystery = adapter.equipmentById("MX-017");
  expect(mystery != nullptr, "MES finds arbitrary machine");
  expect(mystery->type() == "special_processing_machine",
         "unknown machine needs no C++ class");
  expect(mystery->execute("set_temperature", 850.0).accepted,
         "arbitrary set_temperature command");
  const auto mysteryTelemetry = mystery->telemetry();
  const auto *temp = findTelemetry(mysteryTelemetry, "temperature");
  expect(temp != nullptr && temp->value == 850.0,
         "arbitrary temperature telemetry");
  expect(mystery->execute("execute_recipe").accepted,
         "execute_recipe via capability, not an Equipment method");
}

}  // namespace

int main()
{
  virtual_factory::MockIndustrialAdapter adapter("adapter-mock-1");

  expect(adapter.id() == "adapter-mock-1", "adapter construction id");
  expect(adapter.protocol() == "mock", "protocol is mock");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "initial connection state is Disconnected");
  expect(adapter.equipment().empty(), "no equipment while disconnected");
  expect(adapter.equipmentById("P-001") == nullptr,
         "lookup fails while disconnected");

  adapter.addDevice("P-001", "centrifugal_pump");
  adapter.addCapability("P-001", "start");
  adapter.addCapability("P-001", "stop");
  adapter.addCapability("P-001", "set_speed");
  adapter.setSourceTelemetry("P-001", "pressure", 4.2, "bar");
  adapter.setSourceTelemetry("P-001", "flow_rate", 20.0, "L/min");
  adapter.setSourceTelemetry("P-001", "temperature", 35.0, "degC");

  adapter.addDevice("MX-017", "special_processing_machine");
  adapter.addCapability("MX-017", "start");
  adapter.addCapability("MX-017", "stop");
  adapter.addCapability("MX-017", "set_temperature");
  adapter.addCapability("MX-017", "execute_recipe");
  adapter.addCapability("MX-017", "reset");
  adapter.setSourceTelemetry("MX-017", "temperature", 25.0, "degC");
  adapter.setSourceTelemetry("MX-017", "pressure", 1.1, "bar");
  adapter.setSourceTelemetry("MX-017", "material_level", 62.0, "%");

  expect(adapter.connect(), "connect succeeds");
  expect(adapter.equipment().size() == 2, "two devices exposed after connect");

  useAsMes(adapter);

  adapter.disconnect();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "disconnect returns to Disconnected");
  expect(adapter.equipment().empty(), "equipment hidden after disconnect");

  expect(adapter.connect(), "reconnect succeeds");
  virtual_factory::Equipment *pump = adapter.equipmentById("P-001");
  expect(pump != nullptr, "equipment returns after reconnect");

  adapter.simulateCommunicationFailure("link lost");
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Faulted,
         "communication failure is Faulted");
  expect(adapter.lastError() == "link lost", "failure reason is visible");
  expect(!adapter.connected(), "Faulted is not connected");
  expect(adapter.equipmentById("P-001") != nullptr,
         "last-known equipment remains while Faulted");
  expect(!pump->execute("stop").accepted,
         "commands rejected during communication fault");
  expect(!adapter.connect(), "cannot connect while faulted");

  adapter.poll();
  expect(!adapter.lastError().empty(), "poll while faulted keeps an error");

  adapter.clearCommunicationFailure();
  expect(adapter.connectionState() ==
             virtual_factory::ConnectionState::Disconnected,
         "cleared fault returns to Disconnected");
  expect(adapter.connect(), "connect after cleared fault");
  expect(adapter.connected(), "recovered adapter is connected");

  virtual_factory::IndustrialAdapter *asContract = &adapter;
  expect(asContract->protocol() == "mock",
         "usable through IndustrialAdapter*");
  virtual_factory::Equipment *asEquipment =
      asContract->equipmentById("MX-017");
  expect(asEquipment != nullptr, "equipment through IndustrialAdapter*");
  expect(asEquipment->hasCapability("reset"),
         "capabilities through generic Equipment");

  if (failures != 0)
  {
    std::cerr << failures << " assertion(s) failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "industrial_adapter_test: all assertions passed" << std::endl;
  return EXIT_SUCCESS;
}
