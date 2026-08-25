#ifndef VIRTUAL_FACTORY_MQTT_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_MQTT_INDUSTRIAL_ADAPTER_HH_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// How an MQTT payload is interpreted. Mapping-driven; not a machine type.
enum class MqttPayloadEncoding
{
  /// RFC 6901 pointer into a JSON document.
  JsonPointer,
  /// Entire payload is a decimal number.
  NumberText,
  /// true/false, 1/0, running/stopped, or JSON boolean.
  BooleanText
};

/// Named command → MQTT publish. QoS default 1; retain default false.
struct MqttCommandMapping
{
  std::string command;
  std::string topic;
  std::string bodyTemplate;
  int qos{1};
  bool retain{false};
};

/// Named telemetry point from a subscribed topic.
struct MqttTelemetryMapping
{
  std::string name;
  std::string topic;
  std::string unit;
  MqttPayloadEncoding encoding{MqttPayloadEncoding::JsonPointer};
  std::string jsonPointer;
  int qos{1};
};

/// Optional state or fault subscription.
struct MqttSignalMapping
{
  std::string topic;
  MqttPayloadEncoding encoding{MqttPayloadEncoding::BooleanText};
  std::string jsonPointer;
  int qos{1};
  bool mapped{false};
};

inline MqttSignalMapping makeMqttSignal(
    std::string topic,
    MqttPayloadEncoding encoding = MqttPayloadEncoding::BooleanText,
    std::string jsonPointer = "",
    int qos = 1)
{
  MqttSignalMapping signal;
  signal.topic = std::move(topic);
  signal.encoding = encoding;
  signal.jsonPointer = std::move(jsonPointer);
  signal.qos = qos;
  signal.mapped = true;
  return signal;
}

/// One machine as seen through MQTT. Type is metadata, not a C++ class.
/// Instance ids such as PLC-001 belong here, not in a PLC001.hh class.
struct MqttEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<MqttCommandMapping> commands;
  std::vector<MqttTelemetryMapping> telemetry;
  MqttSignalMapping state;
  MqttSignalMapping fault;
};

/// Config for one MQTT broker session (one adapter instance).
/// One instance = one broker. N brokers ⇒ N instances (ADR-038).
///
/// Unlike OPC UA / Modbus / REST (one adapter per endpoint/origin), MQTT is
/// broker-oriented: many logical machines share one broker session via the
/// equipment vector. Phase 7 onboarding may add/remove brokers or move
/// equipment between broker configs without new C++ classes or a Phase 6
/// adapter manager — those lists are configuration data only.
struct MqttAdapterConfig
{
  std::string host{"127.0.0.1"};
  std::uint16_t port{1883};
  /// Unique among simultaneously connected clients to this broker in this
  /// process. Different brokers may reuse the same id. If empty, connect()
  /// generates a unique id (never from secrets).
  std::string clientId;
  /// CONNECT wait, converted to whole seconds (minimum 1).
  int timeoutMs{2000};
  /// Bounded wait inside poll() for queued MQTT publishes. Must stay finite.
  /// Default 50 ms. Paho's internal thread services keepalive.
  int pollTimeoutMs{50};
  int keepaliveSeconds{20};
  bool useTls{false};
  /// TLS certificate verification. Must stay true for production TLS.
  /// Set false only as an explicit development/testing opt-in.
  bool tlsVerify{true};
  std::string username;
  std::string password;
  std::vector<MqttEquipmentMapping> equipment;
};

/// Production MQTT industrial adapter (SoT Phase 6 slice 6F).
///
/// MQTT client to one broker (Paho MQTT C MQTTAsync, MQTT 3.1.1). Not a
/// broker. Not an MES event bus. Translates configured topics into
/// GenericEquipment. Does not expose Paho types, topics, or credentials
/// through Equipment.
///
/// Protocol model: OPC UA / Modbus / REST use one adapter per endpoint or
/// HTTP origin. MQTT uses one adapter per broker/session; many PLCs on that
/// broker are GenericEquipment mappings (ids like PLC-001 are configuration
/// identities, not C++ types). N brokers ⇒ N adapter instances.
///
/// poll() waits up to pollTimeoutMs (default 50 ms) for queued publishes
/// and must not block indefinitely. Paho's internal network thread services
/// keepalive. connect() after Faulted recreates the client and resubscribes.
/// Background auto-reconnect is not implemented.
class MqttIndustrialAdapter : public IndustrialAdapter
{
public:
  MqttIndustrialAdapter(std::string id, MqttAdapterConfig config);
  ~MqttIndustrialAdapter() override;

  MqttIndustrialAdapter(const MqttIndustrialAdapter &) = delete;
  MqttIndustrialAdapter &operator=(const MqttIndustrialAdapter &) = delete;

  std::string id() const override;
  std::string protocol() const override;
  ConnectionState connectionState() const override;
  std::string lastError() const override;

  bool connect() override;
  void disconnect() override;

  std::vector<Equipment *> equipment() override;
  Equipment *equipmentById(const std::string &id) override;

  void poll() override;

  /// Effective MQTT client identifier after connect() (generated or configured).
  std::string clientId() const;

private:
  class BoundEquipment;
  friend class BoundEquipment;

  struct ClientHandle;

  void bindEquipment();
  void enterFault(const std::string &reason);
  bool validateMappedTopics();
  bool subscribeMappedTopics();
  bool publishCommand(
      const MqttCommandMapping &command, double parameter);
  void applyMessage(const std::string &topic, const std::string &payload);
  std::string resolveClientId() const;
  bool claimClientId(const std::string &clientId);
  void releaseClientId();

  std::string id_;
  MqttAdapterConfig config_;
  std::string active_client_id_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<ClientHandle> client_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
};

}  // namespace virtual_factory

#endif
