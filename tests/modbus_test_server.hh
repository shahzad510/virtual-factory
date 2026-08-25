#ifndef VIRTUAL_FACTORY_MODBUS_TEST_SERVER_HH_
#define VIRTUAL_FACTORY_MODBUS_TEST_SERVER_HH_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace virtual_factory
{
namespace test
{

/// In-process Modbus TCP slave for adapter tests (libmodbus).
///
/// DEVELOPMENT/TEST ONLY: localhost, no authentication, no TLS.
/// Machine names (mixer, pump, unknown) are register-map labels only.
class ModbusTestServer
{
public:
  static constexpr std::uint8_t kUnitId = 1;

  static constexpr std::uint16_t kMixerStart = 0;
  static constexpr std::uint16_t kMixerStop = 1;
  static constexpr std::uint16_t kMixerRunning = 2;
  static constexpr std::uint16_t kMixerFault = 3;
  static constexpr std::uint16_t kMixerSpeedSetpoint = 0;
  static constexpr std::uint16_t kMixerSpeedActual = 1;
  static constexpr std::uint16_t kMixerTemperature = 0;

  static constexpr std::uint16_t kPumpStart = 10;
  static constexpr std::uint16_t kPumpStop = 11;
  static constexpr std::uint16_t kPumpRunning = 12;
  static constexpr std::uint16_t kPumpFault = 13;
  static constexpr std::uint16_t kPumpFlow = 10;
  static constexpr std::uint16_t kPumpPressure = 11;

  static constexpr std::uint16_t kUnknownStart = 20;
  static constexpr std::uint16_t kUnknownStop = 21;
  static constexpr std::uint16_t kUnknownRunning = 22;
  static constexpr std::uint16_t kUnknownFault = 23;
  static constexpr std::uint16_t kUnknownTemperature = 20;

  ModbusTestServer();
  ~ModbusTestServer();

  ModbusTestServer(const ModbusTestServer &) = delete;
  ModbusTestServer &operator=(const ModbusTestServer &) = delete;

  bool start();
  void stop();

  std::string host() const;
  std::uint16_t port() const;

  bool coil(std::uint16_t address) const;
  void setCoil(std::uint16_t address, bool value);
  std::uint16_t holding(std::uint16_t address) const;
  void setHolding(std::uint16_t address, std::uint16_t value);
  std::uint16_t inputRegister(std::uint16_t address) const;
  void setInputRegister(std::uint16_t address, std::uint16_t value);
  bool discreteInput(std::uint16_t address) const;
  void setDiscreteInput(std::uint16_t address, bool value);

private:
  void run();
  void handleClient(int clientFd);
  void closeListen();
  void resetTablesLocked();

  static constexpr int kTableSize = 64;

  void *ctx_{nullptr};
  void *mapping_{nullptr};
  int listen_fd_{-1};
  std::atomic<int> client_fd_{-1};
  std::uint16_t port_{0};
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
};

}  // namespace test
}  // namespace virtual_factory

#endif
