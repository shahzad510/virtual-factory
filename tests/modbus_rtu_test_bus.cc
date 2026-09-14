#include "modbus_rtu_test_bus.hh"

#include <errno.h>
#include <modbus.h>
#include <pty.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace virtual_factory
{
namespace test
{

ModbusRtuTestBus::ModbusRtuTestBus() = default;

ModbusRtuTestBus::~ModbusRtuTestBus()
{
  this->stop();
}

bool ModbusRtuTestBus::start()
{
  this->stop();

  char slaveName[256];
  if (openpty(&this->master_fd_, &this->slave_fd_, slaveName, nullptr, nullptr) !=
      0)
  {
    std::cerr << "ModbusRtuTestBus: openpty failed: " << std::strerror(errno)
              << std::endl;
    return false;
  }
  this->client_path_ = slaveName;
  // Only the Modbus client should open the slave path.
  ::close(this->slave_fd_);
  this->slave_fd_ = -1;

  modbus_t *ctx = modbus_new_rtu(this->client_path_.c_str(), 9600, 'N', 8, 1);
  if (ctx == nullptr)
  {
    this->stop();
    return false;
  }
  (void)modbus_set_slave(ctx, kUnitId);
  modbus_set_socket(ctx, this->master_fd_);
  {
    const uint32_t sec = 0;
    const uint32_t usec = 100000;
    (void)modbus_set_response_timeout(ctx, sec, usec);
    (void)modbus_set_byte_timeout(ctx, sec, usec);
  }

  modbus_mapping_t *mapping =
      modbus_mapping_new(kTableSize, kTableSize, kTableSize, kTableSize);
  if (mapping == nullptr)
  {
    modbus_free(ctx);
    this->stop();
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (int i = 0; i < kTableSize; ++i)
    {
      mapping->tab_registers[i] = 0;
      mapping->tab_input_registers[i] = 0;
      mapping->tab_bits[i] = 0;
      mapping->tab_input_bits[i] = 0;
    }
    mapping->tab_registers[kHoldingSpeed] = 42;
  }

  this->ctx_ = ctx;
  this->mapping_ = mapping;
  this->running_ = true;
  this->thread_ = std::thread([this]() { this->run(); });
  return true;
}

void ModbusRtuTestBus::stop()
{
  this->running_ = false;
  if (this->master_fd_ >= 0)
  {
    ::close(this->master_fd_);
    this->master_fd_ = -1;
  }
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }
  if (this->slave_fd_ >= 0)
  {
    ::close(this->slave_fd_);
    this->slave_fd_ = -1;
  }
  if (this->mapping_ != nullptr)
  {
    modbus_mapping_free(static_cast<modbus_mapping_t *>(this->mapping_));
    this->mapping_ = nullptr;
  }
  if (this->ctx_ != nullptr)
  {
    modbus_free(static_cast<modbus_t *>(this->ctx_));
    this->ctx_ = nullptr;
  }
  this->client_path_.clear();
}

std::string ModbusRtuTestBus::clientDevice() const
{
  return this->client_path_;
}

std::uint16_t ModbusRtuTestBus::holding(std::uint16_t address) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto *mapping = static_cast<modbus_mapping_t *>(this->mapping_);
  if (mapping == nullptr || address >= kTableSize)
  {
    return 0;
  }
  return mapping->tab_registers[address];
}

void ModbusRtuTestBus::setHolding(std::uint16_t address, std::uint16_t value)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  auto *mapping = static_cast<modbus_mapping_t *>(this->mapping_);
  if (mapping == nullptr || address >= kTableSize)
  {
    return;
  }
  mapping->tab_registers[address] = value;
}

void ModbusRtuTestBus::run()
{
  auto *ctx = static_cast<modbus_t *>(this->ctx_);
  auto *mapping = static_cast<modbus_mapping_t *>(this->mapping_);
  std::vector<uint8_t> query(MODBUS_RTU_MAX_ADU_LENGTH);

  while (this->running_)
  {
    int rc = modbus_receive(ctx, query.data());
    if (rc > 0)
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      (void)modbus_reply(ctx, query.data(), rc, mapping);
      continue;
    }
    // PTY master returns EIO until the client opens the slave; avoid busy-spin.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

}  // namespace test
}  // namespace virtual_factory
