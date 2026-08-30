// SOFTWARE-INTEGRATION TEST
// Host-side process-image encode/decode and GenericEquipment mapping.
// Not a REAL PROFINET TEST, REAL PROFIBUS TEST, or HARDWARE VALIDATION.

#include "hilscher/process_image_codec.hh"
#include "hilscher/process_image_mapping.hh"

#include <virtual_factory/equipment/GenericEquipment.hh>

#include <cstdint>
#include <iostream>
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

}  // namespace

int main()
{
  using virtual_factory::GenericEquipment;
  using virtual_factory::OperationalState;
  using virtual_factory::internal::ProcessValueType;
  using virtual_factory::internal::processImageRead;
  using virtual_factory::internal::processImageWrite;
  using virtual_factory::internal::applyTelemetryFromImage;
  using virtual_factory::internal::applyStateFromImage;
  using virtual_factory::internal::applyFaultFromImage;

  std::vector<std::uint8_t> image(16, 0);

  expect(processImageWrite(&image, ProcessValueType::Bool, 0, 3, 1.0), "write bool");
  double bit = 0.0;
  expect(processImageRead(image, ProcessValueType::Bool, 0, 3, &bit), "read bool");
  expect(bit == 1.0, "bool bit 3 set");

  expect(processImageWrite(&image, ProcessValueType::Uint8, 1, 0, 42.0), "write u8");
  double u8 = 0.0;
  expect(processImageRead(image, ProcessValueType::Uint8, 1, 0, &u8) && u8 == 42.0, "read u8");

  expect(processImageWrite(&image, ProcessValueType::Int16, 2, 0, -300.0), "write i16");
  double i16 = 0.0;
  expect(processImageRead(image, ProcessValueType::Int16, 2, 0, &i16) && i16 == -300.0, "read i16");

  expect(processImageWrite(&image, ProcessValueType::Uint16, 4, 0, 40000.0), "write u16");
  double u16 = 0.0;
  expect(processImageRead(image, ProcessValueType::Uint16, 4, 0, &u16) && u16 == 40000.0, "read u16");

  expect(processImageWrite(&image, ProcessValueType::Int32, 6, 0, -100000.0), "write i32");
  double i32 = 0.0;
  expect(processImageRead(image, ProcessValueType::Int32, 6, 0, &i32) && i32 == -100000.0, "read i32");

  expect(processImageWrite(&image, ProcessValueType::Real, 10, 0, 12.5), "write real");
  double real = 0.0;
  expect(processImageRead(image, ProcessValueType::Real, 10, 0, &real), "read real");
  expect(real > 12.49 && real < 12.51, "real value");

  expect(!processImageRead(image, ProcessValueType::Int32, 14, 0, &i32), "short image rejected");

  GenericEquipment equipment("EQ-001", "remote_io");
  std::vector<std::uint8_t> mapped(8, 0);
  mapped[0] = 0x05;
  mapped[2] = 0x10;
  mapped[3] = 0x00;
  expect(
      applyTelemetryFromImage(
          &equipment, "speed", ProcessValueType::Int16, 2, 0, "rpm", mapped),
      "apply telemetry");
  const auto points = equipment.telemetry();
  expect(!points.empty() && points[0].name == "speed", "telemetry name");
  expect(!points.empty() && points[0].value == 16.0, "telemetry value");

  expect(
      applyStateFromImage(
          &equipment, ProcessValueType::Bool, 0, 0, mapped),
      "apply state");
  expect(equipment.operationalState() == OperationalState::Running, "state running");

  expect(
      applyFaultFromImage(
          &equipment, ProcessValueType::Bool, 0, 2, mapped),
      "apply fault");
  expect(equipment.fault(), "fault bit 2");

  if (failures != 0)
  {
    std::cerr << failures << " SOFTWARE-INTEGRATION test failure(s)" << std::endl;
    return 1;
  }

  std::cout << "process_image_codec_test: OK (SOFTWARE-INTEGRATION)" << std::endl;
  return 0;
}
