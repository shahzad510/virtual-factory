#ifndef VIRTUAL_FACTORY_OPCUA_TEST_SERVER_HH_
#define VIRTUAL_FACTORY_OPCUA_TEST_SERVER_HH_

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

struct UA_Server;

namespace virtual_factory
{
namespace test
{

/// In-process OPC UA server for adapter tests.
///
/// DEVELOPMENT ONLY: localhost, SecurityPolicy#None, anonymous access.
/// This is not production industrial security. No certificates are used.
///
/// Address space is a flat set of string NodeIds (ns=1;s=…). Machines are
/// TEST DEVICES only. The adapter must not grow a C++ class per machine.
class OpcUaTestServer
{
public:
  static constexpr const char *kMixerId = "TEST-MIXER-001";
  static constexpr const char *kMixerRunning = "TEST-MIXER-001.Running";
  static constexpr const char *kMixerFault = "TEST-MIXER-001.Fault";
  static constexpr const char *kMixerSpeedSetpoint = "TEST-MIXER-001.SpeedSetpoint";
  static constexpr const char *kMixerSpeedActual = "TEST-MIXER-001.SpeedActual";
  static constexpr const char *kMixerTemperature = "TEST-MIXER-001.Temperature";
  static constexpr const char *kMixerStart = "TEST-MIXER-001.Start";
  static constexpr const char *kMixerStop = "TEST-MIXER-001.Stop";

  static constexpr const char *kPumpId = "Pump_01";
  static constexpr const char *kPumpRunning = "Pump_01.Running";
  static constexpr const char *kPumpFault = "Pump_01.Fault";
  static constexpr const char *kPumpFlowRate = "Pump_01.FlowRate";
  static constexpr const char *kPumpPressure = "Pump_01.Pressure";
  static constexpr const char *kPumpStart = "Pump_01.Start";
  static constexpr const char *kPumpStop = "Pump_01.Stop";

  static constexpr const char *kUnknownId = "UnknownMachine_01";
  static constexpr const char *kUnknownRunning = "UnknownMachine_01.Running";
  static constexpr const char *kUnknownFault = "UnknownMachine_01.Fault";
  static constexpr const char *kUnknownTemperature = "UnknownMachine_01.Temperature";
  static constexpr const char *kUnknownStart = "UnknownMachine_01.Start";
  static constexpr const char *kUnknownStop = "UnknownMachine_01.Stop";

  /// Legacy aliases used by mixer-focused assertions.
  static constexpr const char *kEquipmentId = kMixerId;
  static constexpr const char *kRunning = kMixerRunning;
  static constexpr const char *kFault = kMixerFault;
  static constexpr const char *kSpeedSetpoint = kMixerSpeedSetpoint;
  static constexpr const char *kSpeedActual = kMixerSpeedActual;
  static constexpr const char *kTemperature = kMixerTemperature;
  static constexpr const char *kStart = kMixerStart;
  static constexpr const char *kStop = kMixerStop;

  OpcUaTestServer();
  ~OpcUaTestServer();

  OpcUaTestServer(const OpcUaTestServer &) = delete;
  OpcUaTestServer &operator=(const OpcUaTestServer &) = delete;

  bool start();
  void stop();

  std::uint16_t port() const;
  std::string endpointUrl() const;

  bool booleanNode(const char *identifier) const;
  double doubleNode(const char *identifier) const;
  void setBooleanNode(const char *identifier, bool value);
  void setDoubleNode(const char *identifier, double value);

  bool runningNode() const;
  bool faultNode() const;
  bool startNode() const;
  bool stopNode() const;
  double speedSetpoint() const;
  double speedActual() const;
  double temperature() const;

  void setRunning(bool value);
  void setFault(bool value);
  void setSpeedActual(double value);
  void setTemperature(double value);

private:
  bool addBooleanNode(const char *identifier, bool initial);
  bool addDoubleNode(const char *identifier, double initial);
  bool readBoolean(const char *identifier) const;
  double readDouble(const char *identifier) const;
  void writeBoolean(const char *identifier, bool value);
  void writeDouble(const char *identifier, double value);
  bool addMixerNodes();
  bool addPumpNodes();
  bool addUnknownMachineNodes();

  UA_Server *server_{nullptr};
  std::uint16_t port_{0};
  std::atomic<bool> iterate_{false};
  std::thread thread_;
};

}  // namespace test
}  // namespace virtual_factory

#endif
