#include "modbus_tcp_session.hh"

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

void applyTimeout(modbus_t *ctx, int timeoutMs)
{
  const int clamped = timeoutMs < 1 ? 1 : timeoutMs;
  const uint32_t sec = static_cast<uint32_t>(clamped / 1000);
  const uint32_t usec = static_cast<uint32_t>((clamped % 1000) * 1000);
  (void)modbus_set_response_timeout(ctx, sec, usec);
  (void)modbus_set_byte_timeout(ctx, sec, usec);
}

}  // namespace

ModbusTcpSession::~ModbusTcpSession()
{
  this->close();
}

bool ModbusTcpSession::connect(
    const std::string &host, std::uint16_t port, int timeoutMs)
{
  this->close();

  modbus_t *ctx = modbus_new_tcp(host.c_str(), static_cast<int>(port));
  if (ctx == nullptr)
  {
    this->last_error_ = "failed to create libmodbus TCP context";
    return false;
  }

  applyTimeout(ctx, timeoutMs);
  (void)modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE);

  if (modbus_connect(ctx) == -1)
  {
    this->ctx_ = ctx;
    this->captureError("Modbus TCP connect failed");
    this->close();
    return false;
  }

  this->ctx_ = ctx;
  this->last_error_.clear();
  return true;
}

void ModbusTcpSession::close()
{
  if (this->ctx_ != nullptr)
  {
    modbus_t *ctx = asCtx(this->ctx_);
    modbus_close(ctx);
    modbus_free(ctx);
    this->ctx_ = nullptr;
  }
}

bool ModbusTcpSession::connected() const
{
  return this->ctx_ != nullptr;
}

bool ModbusTcpSession::prepare(std::uint8_t unitId)
{
  if (this->ctx_ == nullptr)
  {
    this->last_error_ = "Modbus TCP session is closed";
    return false;
  }
  if (modbus_set_slave(asCtx(this->ctx_), static_cast<int>(unitId)) == -1)
  {
    this->captureError("failed to set Modbus unit id");
    return false;
  }
  return true;
}

void ModbusTcpSession::captureError(const char *prefix)
{
  this->last_error_ = std::string(prefix) + ": " + modbus_strerror(errno);
}

bool ModbusTcpSession::readCoils(
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
  return true;
}

bool ModbusTcpSession::readDiscreteInputs(
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
  return true;
}

bool ModbusTcpSession::readHoldingRegisters(
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
  return true;
}

bool ModbusTcpSession::readInputRegisters(
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
  return true;
}

bool ModbusTcpSession::writeCoil(
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
  return true;
}

bool ModbusTcpSession::writeHoldingRegister(
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
  return true;
}

std::string ModbusTcpSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
