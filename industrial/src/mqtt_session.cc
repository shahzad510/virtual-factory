#include "mqtt_session.hh"

#include <MQTTAsync.h>
#include <MQTTClientPersistence.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>

namespace virtual_factory
{
namespace internal
{

namespace
{

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

}  // namespace

struct MqttSession::Impl
{
  MQTTAsync handle{nullptr};
  std::string server_uri;
  std::string client_id;
  std::string username;
  std::string password;
  std::string last_error;
  std::atomic<bool> closing{false};
  bool connected{false};
  bool lost{false};
  bool connect_done{false};
  int connect_rc{MQTTASYNC_FAILURE};
  bool op_done{false};
  int op_rc{MQTTASYNC_FAILURE};
  mutable std::mutex mu;
  std::condition_variable cv;
  std::deque<MqttIncoming> queue;

  static void onConnectionLost(void *context, char *cause)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr || self->closing.load())
    {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mu);
    self->connected = false;
    self->lost = true;
    if (cause != nullptr && self->last_error.empty())
    {
      self->last_error = std::string("MQTT connection lost: ") + cause;
    }
    else if (self->last_error.empty())
    {
      self->last_error = "MQTT connection lost";
    }
    self->cv.notify_all();
  }

  static int onMessage(
      void *context, char *topicName, int topicLen, MQTTAsync_message *message)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr || self->closing.load() || message == nullptr)
    {
      if (message != nullptr)
      {
        MQTTAsync_freeMessage(&message);
      }
      if (topicName != nullptr)
      {
        MQTTAsync_free(topicName);
      }
      return 1;
    }

    MqttIncoming incoming;
    incoming.received = true;
    if (topicName != nullptr)
    {
      if (topicLen > 0)
      {
        incoming.topic.assign(topicName, static_cast<std::size_t>(topicLen));
      }
      else
      {
        incoming.topic = topicName;
      }
    }
    if (message->payload != nullptr && message->payloadlen > 0)
    {
      incoming.payload.assign(
          static_cast<const char *>(message->payload),
          static_cast<std::size_t>(message->payloadlen));
    }

    {
      std::lock_guard<std::mutex> lock(self->mu);
      // Bounded queue. When full, drop oldest so newer publishes win
      // (latest-value telemetry). Do not grow without bound.
      constexpr std::size_t kMaxQueued = 4096;
      if (self->queue.size() >= kMaxQueued)
      {
        self->queue.pop_front();
      }
      self->queue.push_back(std::move(incoming));
      self->cv.notify_all();
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  static void onConnect(void *context, MQTTAsync_successData * /*response*/)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mu);
    self->connect_rc = MQTTASYNC_SUCCESS;
    self->connect_done = true;
    self->connected = true;
    self->lost = false;
    self->cv.notify_all();
  }

  static void onConnectFailure(void *context, MQTTAsync_failureData *response)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mu);
    self->connect_rc =
        response != nullptr ? response->code : MQTTASYNC_FAILURE;
    if (response != nullptr && response->message != nullptr)
    {
      self->last_error =
          std::string("MQTT connect failed: ") + response->message;
    }
    else
    {
      self->last_error = "MQTT connect failed";
    }
    self->connect_done = true;
    self->connected = false;
    self->cv.notify_all();
  }

  static void onOpSuccess(void *context, MQTTAsync_successData * /*response*/)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mu);
    self->op_rc = MQTTASYNC_SUCCESS;
    self->op_done = true;
    self->cv.notify_all();
  }

  static void onOpFailure(void *context, MQTTAsync_failureData *response)
  {
    auto *self = static_cast<Impl *>(context);
    if (self == nullptr)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(self->mu);
    self->op_rc = response != nullptr ? response->code : MQTTASYNC_FAILURE;
    if (response != nullptr && response->message != nullptr)
    {
      self->last_error = std::string("MQTT operation failed: ") + response->message;
    }
    else
    {
      self->last_error = "MQTT operation failed";
    }
    self->op_done = true;
    self->cv.notify_all();
  }
};

MqttSession::MqttSession() : impl_(std::make_unique<Impl>())
{
}

MqttSession::~MqttSession()
{
  this->close();
}

bool MqttSession::connect(const MqttSessionConfig &config)
{
  this->close();
  impl_->last_error.clear();
  impl_->closing.store(false);
  impl_->lost = false;
  impl_->connected = false;

  if (config.host.empty() || config.port == 0 || config.clientId.empty())
  {
    impl_->last_error = "missing MQTT host, port, or client id";
    return false;
  }

  impl_->client_id = config.clientId;
  impl_->username = config.username;
  impl_->password = config.password;

  std::ostringstream uri;
  uri << (config.useTls ? "ssl://" : "tcp://") << config.host << ":"
      << config.port;
  impl_->server_uri = uri.str();

  MQTTAsync_createOptions createOpts = MQTTAsync_createOptions_initializer;
  createOpts.MQTTVersion = MQTTVERSION_3_1_1;
  MQTTAsync handle = nullptr;
  const int createRc = MQTTAsync_createWithOptions(
      &handle,
      impl_->server_uri.c_str(),
      impl_->client_id.c_str(),
      MQTTCLIENT_PERSISTENCE_NONE,
      nullptr,
      &createOpts);
  if (createRc != MQTTASYNC_SUCCESS || handle == nullptr)
  {
    impl_->last_error = "MQTT create failed";
    return false;
  }
  impl_->handle = handle;

  if (MQTTAsync_setCallbacks(
          handle, impl_.get(), &Impl::onConnectionLost, &Impl::onMessage,
          nullptr) != MQTTASYNC_SUCCESS)
  {
    impl_->last_error = "MQTT setCallbacks failed";
    MQTTAsync_destroy(&impl_->handle);
    impl_->handle = nullptr;
    return false;
  }

  MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
  options.keepAliveInterval =
      config.keepaliveSeconds > 0 ? config.keepaliveSeconds : 20;
  options.cleansession = 1;
  options.connectTimeout =
      config.connectTimeoutSeconds > 0 ? config.connectTimeoutSeconds : 1;
  options.MQTTVersion = MQTTVERSION_3_1_1;
  options.automaticReconnect = 0;
  options.context = impl_.get();
  options.onSuccess = &Impl::onConnect;
  options.onFailure = &Impl::onConnectFailure;
  if (!impl_->username.empty())
  {
    options.username = impl_->username.c_str();
  }
  if (!impl_->password.empty())
  {
    options.password = impl_->password.c_str();
  }

  MQTTAsync_SSLOptions ssl = MQTTAsync_SSLOptions_initializer;
  if (config.useTls)
  {
    ssl.enableServerCertAuth = config.tlsVerify ? 1 : 0;
    ssl.verify = config.tlsVerify ? 1 : 0;
    options.ssl = &ssl;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->connect_done = false;
    impl_->connect_rc = MQTTASYNC_FAILURE;
  }

  const int connectRc = MQTTAsync_connect(handle, &options);
  if (connectRc != MQTTASYNC_SUCCESS)
  {
    impl_->last_error = "MQTT connect request failed";
    this->close();
    return false;
  }

  const int waitSeconds =
      (config.connectTimeoutSeconds > 0 ? config.connectTimeoutSeconds : 2) + 3;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(waitSeconds);
  std::unique_lock<std::mutex> lock(impl_->mu);
  impl_->cv.wait_until(lock, deadline, [this] { return impl_->connect_done; });
  if (!impl_->connect_done || impl_->connect_rc != MQTTASYNC_SUCCESS)
  {
    lock.unlock();
    if (impl_->last_error.empty())
    {
      impl_->last_error = "MQTT connect timed out";
    }
    this->close();
    return false;
  }
  return true;
}

void MqttSession::close()
{
  impl_->closing.store(true);
  if (impl_->handle != nullptr)
  {
    if (MQTTAsync_isConnected(impl_->handle))
    {
      MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
      opts.timeout = 1;
      (void)MQTTAsync_disconnect(impl_->handle, &opts);
    }
    MQTTAsync_destroy(&impl_->handle);
    impl_->handle = nullptr;
  }
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->connected = false;
  impl_->lost = false;
  impl_->queue.clear();
  impl_->closing.store(false);
}

bool MqttSession::connected() const
{
  if (impl_->handle == nullptr)
  {
    return false;
  }
  const bool pahoConnected = MQTTAsync_isConnected(impl_->handle) != 0;
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->connected && !impl_->lost && pahoConnected;
}

bool MqttSession::subscribe(const std::string &topic, int qos)
{
  if (impl_->handle == nullptr || topic.empty())
  {
    impl_->last_error = "MQTT subscribe failed: no session or topic";
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->op_done = false;
    impl_->op_rc = MQTTASYNC_FAILURE;
  }

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  opts.context = impl_.get();
  opts.onSuccess = &Impl::onOpSuccess;
  opts.onFailure = &Impl::onOpFailure;
  const int rc = MQTTAsync_subscribe(
      impl_->handle, topic.c_str(), clampQos(qos), &opts);
  if (rc != MQTTASYNC_SUCCESS)
  {
    impl_->last_error = "MQTT subscribe request failed";
    return false;
  }

  std::unique_lock<std::mutex> lock(impl_->mu);
  impl_->cv.wait_for(lock, std::chrono::seconds(2), [this] {
    return impl_->op_done;
  });
  if (!impl_->op_done || impl_->op_rc != MQTTASYNC_SUCCESS)
  {
    if (impl_->last_error.empty())
    {
      impl_->last_error = "MQTT subscribe timed out";
    }
    return false;
  }
  return true;
}

bool MqttSession::publish(
    const std::string &topic,
    const std::string &payload,
    int qos,
    bool retain)
{
  if (impl_->handle == nullptr || topic.empty())
  {
    impl_->last_error = "MQTT publish failed: no session or topic";
    return false;
  }

  MQTTAsync_message message = MQTTAsync_message_initializer;
  message.payload = const_cast<char *>(payload.data());
  message.payloadlen = static_cast<int>(payload.size());
  message.qos = clampQos(qos);
  message.retained = retain ? 1 : 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  const int rc = MQTTAsync_sendMessage(
      impl_->handle, topic.c_str(), &message, &opts);
  if (rc != MQTTASYNC_SUCCESS)
  {
    impl_->last_error = "MQTT publish request failed";
    return false;
  }
  const int waitRc =
      MQTTAsync_waitForCompletion(impl_->handle, opts.token, 2000);
  if (waitRc != MQTTASYNC_SUCCESS)
  {
    impl_->last_error = "MQTT publish wait failed";
    return false;
  }
  return true;
}

MqttIncoming MqttSession::receive(unsigned long timeoutMs)
{
  MqttIncoming incoming;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->lost)
    {
      incoming.error = true;
      if (impl_->last_error.empty())
      {
        impl_->last_error = "MQTT broker disconnected";
      }
      return incoming;
    }
  }
  if (impl_->handle != nullptr && MQTTAsync_isConnected(impl_->handle) == 0)
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->connected || impl_->lost)
    {
      incoming.error = true;
      if (impl_->last_error.empty())
      {
        impl_->last_error = "MQTT broker disconnected";
      }
      impl_->lost = true;
      impl_->connected = false;
      return incoming;
    }
  }

  std::unique_lock<std::mutex> lock(impl_->mu);
  auto hasWork = [this] { return impl_->lost || !impl_->queue.empty(); };
  impl_->cv.wait_for(
      lock, std::chrono::milliseconds(timeoutMs), hasWork);

  if (impl_->lost)
  {
    incoming.error = true;
    if (impl_->last_error.empty())
    {
      impl_->last_error = "MQTT broker disconnected";
    }
    return incoming;
  }
  if (impl_->queue.empty())
  {
    return incoming;
  }
  incoming = impl_->queue.front();
  impl_->queue.pop_front();
  return incoming;
}

std::string MqttSession::lastError() const
{
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->last_error;
}

}  // namespace internal
}  // namespace virtual_factory
