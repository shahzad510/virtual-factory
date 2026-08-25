#ifndef VIRTUAL_FACTORY_MQTT_SESSION_HH_
#define VIRTUAL_FACTORY_MQTT_SESSION_HH_

// Private industrial-layer helper. Not part of the public adapter API.
// Not included from Equipment or IndustrialAdapter headers.
// Implementation uses Eclipse Paho MQTT C (MQTTAsync); this header does
// not include <MQTTAsync.h>.

#include <cstdint>
#include <memory>
#include <string>

namespace virtual_factory
{
namespace internal
{

struct MqttSessionConfig
{
  std::string host{"127.0.0.1"};
  std::uint16_t port{1883};
  std::string clientId;
  int connectTimeoutSeconds{2};
  int keepaliveSeconds{20};
  bool useTls{false};
  bool tlsVerify{true};
  std::string username;
  std::string password;
};

struct MqttIncoming
{
  bool received{false};
  bool error{false};
  std::string topic;
  std::string payload;
};

/// MQTT 3.1.1 client session via Paho MQTTAsync.
/// poll() waits on a bounded queue; Paho's internal thread services
/// keepalive. Automatic reconnect is disabled.
class MqttSession
{
public:
  MqttSession();
  ~MqttSession();

  MqttSession(const MqttSession &) = delete;
  MqttSession &operator=(const MqttSession &) = delete;

  bool connect(const MqttSessionConfig &config);
  void close();
  bool connected() const;

  bool subscribe(const std::string &topic, int qos);
  bool publish(
      const std::string &topic,
      const std::string &payload,
      int qos,
      bool retain);

  /// Bounded wait for one queued publish. timeoutMs 0 is non-blocking.
  MqttIncoming receive(unsigned long timeoutMs);

  std::string lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace internal
}  // namespace virtual_factory

#endif
