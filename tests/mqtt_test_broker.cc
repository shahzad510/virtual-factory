#include "mqtt_test_broker.hh"

#include <MQTTClient.h>
#include <MQTTClientPersistence.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#ifndef VF_MOSQUITTO_EXECUTABLE
#define VF_MOSQUITTO_EXECUTABLE ""
#endif
#ifndef VF_MOSQUITTO_PASSWD
#define VF_MOSQUITTO_PASSWD ""
#endif
#ifndef VF_MOSQUITTO_LIBDIR
#define VF_MOSQUITTO_LIBDIR ""
#endif

namespace virtual_factory
{
namespace test
{

namespace
{

std::uint16_t pickFreePort()
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    ::close(fd);
    return 0;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
  {
    ::close(fd);
    return 0;
  }
  const std::uint16_t port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

bool tcpReady(const std::string &host, std::uint16_t port)
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
  {
    ::close(fd);
    return false;
  }
  const int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  ::close(fd);
  return rc == 0;
}

void applyLibraryPath()
{
  const char *dir = VF_MOSQUITTO_LIBDIR;
  if (dir == nullptr || dir[0] == '\0')
  {
    return;
  }
  const char *old = ::getenv("LD_LIBRARY_PATH");
  std::string value = dir;
  if (old != nullptr && old[0] != '\0')
  {
    value.push_back(':');
    value += old;
  }
  ::setenv("LD_LIBRARY_PATH", value.c_str(), 1);
}

int runChecked(const std::vector<std::string> &args, bool quietStderr = false)
{
  if (args.empty())
  {
    return -1;
  }
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &arg : args)
  {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t child = ::fork();
  if (child == 0)
  {
    applyLibraryPath();
    if (quietStderr)
    {
      const int devnull = ::open("/dev/null", O_WRONLY);
      if (devnull >= 0)
      {
        ::dup2(devnull, STDERR_FILENO);
        ::close(devnull);
      }
    }
    ::execvp(args[0].c_str(), argv.data());
    _exit(127);
  }
  if (child < 0)
  {
    return -1;
  }
  int status = 0;
  if (::waitpid(child, &status, 0) != child)
  {
    return -1;
  }
  if (WIFEXITED(status))
  {
    return WEXITSTATUS(status);
  }
  return -1;
}

}  // namespace

MqttTestClient::~MqttTestClient()
{
  this->disconnect();
}

bool MqttTestClient::connect(
    const std::string &host,
    std::uint16_t port,
    const std::string &clientId,
    bool useTls,
    bool tlsVerify,
    const std::string &username,
    const std::string &password)
{
  this->disconnect();
  this->client_id_ = clientId;
  this->username_ = username;
  this->password_ = password;
  this->server_uri_ = std::string(useTls ? "ssl://" : "tcp://") + host + ":" +
                      std::to_string(port);

  MQTTClient handle = nullptr;
  if (MQTTClient_create(
          &handle,
          this->server_uri_.c_str(),
          this->client_id_.c_str(),
          MQTTCLIENT_PERSISTENCE_NONE,
          nullptr) != MQTTCLIENT_SUCCESS)
  {
    return false;
  }
  this->handle_ = handle;

  MQTTClient_connectOptions options = MQTTClient_connectOptions_initializer;
  options.keepAliveInterval = 20;
  options.cleansession = 1;
  options.connectTimeout = 2;
  options.MQTTVersion = MQTTVERSION_3_1_1;
  if (!this->username_.empty())
  {
    options.username = this->username_.c_str();
  }
  if (!this->password_.empty())
  {
    options.password = this->password_.c_str();
  }
  MQTTClient_SSLOptions ssl = MQTTClient_SSLOptions_initializer;
  if (useTls)
  {
    ssl.enableServerCertAuth = tlsVerify ? 1 : 0;
    ssl.verify = tlsVerify ? 1 : 0;
    options.ssl = &ssl;
  }
  if (MQTTClient_connect(handle, &options) != MQTTCLIENT_SUCCESS)
  {
    this->disconnect();
    return false;
  }
  return true;
}

void MqttTestClient::disconnect()
{
  if (this->handle_ == nullptr)
  {
    return;
  }
  MQTTClient handle = static_cast<MQTTClient>(this->handle_);
  if (MQTTClient_isConnected(handle) != 0)
  {
    (void)MQTTClient_disconnect(handle, 500);
  }
  MQTTClient_destroy(&handle);
  this->handle_ = nullptr;
}

bool MqttTestClient::publish(
    const std::string &topic,
    const std::string &payload,
    int qos,
    bool retain)
{
  if (this->handle_ == nullptr)
  {
    return false;
  }
  MQTTClient_message message = MQTTClient_message_initializer;
  message.payload = const_cast<char *>(payload.data());
  message.payloadlen = static_cast<int>(payload.size());
  message.qos = qos;
  message.retained = retain ? 1 : 0;
  MQTTClient_deliveryToken token = 0;
  MQTTClient handle = static_cast<MQTTClient>(this->handle_);
  if (MQTTClient_publishMessage(handle, topic.c_str(), &message, &token) !=
      MQTTCLIENT_SUCCESS)
  {
    return false;
  }
  return MQTTClient_waitForCompletion(handle, token, 2000) == MQTTCLIENT_SUCCESS;
}

bool MqttTestClient::subscribe(const std::string &topic, int qos)
{
  if (this->handle_ == nullptr)
  {
    return false;
  }
  return MQTTClient_subscribe(
             static_cast<MQTTClient>(this->handle_), topic.c_str(), qos) ==
         MQTTCLIENT_SUCCESS;
}

bool MqttTestClient::waitMessage(
    std::string *topic, std::string *payload, int timeoutMs)
{
  if (this->handle_ == nullptr || topic == nullptr || payload == nullptr)
  {
    return false;
  }
  char *topicName = nullptr;
  int topicLen = 0;
  MQTTClient_message *message = nullptr;
  const int rc = MQTTClient_receive(
      static_cast<MQTTClient>(this->handle_),
      &topicName,
      &topicLen,
      &message,
      static_cast<unsigned long>(timeoutMs < 0 ? 0 : timeoutMs));
  if ((rc != MQTTCLIENT_SUCCESS && rc != MQTTCLIENT_TOPICNAME_TRUNCATED) ||
      message == nullptr)
  {
    if (topicName != nullptr)
    {
      MQTTClient_free(topicName);
    }
    if (message != nullptr)
    {
      MQTTClient_freeMessage(&message);
    }
    return false;
  }
  if (topicName != nullptr)
  {
    *topic = topicLen > 0
                 ? std::string(topicName, static_cast<std::size_t>(topicLen))
                 : std::string(topicName);
    MQTTClient_free(topicName);
  }
  if (message->payload != nullptr && message->payloadlen > 0)
  {
    payload->assign(
        static_cast<const char *>(message->payload),
        static_cast<std::size_t>(message->payloadlen));
  }
  else
  {
    payload->clear();
  }
  MQTTClient_freeMessage(&message);
  return true;
}

MqttTestBroker::MqttTestBroker(MqttTestBrokerOptions options)
    : options_(std::move(options))
{
}

MqttTestBroker::~MqttTestBroker()
{
  this->stop();
}

bool MqttTestBroker::start()
{
  this->stop();
  char tmpl[] = "/tmp/vf-mqtt-XXXXXX";
  if (::mkdtemp(tmpl) == nullptr)
  {
    this->last_error_ = "mkdtemp failed";
    return false;
  }
  this->dir_ = tmpl;
  this->config_path_ = this->dir_ + "/mosquitto.conf";
  if (this->port_ == 0)
  {
    this->port_ = pickFreePort();
  }
  if (this->port_ == 0)
  {
    this->last_error_ = "no free TCP port";
    return false;
  }
  if (this->options_.tls && !this->writeTlsFiles())
  {
    return false;
  }
  if (this->options_.requirePassword && !this->writePasswordFile())
  {
    return false;
  }
  if (!this->writeConfig(this->port_))
  {
    return false;
  }
  if (!this->spawnBroker())
  {
    return false;
  }
  if (!this->waitUntilListening())
  {
    this->last_error_ = "mosquitto did not accept TCP connections";
    this->stop();
    return false;
  }
  return true;
}

void MqttTestBroker::stop()
{
  if (this->pid_ > 0)
  {
    ::kill(this->pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 20; ++i)
    {
      const pid_t rc = ::waitpid(this->pid_, &status, WNOHANG);
      if (rc == this->pid_)
      {
        this->pid_ = -1;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (this->pid_ > 0)
    {
      ::kill(this->pid_, SIGKILL);
      ::waitpid(this->pid_, &status, 0);
      this->pid_ = -1;
    }
  }
  if (!this->dir_.empty())
  {
    ::unlink((this->dir_ + "/mosquitto.conf").c_str());
    ::unlink((this->dir_ + "/passwd").c_str());
    ::unlink((this->dir_ + "/cert.pem").c_str());
    ::unlink((this->dir_ + "/key.pem").c_str());
    ::rmdir(this->dir_.c_str());
    this->dir_.clear();
  }
}

std::string MqttTestBroker::host() const
{
  return this->host_;
}

std::uint16_t MqttTestBroker::port() const
{
  return this->port_;
}

bool MqttTestBroker::tls() const
{
  return this->options_.tls;
}

std::string MqttTestBroker::lastError() const
{
  return this->last_error_;
}

bool MqttTestBroker::writeConfig(std::uint16_t port)
{
  std::ofstream out(this->config_path_.c_str());
  if (!out)
  {
    this->last_error_ = "cannot write mosquitto.conf";
    return false;
  }
  out << "listener " << port << " 127.0.0.1\n";
  out << "persistence false\n";
  out << "log_dest none\n";
  if (const passwd *pw = ::getpwuid(::getuid()))
  {
    if (pw->pw_name != nullptr && pw->pw_name[0] != '\0')
    {
      out << "user " << pw->pw_name << "\n";
    }
  }
  if (this->options_.requirePassword)
  {
    out << "allow_anonymous false\n";
    out << "password_file " << this->dir_ << "/passwd\n";
  }
  else
  {
    out << "allow_anonymous true\n";
  }
  if (this->options_.tls)
  {
    out << "cafile " << this->dir_ << "/cert.pem\n";
    out << "certfile " << this->dir_ << "/cert.pem\n";
    out << "keyfile " << this->dir_ << "/key.pem\n";
    out << "require_certificate false\n";
  }
  return true;
}

bool MqttTestBroker::spawnBroker()
{
  const char *binary = VF_MOSQUITTO_EXECUTABLE;
  if (binary == nullptr || binary[0] == '\0')
  {
    this->last_error_ = "mosquitto executable not configured";
    return false;
  }
  const pid_t child = ::fork();
  if (child == 0)
  {
    applyLibraryPath();
    ::execl(binary, binary, "-c", this->config_path_.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  if (child < 0)
  {
    this->last_error_ = "fork mosquitto failed";
    return false;
  }
  this->pid_ = child;
  return true;
}

bool MqttTestBroker::waitUntilListening()
{
  for (int i = 0; i < 40; ++i)
  {
    int status = 0;
    const pid_t ended = ::waitpid(this->pid_, &status, WNOHANG);
    if (ended == this->pid_)
    {
      this->pid_ = -1;
      this->last_error_ = "mosquitto exited during startup";
      return false;
    }
    if (tcpReady(this->host_, this->port_))
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

bool MqttTestBroker::writePasswordFile()
{
  const char *passwd = VF_MOSQUITTO_PASSWD;
  if (passwd == nullptr || passwd[0] == '\0')
  {
    this->last_error_ = "mosquitto_passwd not configured";
    return false;
  }
  const std::string path = this->dir_ + "/passwd";
  const int rc = runChecked(
      {passwd, "-c", "-b", path, this->options_.username, this->options_.password});
  if (rc != 0)
  {
    this->last_error_ = "mosquitto_passwd failed";
    return false;
  }
  ::chmod(path.c_str(), 0600);
  return true;
}

bool MqttTestBroker::writeTlsFiles()
{
  const std::string cert = this->dir_ + "/cert.pem";
  const std::string key = this->dir_ + "/key.pem";
  const char *openssl = ::getenv("VF_OPENSSL");
  const std::string opensslBin =
      (openssl != nullptr && openssl[0] != '\0') ? openssl : "openssl";
  const int rc = runChecked({
      opensslBin,
      "req",
      "-x509",
      "-newkey",
      "rsa:2048",
      "-keyout",
      key,
      "-out",
      cert,
      "-days",
      "1",
      "-nodes",
      "-subj",
      "/CN=127.0.0.1",
  },
      true);
  if (rc != 0)
  {
    this->last_error_ = "openssl self-signed certificate generation failed";
    return false;
  }
  return true;
}

}  // namespace test
}  // namespace virtual_factory
