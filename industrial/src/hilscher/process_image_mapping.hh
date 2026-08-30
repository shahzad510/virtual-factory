#ifndef VIRTUAL_FACTORY_PROCESS_IMAGE_MAPPING_HH_
#define VIRTUAL_FACTORY_PROCESS_IMAGE_MAPPING_HH_

#include "process_image_codec.hh"

#include <virtual_factory/equipment/GenericEquipment.hh>

#include <string>
#include <vector>

namespace virtual_factory
{
namespace internal
{

inline bool applyTelemetryFromImage(
    GenericEquipment *equipment,
    const std::string &name,
    ProcessValueType type,
    std::size_t byteOffset,
    std::size_t bitOffset,
    const std::string &unit,
    const std::vector<std::uint8_t> &image)
{
  if (equipment == nullptr || name.empty())
  {
    return false;
  }
  double value = 0.0;
  if (!processImageRead(image, type, byteOffset, bitOffset, &value))
  {
    return false;
  }
  equipment->setTelemetry(name, value, unit);
  return true;
}

inline bool applyStateFromImage(
    GenericEquipment *equipment,
    ProcessValueType type,
    std::size_t byteOffset,
    std::size_t bitOffset,
    const std::vector<std::uint8_t> &image)
{
  if (equipment == nullptr)
  {
    return false;
  }
  double value = 0.0;
  if (!processImageRead(image, type, byteOffset, bitOffset, &value))
  {
    return false;
  }
  equipment->setOperationalState(
      value != 0.0 ? OperationalState::Running : OperationalState::Stopped);
  return true;
}

inline bool applyFaultFromImage(
    GenericEquipment *equipment,
    ProcessValueType type,
    std::size_t byteOffset,
    std::size_t bitOffset,
    const std::vector<std::uint8_t> &image)
{
  if (equipment == nullptr)
  {
    return false;
  }
  double value = 0.0;
  if (!processImageRead(image, type, byteOffset, bitOffset, &value))
  {
    return false;
  }
  equipment->setFault(value != 0.0);
  return true;
}

}  // namespace internal
}  // namespace virtual_factory

#endif
