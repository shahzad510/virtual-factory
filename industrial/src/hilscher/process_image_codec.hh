#ifndef VIRTUAL_FACTORY_PROCESS_IMAGE_CODEC_HH_
#define VIRTUAL_FACTORY_PROCESS_IMAGE_CODEC_HH_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace internal
{

enum class ProcessValueType
{
  Bool,
  Uint8,
  Int16,
  Uint16,
  Int32,
  Real
};

inline bool processImageRead(
    const std::vector<std::uint8_t> &image,
    ProcessValueType type,
    std::size_t byteOffset,
    std::size_t bitOffset,
    double *out)
{
  if (out == nullptr)
  {
    return false;
  }

  auto need = [](ProcessValueType t) -> std::size_t {
    switch (t)
    {
      case ProcessValueType::Bool:
      case ProcessValueType::Uint8:
        return 1;
      case ProcessValueType::Int16:
      case ProcessValueType::Uint16:
        return 2;
      case ProcessValueType::Int32:
      case ProcessValueType::Real:
        return 4;
    }
    return 0;
  };

  const std::size_t length = need(type);
  if (byteOffset + length > image.size())
  {
    return false;
  }

  switch (type)
  {
    case ProcessValueType::Bool:
    {
      const std::uint8_t bit = static_cast<std::uint8_t>(bitOffset & 7U);
      *out = ((image[byteOffset] >> bit) & 1U) ? 1.0 : 0.0;
      return true;
    }
    case ProcessValueType::Uint8:
      *out = static_cast<double>(image[byteOffset]);
      return true;
    case ProcessValueType::Int16:
    {
      std::int16_t value = 0;
      std::memcpy(&value, image.data() + byteOffset, 2);
      *out = static_cast<double>(value);
      return true;
    }
    case ProcessValueType::Uint16:
    {
      std::uint16_t value = 0;
      std::memcpy(&value, image.data() + byteOffset, 2);
      *out = static_cast<double>(value);
      return true;
    }
    case ProcessValueType::Int32:
    {
      std::int32_t value = 0;
      std::memcpy(&value, image.data() + byteOffset, 4);
      *out = static_cast<double>(value);
      return true;
    }
    case ProcessValueType::Real:
    {
      float value = 0.0f;
      std::memcpy(&value, image.data() + byteOffset, 4);
      *out = static_cast<double>(value);
      return true;
    }
  }
  return false;
}

inline bool processImageWrite(
    std::vector<std::uint8_t> *image,
    ProcessValueType type,
    std::size_t byteOffset,
    std::size_t bitOffset,
    double value)
{
  if (image == nullptr)
  {
    return false;
  }

  auto need = [](ProcessValueType t) -> std::size_t {
    switch (t)
    {
      case ProcessValueType::Bool:
      case ProcessValueType::Uint8:
        return 1;
      case ProcessValueType::Int16:
      case ProcessValueType::Uint16:
        return 2;
      case ProcessValueType::Int32:
      case ProcessValueType::Real:
        return 4;
    }
    return 0;
  };

  const std::size_t length = need(type);
  if (byteOffset + length > image->size())
  {
    return false;
  }

  switch (type)
  {
    case ProcessValueType::Bool:
    {
      const std::uint8_t bit = static_cast<std::uint8_t>(bitOffset & 7U);
      if (value != 0.0)
      {
        (*image)[byteOffset] =
            static_cast<std::uint8_t>((*image)[byteOffset] | (1U << bit));
      }
      else
      {
        (*image)[byteOffset] = static_cast<std::uint8_t>(
            (*image)[byteOffset] & static_cast<std::uint8_t>(~(1U << bit)));
      }
      return true;
    }
    case ProcessValueType::Uint8:
      (*image)[byteOffset] = static_cast<std::uint8_t>(value);
      return true;
    case ProcessValueType::Int16:
    {
      const std::int16_t encoded = static_cast<std::int16_t>(value);
      std::memcpy(image->data() + byteOffset, &encoded, 2);
      return true;
    }
    case ProcessValueType::Uint16:
    {
      const std::uint16_t encoded = static_cast<std::uint16_t>(value);
      std::memcpy(image->data() + byteOffset, &encoded, 2);
      return true;
    }
    case ProcessValueType::Int32:
    {
      const std::int32_t encoded = static_cast<std::int32_t>(value);
      std::memcpy(image->data() + byteOffset, &encoded, 4);
      return true;
    }
    case ProcessValueType::Real:
    {
      const float encoded = static_cast<float>(value);
      std::memcpy(image->data() + byteOffset, &encoded, 4);
      return true;
    }
  }
  return false;
}

}  // namespace internal
}  // namespace virtual_factory

#endif
