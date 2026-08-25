#include <virtual_factory/industrial/MqttIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "mqtt_session.hh"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace virtual_factory
{

namespace
{

std::mutex gClientIdMutex;
std::set<std::string> gClaimedClientKeys;
std::atomic<std::uint64_t> gClientSerial{1};

std::string clientKey(const std::string &host, std::uint16_t port, const std::string &clientId)
{
  return host + ":" + std::to_string(port) + "\n" + clientId;
}

std::string sanitizeClientFragment(const std::string &value)
{
  std::string out;
  out.reserve(value.size());
  for (char ch : value)
  {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    out.push_back(ok ? ch : '-');
  }
  if (out.empty())
  {
    return "adapter";
  }
  return out;
}

int clampQos(int qos)
{
  if (qos < 0)
  {
    return 0;
  }
  if (qos > 2)
  {
    return 2;
  }
  return qos;
}

bool topicHasWildcard(const std::string &topic)
{
  return topic.find('+') != std::string::npos ||
         topic.find('#') != std::string::npos;
}

std::string substituteValue(const std::string &bodyTemplate, double value)
{
  const std::string token = "{{value}}";
  std::ostringstream number;
  number << value;
  std::string out = bodyTemplate;
  std::string::size_type pos = 0;
  while ((pos = out.find(token, pos)) != std::string::npos)
  {
    out.replace(pos, token.size(), number.str());
    pos += number.str().size();
  }
  return out;
}

bool jsonAsDouble(const nlohmann::json &node, double *out)
{
  if (out == nullptr)
  {
    return false;
  }
  if (node.is_number())
  {
    *out = node.get<double>();
    return true;
  }
  if (node.is_boolean())
  {
    *out = node.get<bool>() ? 1.0 : 0.0;
    return true;
  }
  if (node.is_string())
  {
    try
    {
      *out = std::stod(node.get<std::string>());
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  return false;
}

bool jsonAsBool(const nlohmann::json &node, bool *out)
{
  if (out == nullptr)
  {
    return false;
  }
  if (node.is_boolean())
  {
    *out = node.get<bool>();
    return true;
  }
  if (node.is_number())
  {
    *out = node.get<double>() != 0.0;
    return true;
  }
  if (node.is_string())
  {
    const std::string text = node.get<std::string>();
    if (text == "true" || text == "running" || text == "1")
    {
      *out = true;
      return true;
    }
    if (text == "false" || text == "stopped" || text == "idle" || text == "0")
    {
      *out = false;
      return true;
    }
  }
  return false;
}

bool parseDoublePayload(
    const std::string &payload,
    MqttPayloadEncoding encoding,
    const std::string &jsonPointer,
    double *out)
{
  if (out == nullptr)
  {
    return false;
  }
  if (encoding == MqttPayloadEncoding::NumberText)
  {
    try
    {
      *out = std::stod(payload);
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  if (encoding == MqttPayloadEncoding::BooleanText)
  {
    bool flag = false;
    const nlohmann::json asString = payload;
    if (jsonAsBool(asString, &flag))
    {
      *out = flag ? 1.0 : 0.0;
      return true;
    }
    try
    {
      if (jsonAsBool(nlohmann::json::parse(payload), &flag))
      {
        *out = flag ? 1.0 : 0.0;
        return true;
      }
    }
    catch (...)
    {
    }
    return false;
  }

  try
  {
    const nlohmann::json doc = nlohmann::json::parse(payload);
    if (jsonPointer.empty())
    {
      return jsonAsDouble(doc, out);
    }
    const nlohmann::json node = doc.at(nlohmann::json::json_pointer(jsonPointer));
    return jsonAsDouble(node, out);
  }
  catch (...)
  {
    return false;
  }
}

bool parseBoolPayload(
    const std::string &payload,
    MqttPayloadEncoding encoding,
    const std::string &jsonPointer,
    bool *out)
{
  double value = 0.0;
  if (!parseDoublePayload(payload, encoding, jsonPointer, &value))
  {
    return false;
  }
  *out = value != 0.0;
  return true;
}

}  // namespace

struct MqttIndustrialAdapter::ClientHandle
{
  internal::MqttSession session;
};

class MqttIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      MqttIndustrialAdapter *adapter, const MqttEquipmentMapping *mapping)
      : adapter_(adapter), mapping_(mapping), inner_(mapping->id, mapping->type)
  {
    for (const auto &capability : mapping->capabilities)
    {
      this->inner_.addCapability(capability);
    }
    for (const auto &command : mapping->commands)
    {
      this->inner_.addCapability(command.command);
    }
  }

  std::string id() const override
  {
    return this->inner_.id();
  }

  std::string type() const override
  {
    return this->inner_.type();
  }

  OperationalState operationalState() const override
  {
    return this->inner_.operationalState();
  }

  bool fault() const override
  {
    return this->inner_.fault();
  }

  std::vector<std::string> capabilities() const override
  {
    return this->inner_.capabilities();
  }

  std::vector<std::string> commands() const override
  {
    return this->inner_.commands();
  }

  CommandResult execute(
      const std::string &command, double parameter) override
  {
    if (this->adapter_->connectionState() != ConnectionState::Connected)
    {
      if (this->adapter_->connectionState() == ConnectionState::Faulted)
      {
        return {false, "communication fault"};
      }
      return {false, "adapter not connected"};
    }

    const MqttCommandMapping *mapped = nullptr;
    for (const auto &entry : this->mapping_->commands)
    {
      if (entry.command == command)
      {
        mapped = &entry;
        break;
      }
    }
    if (mapped == nullptr)
    {
      return {false, "unknown command"};
    }
    if (mapped->topic.empty() || topicHasWildcard(mapped->topic))
    {
      return {false, "invalid command topic"};
    }
    if (!this->adapter_->publishCommand(*mapped, parameter))
    {
      return {false, "mqtt publish failed"};
    }
    return {true, "published"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  void applyMessage(const std::string &topic, const std::string &payload)
  {
    for (const auto &point : this->mapping_->telemetry)
    {
      if (point.topic != topic)
      {
        continue;
      }
      double value = 0.0;
      if (!parseDoublePayload(
              payload, point.encoding, point.jsonPointer, &value))
      {
        continue;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (this->mapping_->state.mapped && this->mapping_->state.topic == topic)
    {
      bool running = false;
      if (parseBoolPayload(
              payload,
              this->mapping_->state.encoding,
              this->mapping_->state.jsonPointer,
              &running))
      {
        this->inner_.setOperationalState(
            running ? OperationalState::Running : OperationalState::Stopped);
      }
    }

    if (this->mapping_->fault.mapped && this->mapping_->fault.topic == topic)
    {
      bool fault = false;
      if (parseBoolPayload(
              payload,
              this->mapping_->fault.encoding,
              this->mapping_->fault.jsonPointer,
              &fault))
      {
        this->inner_.setFault(fault);
      }
    }
  }

private:
  MqttIndustrialAdapter *adapter_;
  const MqttEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

MqttIndustrialAdapter::MqttIndustrialAdapter(
    std::string id, MqttAdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      client_(std::make_unique<ClientHandle>())
{
}

MqttIndustrialAdapter::~MqttIndustrialAdapter()
{
  this->disconnect();
}

std::string MqttIndustrialAdapter::id() const
{
  return this->id_;
}

std::string MqttIndustrialAdapter::protocol() const
{
  return "mqtt";
}

ConnectionState MqttIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string MqttIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

std::string MqttIndustrialAdapter::clientId() const
{
  return this->active_client_id_;
}

bool MqttIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.host.empty() || this->config_.port == 0)
  {
    this->enterFault("missing MQTT broker host or port");
    return false;
  }
  if (!this->validateMappedTopics())
  {
    return false;
  }

  const std::string clientId = this->resolveClientId();
  if (!this->claimClientId(clientId))
  {
    this->enterFault("MQTT client id already in use for this broker");
    return false;
  }

  internal::MqttSessionConfig sessionConfig;
  sessionConfig.host = this->config_.host;
  sessionConfig.port = this->config_.port;
  sessionConfig.clientId = clientId;
  sessionConfig.connectTimeoutSeconds =
      this->config_.timeoutMs > 0
          ? (this->config_.timeoutMs + 999) / 1000
          : 2;
  sessionConfig.keepaliveSeconds =
      this->config_.keepaliveSeconds > 0 ? this->config_.keepaliveSeconds : 20;
  sessionConfig.useTls = this->config_.useTls;
  sessionConfig.tlsVerify = this->config_.tlsVerify;
  sessionConfig.username = this->config_.username;
  sessionConfig.password = this->config_.password;

  this->client_->session.close();
  if (!this->client_->session.connect(sessionConfig))
  {
    this->enterFault(
        std::string("MQTT broker connect failed: ") +
        this->client_->session.lastError());
    return false;
  }

  const bool reuseEquipment =
      !this->bound_.empty() &&
      this->connection_state_ == ConnectionState::Faulted;
  if (!reuseEquipment)
  {
    this->bindEquipment();
  }
  if (!this->subscribeMappedTopics())
  {
    return false;
  }

  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void MqttIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  if (this->client_)
  {
    this->client_->session.close();
  }
  this->releaseClientId();
  this->active_client_id_.clear();
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> MqttIndustrialAdapter::equipment()
{
  std::vector<Equipment *> result;
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return result;
  }
  result.reserve(this->bound_.size());
  for (auto &item : this->bound_)
  {
    result.push_back(item.get());
  }
  return result;
}

Equipment *MqttIndustrialAdapter::equipmentById(const std::string &id)
{
  if (this->connection_state_ == ConnectionState::Disconnected)
  {
    return nullptr;
  }
  for (auto &item : this->bound_)
  {
    if (item->id() == id)
    {
      return item.get();
    }
  }
  return nullptr;
}

void MqttIndustrialAdapter::poll()
{
  if (this->connection_state_ != ConnectionState::Connected)
  {
    if (this->connection_state_ == ConnectionState::Faulted &&
        this->last_error_.empty())
    {
      this->last_error_ = "poll failed: communication fault";
    }
    return;
  }

  unsigned long timeout = 50;
  if (this->config_.pollTimeoutMs > 0)
  {
    timeout = static_cast<unsigned long>(this->config_.pollTimeoutMs);
  }
  else if (this->config_.pollTimeoutMs == 0)
  {
    timeout = 0;
  }

  constexpr int kMaxMessagesPerPoll = 512;
  bool first = true;
  for (int n = 0; n < kMaxMessagesPerPoll; ++n)
  {
    const internal::MqttIncoming incoming =
        this->client_->session.receive(first ? timeout : 0UL);
    first = false;
    if (incoming.error)
    {
      this->enterFault(
          this->client_->session.lastError().empty()
              ? "MQTT broker disconnected"
              : std::string("MQTT receive failed: ") +
                    this->client_->session.lastError());
      return;
    }
    if (!incoming.received)
    {
      if (!this->client_->session.connected())
      {
        this->enterFault("MQTT broker disconnected");
      }
      return;
    }
    this->applyMessage(incoming.topic, incoming.payload);
  }
}

void MqttIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void MqttIndustrialAdapter::enterFault(const std::string &reason)
{
  if (this->client_)
  {
    this->client_->session.close();
  }
  this->releaseClientId();
  this->active_client_id_.clear();
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
  if (!this->config_.password.empty())
  {
    std::string::size_type pos = 0;
    while ((pos = this->last_error_.find(this->config_.password, pos)) !=
           std::string::npos)
    {
      this->last_error_.replace(pos, this->config_.password.size(), "[redacted]");
      pos += 10;
    }
  }
}

bool MqttIndustrialAdapter::validateMappedTopics()
{
  auto invalid = [this](const std::string &topic, const char *what) {
    if (topic.empty())
    {
      return false;
    }
    if (topicHasWildcard(topic))
    {
      this->enterFault(std::string("MQTT ") + what +
                       " topic must not use wildcards");
      return true;
    }
    return false;
  };

  for (const auto &mapping : this->config_.equipment)
  {
    for (const auto &point : mapping.telemetry)
    {
      if (invalid(point.topic, "telemetry"))
      {
        return false;
      }
    }
    if (mapping.state.mapped && invalid(mapping.state.topic, "state"))
    {
      return false;
    }
    if (mapping.fault.mapped && invalid(mapping.fault.topic, "fault"))
    {
      return false;
    }
    for (const auto &command : mapping.commands)
    {
      if (invalid(command.topic, "command"))
      {
        return false;
      }
    }
  }
  return true;
}

bool MqttIndustrialAdapter::subscribeMappedTopics()
{
  struct Sub
  {
    std::string topic;
    int qos{1};
  };
  std::vector<Sub> subs;
  auto add = [&subs](const std::string &topic, int qos) {
    if (topic.empty())
    {
      return true;
    }
    if (topicHasWildcard(topic))
    {
      return false;
    }
    for (auto &entry : subs)
    {
      if (entry.topic == topic)
      {
        if (qos > entry.qos)
        {
          entry.qos = qos;
        }
        return true;
      }
    }
    subs.push_back({topic, clampQos(qos)});
    return true;
  };

  for (const auto &mapping : this->config_.equipment)
  {
    for (const auto &point : mapping.telemetry)
    {
      if (!add(point.topic, point.qos))
      {
        this->enterFault("MQTT telemetry topic must not use wildcards");
        return false;
      }
    }
    if (mapping.state.mapped && !add(mapping.state.topic, mapping.state.qos))
    {
      this->enterFault("MQTT state topic must not use wildcards");
      return false;
    }
    if (mapping.fault.mapped && !add(mapping.fault.topic, mapping.fault.qos))
    {
      this->enterFault("MQTT fault topic must not use wildcards");
      return false;
    }
  }

  for (const auto &sub : subs)
  {
    if (!this->client_->session.subscribe(sub.topic, sub.qos))
    {
      this->enterFault(
          std::string("MQTT subscribe failed: ") +
          this->client_->session.lastError());
      return false;
    }
  }
  return true;
}

bool MqttIndustrialAdapter::publishCommand(
    const MqttCommandMapping &command, double parameter)
{
  const std::string body = substituteValue(command.bodyTemplate, parameter);
  if (!this->client_->session.publish(
          command.topic, body, command.qos, command.retain))
  {
    this->enterFault(
        std::string("MQTT publish failed: ") +
        this->client_->session.lastError());
    return false;
  }
  return true;
}

void MqttIndustrialAdapter::applyMessage(
    const std::string &topic, const std::string &payload)
{
  for (auto &item : this->bound_)
  {
    item->applyMessage(topic, payload);
  }
}

std::string MqttIndustrialAdapter::resolveClientId() const
{
  if (!this->config_.clientId.empty())
  {
    return this->config_.clientId;
  }
  const std::uint64_t serial = gClientSerial.fetch_add(1);
  return std::string("vf.") + sanitizeClientFragment(this->id_) + "." +
         std::to_string(serial);
}

bool MqttIndustrialAdapter::claimClientId(const std::string &clientId)
{
  const std::string key =
      clientKey(this->config_.host, this->config_.port, clientId);
  std::lock_guard<std::mutex> lock(gClientIdMutex);
  if (gClaimedClientKeys.count(key) != 0)
  {
    return false;
  }
  gClaimedClientKeys.insert(key);
  this->active_client_id_ = clientId;
  return true;
}

void MqttIndustrialAdapter::releaseClientId()
{
  if (this->active_client_id_.empty())
  {
    return;
  }
  const std::string key = clientKey(
      this->config_.host, this->config_.port, this->active_client_id_);
  std::lock_guard<std::mutex> lock(gClientIdMutex);
  gClaimedClientKeys.erase(key);
}

}  // namespace virtual_factory
