#ifndef VIRTUAL_FACTORY_MODBUS_SESSION_HH_
#define VIRTUAL_FACTORY_MODBUS_SESSION_HH_

// Private industrial-layer helper. Not part of the public adapter API.
// Not included from Equipment or IndustrialAdapter headers.
// Implementation uses libmodbus; this header does not include <modbus.h>.

#include <cstdint>
#include <string>

namespace virtual_factory
{
namespace internal
{

/// Blocking Modbus client session (FC 1–6) via libmodbus for TCP or RTU.
class ModbusSession
{
public:
  ModbusSession() = default;
  ~ModbusSession();

  ModbusSession(const ModbusSession &) = delete;
  ModbusSession &operator=(const ModbusSession &) = delete;

  bool connectTcp(const std::string &host, std::uint16_t port, int timeoutMs);
  /// Opens the serial device and configures RTU framing (parity: 'N'|'E'|'O').
  bool connectRtu(
      const std::string &device,
      int baudRate,
      char parity,
      int dataBits,
      int stopBits,
      int timeoutMs);
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

  /// True when the last failed I/O received a Modbus exception from a slave
  /// (proof of RTU framing / link presence even when the address is illegal).
  bool lastErrorWasException() const;

  std::string lastError() const;

private:
  bool prepare(std::uint8_t unitId);
  void captureError(const std::string &prefix);
  void applyTimeout(int timeoutMs);

  void *ctx_{nullptr};
  std::string last_error_;
  bool last_error_exception_{false};
};

}  // namespace internal
}  // namespace virtual_factory

#endif
