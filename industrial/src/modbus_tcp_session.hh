#ifndef VIRTUAL_FACTORY_MODBUS_TCP_SESSION_HH_
#define VIRTUAL_FACTORY_MODBUS_TCP_SESSION_HH_

// Private industrial-layer helper. Not part of the public adapter API.
// Not included from Equipment or IndustrialAdapter headers.
// Implementation uses libmodbus; this header does not include <modbus.h>.

#include <cstdint>
#include <string>

namespace virtual_factory
{
namespace internal
{

/// Blocking Modbus TCP client session (function codes 1–6) via libmodbus.
class ModbusTcpSession
{
public:
  ModbusTcpSession() = default;
  ~ModbusTcpSession();

  ModbusTcpSession(const ModbusTcpSession &) = delete;
  ModbusTcpSession &operator=(const ModbusTcpSession &) = delete;

  bool connect(const std::string &host, std::uint16_t port, int timeoutMs);
  void close();
  bool connected() const;

  bool readCoils(
      std::uint8_t unitId, std::uint16_t address, std::uint8_t *bit);
  bool readDiscreteInputs(
      std::uint8_t unitId, std::uint16_t address, std::uint8_t *bit);
  bool readHoldingRegisters(
      std::uint8_t unitId, std::uint16_t address, std::uint16_t *value);
  bool readInputRegisters(
      std::uint8_t unitId, std::uint16_t address, std::uint16_t *value);
  bool writeCoil(
      std::uint8_t unitId, std::uint16_t address, bool value);
  bool writeHoldingRegister(
      std::uint8_t unitId, std::uint16_t address, std::uint16_t value);

  std::string lastError() const;

private:
  bool prepare(std::uint8_t unitId);
  void captureError(const char *prefix);

  void *ctx_{nullptr};
  std::string last_error_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
