#include "modbus_rtu_test_bus.hh"

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/ModbusIndustrialAdapter.hh>

#include <iostream>
#include <string>

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

}  // namespace

int main()
{
  using virtual_factory::ConnectionState;
  using virtual_factory::ModbusAdapterConfig;
  using virtual_factory::ModbusEquipmentMapping;
  using virtual_factory::ModbusIndustrialAdapter;
  using virtual_factory::ModbusTable;
  using virtual_factory::ModbusTelemetryMapping;
  using virtual_factory::ModbusTransport;
  using virtual_factory::makeModbusRef;
  using virtual_factory::test::ModbusRtuTestBus;

  // --- missing serial device ---
  {
    ModbusAdapterConfig config;
    config.transport = ModbusTransport::Rtu;
    config.serialDevice = "/dev/does-not-exist-icp-rtu";
    config.baudRate = 9600;
    config.parity = 'N';
    config.dataBits = 8;
    config.stopBits = 1;
    config.timeoutMs = 200;
    ModbusIndustrialAdapter adapter("rtu-missing", config);
    expect(!adapter.connect(), "RTU connect fails for missing serial device");
    expect(
        adapter.connectionState() == ConnectionState::Faulted,
        "missing device leaves adapter Faulted");
    expect(
        adapter.lastError().find("serial") != std::string::npos
            || adapter.lastError().find("Unable to open") != std::string::npos
            || adapter.lastError().find("failed") != std::string::npos,
        "missing device error mentions open/serial failure");
  }

  // --- PTY loopback (mocked serial; NOT real RS-485 hardware) ---
  ModbusRtuTestBus bus;
  if (!bus.start())
  {
    std::cerr << "FAIL: could not start ModbusRtuTestBus PTY fixture" << std::endl;
    return 1;
  }

  ModbusAdapterConfig config;
  config.transport = ModbusTransport::Rtu;
  config.serialDevice = bus.clientDevice();
  config.baudRate = bus.baudRate();
  config.parity = 'N';
  config.dataBits = 8;
  config.stopBits = 1;
  config.timeoutMs = 1000;
  config.linkUnitId = ModbusRtuTestBus::kUnitId;

  ModbusEquipmentMapping eq;
  eq.id = "rtu-device";
  eq.type = "device";
  ModbusTelemetryMapping tel;
  tel.name = "speed";
  tel.unit = "rpm";
  tel.source = makeModbusRef(
      ModbusRtuTestBus::kUnitId,
      ModbusTable::HoldingRegister,
      ModbusRtuTestBus::kHoldingSpeed);
  eq.telemetry.push_back(tel);
  config.equipment.push_back(eq);

  ModbusIndustrialAdapter adapter("rtu-pty", config);
  expect(adapter.connect(), "RTU connect succeeds on PTY loopback");
  expect(
      adapter.connectionState() == ConnectionState::Connected,
      "RTU PTY adapter Connected");

  adapter.poll();
  expect(
      adapter.connectionState() == ConnectionState::Connected,
      "RTU poll keeps Connected");
  auto *equipment = adapter.equipmentById("rtu-device");
  expect(equipment != nullptr, "RTU equipment present");
  if (equipment != nullptr)
  {
    const auto points = equipment->telemetry();
    bool found = false;
    for (const auto &point : points)
    {
      if (point.name == "speed")
      {
        found = true;
        expect(point.value == 42.0, "RTU holding register telemetry value");
      }
    }
    expect(found, "RTU speed telemetry present");
  }

  bus.setHolding(ModbusRtuTestBus::kHoldingSpeed, 99);
  adapter.poll();
  if (equipment != nullptr)
  {
    for (const auto &point : equipment->telemetry())
    {
      if (point.name == "speed")
      {
        expect(point.value == 99.0, "RTU telemetry updates after write on bus");
      }
    }
  }

  adapter.disconnect();
  expect(
      adapter.connectionState() == ConnectionState::Disconnected,
      "RTU disconnect");

  bus.stop();

  if (failures != 0)
  {
    std::cerr << "modbus_rtu_adapter_test: " << failures << " failure(s)"
              << std::endl;
    return 1;
  }
  std::cout << "modbus_rtu_adapter_test: OK (PTY loopback; not RS-485 hardware)"
            << std::endl;
  return 0;
}
