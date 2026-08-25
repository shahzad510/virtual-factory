#ifndef VIRTUAL_FACTORY_MQTT_TEST_BROKER_HH_
#define VIRTUAL_FACTORY_MQTT_TEST_BROKER_HH_

#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace test
{

/// Local Mosquitto process used by mqtt_adapter_test.
///
/// DEVELOPMENT / INTEGRATION VALIDATION ONLY. Not production broker
/// certification, not cloud MQTT proof, and not linked into the
/// production adapter library.
struct MqttTestBrokerOptions
{
  bool requirePassword{false};
  std::string username;
  std::string password;
  bool tls{false};
};

class MqttTestClient
{
public:
  MqttTestClient() = default;
  ~MqttTestClient();

  MqttTestClient(const MqttTestClient &) = delete;
  MqttTestClient &operator=(const MqttTestClient &) = delete;

  bool connect(
      const std::string &host,
      std::uint16_t port,
      const std::string &clientId,
      bool useTls = false,
      bool tlsVerify = true,
      const std::string &username = "",
      const std::string &password = "");
  void disconnect();

  bool publish(
      const std::string &topic,
      const std::string &payload,
      int qos = 1,
      bool retain = false);
  bool subscribe(const std::string &topic, int qos = 1);
  bool waitMessage(
      std::string *topic, std::string *payload, int timeoutMs);

private:
  void *handle_{nullptr};
  std::string server_uri_;
  std::string client_id_;
  std::string username_;
  std::string password_;
};

class MqttTestBroker
{
public:
  explicit MqttTestBroker(MqttTestBrokerOptions options = {});
  ~MqttTestBroker();

  MqttTestBroker(const MqttTestBroker &) = delete;
  MqttTestBroker &operator=(const MqttTestBroker &) = delete;

  bool start();
  void stop();

  std::string host() const;
  std::uint16_t port() const;
  bool tls() const;
  std::string lastError() const;

private:
  bool writeConfig(std::uint16_t port);
  bool spawnBroker();
  bool waitUntilListening();
  bool writePasswordFile();
  bool writeTlsFiles();

  MqttTestBrokerOptions options_;
  std::string dir_;
  std::string config_path_;
  std::string host_{"127.0.0.1"};
  std::uint16_t port_{0};
  pid_t pid_{-1};
  std::string last_error_;
};

}  // namespace test
}  // namespace virtual_factory

#endif
