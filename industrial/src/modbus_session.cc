#include "modbus_session.hh"

#include <errno.h>
#include <modbus.h>

#include <string>

namespace virtual_factory
{
namespace internal
{

namespace
{

modbus_t *asCtx(void *ctx)
{
  return static_cast<modbus_t *>(ctx);
}

bool isLibmodbusException(int err)
{
  switch (err)
  {
    case EMBXILFUN:
    case EMBXILADD:
    case EMBXILVAL:
    case EMBXSFAIL:
    case EMBXACK:
    case EMBXMEMPAR:
    case EMBXGPATH:
    case EMBXGTAR:
      return true;
    default:
      return false;
  }
}

}  // namespace

ModbusSession::~ModbusSession()
{
  this->close();
}

void ModbusSession::applyTimeout(int timeoutMs)
{
  if (this->ctx_ == nullptr)
  {
    return;
  }
  const int clamped = timeoutMs < 1 ? 1 : timeoutMs;
  const uint32_t sec = static_cast<uint32_t>(clamped / 1000);
  const uint32_t usec = static_cast<uint32_t>((clamped % 1000) * 1000);
  (void)modbus_set_response_timeout(asCtx(this->ctx_), sec, usec);
  (void)modbus_set_byte_timeout(asCtx(this->ctx_), sec, usec);
}

bool ModbusSession::connectTcp(
    const std::string &host, std::uint16_t port, int timeoutMs)
{
  this->close();

  modbus_t *ctx = modbus_new_tcp(host.c_str(), static_cast<int>(port));
  if (ctx == nullptr)
  {
    this->last_error_ = "failed to create libmodbus TCP context";
    this->last_error_exception_ = false;
    return false;
  }

  this->ctx_ = ctx;
  this->applyTimeout(timeoutMs);
  (void)modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE);

  if (modbus_connect(ctx) == -1)
  {
    this->captureError("Modbus TCP connect failed");
    this->close();
    return false;
  }

  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::connectRtu(
    const std::string &device,
    int baudRate,
    char parity,
    int dataBits,
    int stopBits,
    int timeoutMs)
{
  this->close();

  if (device.empty())
  {
    this->last_error_ = "serial device path is empty";
    this->last_error_exception_ = false;
    return false;
  }

  modbus_t *ctx = modbus_new_rtu(
      device.c_str(), baudRate, parity, dataBits, stopBits);
  if (ctx == nullptr)
  {
    this->last_error_ =
        "failed to create libmodbus RTU context for " + device;
    this->last_error_exception_ = false;
    return false;
  }

  this->ctx_ = ctx;
  this->applyTimeout(timeoutMs);
  (void)modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE);

  if (modbus_connect(ctx) == -1)
  {
    this->captureError("Unable to open serial port " + device);
    this->close();
    return false;
  }

  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

void ModbusSession::close()
{
  if (this->ctx_ != nullptr)
  {
    modbus_t *ctx = asCtx(this->ctx_);
    modbus_close(ctx);
    modbus_free(ctx);
    this->ctx_ = nullptr;
  }
}

bool ModbusSession::connected() const
{
  return this->ctx_ != nullptr;
}

bool ModbusSession::prepare(std::uint8_t unitId)
{
  if (this->ctx_ == nullptr)
  {
    this->last_error_ = "Modbus session is closed";
    this->last_error_exception_ = false;
    return false;
  }
  if (modbus_set_slave(asCtx(this->ctx_), static_cast<int>(unitId)) == -1)
  {
    this->captureError("failed to set Modbus unit id");
    return false;
  }
  return true;
}

void ModbusSession::captureError(const std::string &prefix)
{
  this->last_error_exception_ = isLibmodbusException(errno);
  this->last_error_ = prefix + ": " + modbus_strerror(errno);
}

bool ModbusSession::lastErrorWasException() const
{
  return this->last_error_exception_;
}

bool ModbusSession::readCoils(
    std::uint8_t unitId, std::uint16_t address, std::uint8_t *bit)
{
  if (bit == nullptr || !this->prepare(unitId))
  {
    return false;
  }
  uint8_t dest = 0;
  if (modbus_read_bits(asCtx(this->ctx_), static_cast<int>(address), 1, &dest) !=
      1)
  {
    this->captureError("coil read failed");
    return false;
  }
  *bit = dest != 0 ? 1 : 0;
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::readDiscreteInputs(
    std::uint8_t unitId, std::uint16_t address, std::uint8_t *bit)
{
  if (bit == nullptr || !this->prepare(unitId))
  {
    return false;
  }
  uint8_t dest = 0;
  if (modbus_read_input_bits(
          asCtx(this->ctx_), static_cast<int>(address), 1, &dest) != 1)
  {
    this->captureError("discrete-input read failed");
    return false;
  }
  *bit = dest != 0 ? 1 : 0;
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::readHoldingRegisters(
    std::uint8_t unitId, std::uint16_t address, std::uint16_t *value)
{
  if (value == nullptr || !this->prepare(unitId))
  {
    return false;
  }
  uint16_t dest = 0;
  if (modbus_read_registers(
          asCtx(this->ctx_), static_cast<int>(address), 1, &dest) != 1)
  {
    this->captureError("holding-register read failed");
    return false;
  }
  *value = dest;
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::readInputRegisters(
    std::uint8_t unitId, std::uint16_t address, std::uint16_t *value)
{
  if (value == nullptr || !this->prepare(unitId))
  {
    return false;
  }
  uint16_t dest = 0;
  if (modbus_read_input_registers(
          asCtx(this->ctx_), static_cast<int>(address), 1, &dest) != 1)
  {
    this->captureError("input-register read failed");
    return false;
  }
  *value = dest;
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::writeCoil(
    std::uint8_t unitId, std::uint16_t address, bool value)
{
  if (!this->prepare(unitId))
  {
    return false;
  }
  if (modbus_write_bit(
          asCtx(this->ctx_), static_cast<int>(address), value ? TRUE : FALSE) !=
      1)
  {
    this->captureError("coil write failed");
    return false;
  }
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

bool ModbusSession::writeHoldingRegister(
    std::uint8_t unitId, std::uint16_t address, std::uint16_t value)
{
  if (!this->prepare(unitId))
  {
    return false;
  }
  if (modbus_write_register(
          asCtx(this->ctx_), static_cast<int>(address), value) != 1)
  {
    this->captureError("holding-register write failed");
    return false;
  }
  this->last_error_.clear();
  this->last_error_exception_ = false;
  return true;
}

std::string ModbusSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
