#ifndef VIRTUAL_FACTORY_OPCUA_SCALE_PLC_SERVER_HH_
#define VIRTUAL_FACTORY_OPCUA_SCALE_PLC_SERVER_HH_

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

struct UA_Server;

namespace virtual_factory
{
namespace test
{

/// One simulated PLC OPC UA server for the multi-server scalability test.
///
/// TEST / DEVELOPMENT ONLY. Localhost, SecurityPolicy#None, anonymous.
/// Not a production architecture component. Distinct from OpcUaTestServer
/// so the existing mixer/pump unit fixture is unchanged.
class OpcUaScalePlcServer
{
public:
  explicit OpcUaScalePlcServer(int index);
  ~OpcUaScalePlcServer();

  OpcUaScalePlcServer(const OpcUaScalePlcServer &) = delete;
  OpcUaScalePlcServer &operator=(const OpcUaScalePlcServer &) = delete;

  bool start();
  void stop();

  int index() const;
  std::uint16_t port() const;
  std::string endpointUrl() const;
  std::string equipmentId() const;

  std::string runningNodeId() const;
  std::string faultNodeId() const;
  std::string temperatureNodeId() const;
  std::string pressureNodeId() const;
  std::string speedNodeId() const;
  std::string startNodeId() const;
  std::string stopNodeId() const;
  std::string speedSetpointNodeId() const;

  double expectedTemperature() const;
  double expectedPressure() const;
  double expectedSpeed() const;

  bool startNode() const;
  bool stopNode() const;
  double speedSetpoint() const;
  double temperature() const;

private:
  bool addBooleanNode(const std::string &identifier, bool initial);
  bool addDoubleNode(const std::string &identifier, double initial);
  bool readBoolean(const std::string &identifier) const;
  double readDouble(const std::string &identifier) const;
  bool addNodes();

  int index_{0};
  std::string equipment_id_;
  UA_Server *server_{nullptr};
  std::uint16_t port_{0};
  std::atomic<bool> iterate_{false};
  std::thread thread_;
};

}  // namespace test
}  // namespace virtual_factory

#endif
