#ifndef VIRTUAL_FACTORY_MODBUS_RTU_TEST_BUS_HH_
#define VIRTUAL_FACTORY_MODBUS_RTU_TEST_BUS_HH_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace virtual_factory
{
namespace test
{

/// In-process Modbus RTU slave on a PTY pair for adapter tests (libmodbus).
/// DEVELOPMENT/TEST ONLY — not real RS-485 hardware validation.
class ModbusRtuTestBus
{
public:
  static constexpr std::uint8_t kUnitId = 1;
  static constexpr std::uint16_t kHoldingSpeed = 1;

  ModbusRtuTestBus();
  ~ModbusRtuTestBus();

  ModbusRtuTestBus(const ModbusRtuTestBus &) = delete;
  ModbusRtuTestBus &operator=(const ModbusRtuTestBus &) = delete;

  bool start();
  void stop();

  /// Client-side PTY path to pass to ModbusIndustrialAdapter (serialDevice).
  std::string clientDevice() const;

  int baudRate() const
  {
    return 9600;
  }

  std::uint16_t holding(std::uint16_t address) const;
  void setHolding(std::uint16_t address, std::uint16_t value);

private:
  void run();

  static constexpr int kTableSize = 64;

  int master_fd_{-1};
  int slave_fd_{-1};
  std::string client_path_;
  void *ctx_{nullptr};
  void *mapping_{nullptr};
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
};

}  // namespace test
}  // namespace virtual_factory

#endif
