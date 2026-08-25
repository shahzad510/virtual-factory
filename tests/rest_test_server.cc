#include "rest_test_server.hh"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace virtual_factory
{
namespace test
{

namespace
{

std::string toLower(std::string value)
{
  for (char &ch : value)
  {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string jsonBool(bool value)
{
  return value ? "true" : "false";
}

bool extractJsonNumber(
    const std::string &body, const char *key, double *out)
{
  if (out == nullptr || key == nullptr)
  {
    return false;
  }
  const std::string needle = std::string("\"") + key + "\"";
  auto pos = body.find(needle);
  if (pos == std::string::npos)
  {
    return false;
  }
  pos = body.find(':', pos + needle.size());
  if (pos == std::string::npos)
  {
    return false;
  }
  ++pos;
  while (pos < body.size() &&
         (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\n' ||
          body[pos] == '\r'))
  {
    ++pos;
  }
  try
  {
    std::size_t consumed = 0;
    *out = std::stod(body.substr(pos), &consumed);
    return consumed > 0;
  }
  catch (...)
  {
    return false;
  }
}

bool readAtLeast(int fd, std::string *buffer, std::size_t need, int timeoutMs)
{
  while (buffer->size() < need)
  {
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int prc = ::poll(&pfd, 1, timeoutMs);
    if (prc <= 0 || (pfd.revents & POLLIN) == 0)
    {
      return false;
    }
    char chunk[2048];
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0)
    {
      return false;
    }
    buffer->append(chunk, static_cast<std::size_t>(n));
    if (buffer->size() > 65536)
    {
      return false;
    }
  }
  return true;
}

std::string headerValue(const std::string &headers, const char *name)
{
  const std::string lowerHeaders = toLower(headers);
  const std::string needle = std::string("\n") + toLower(name) + ":";
  auto pos = lowerHeaders.find(needle);
  if (pos == std::string::npos)
  {
    const std::string first = toLower(name) + ":";
    if (lowerHeaders.compare(0, first.size(), first) != 0)
    {
      return "";
    }
    pos = 0;
    const auto colon = headers.find(':');
    if (colon == std::string::npos)
    {
      return "";
    }
    auto start = colon + 1;
    while (start < headers.size() &&
           (headers[start] == ' ' || headers[start] == '\t'))
    {
      ++start;
    }
    auto end = headers.find('\r', start);
    if (end == std::string::npos)
    {
      end = headers.find('\n', start);
    }
    return headers.substr(start, end - start);
  }

  const auto colon = headers.find(':', pos);
  if (colon == std::string::npos)
  {
    return "";
  }
  auto start = colon + 1;
  while (start < headers.size() &&
         (headers[start] == ' ' || headers[start] == '\t'))
  {
    ++start;
  }
  auto end = headers.find('\r', start);
  if (end == std::string::npos)
  {
    end = headers.find('\n', start);
  }
  return headers.substr(start, end - start);
}

std::string httpResponse(int status, const std::string &body)
{
  const char *reason = "OK";
  if (status == 401)
  {
    reason = "Unauthorized";
  }
  else if (status == 404)
  {
    reason = "Not Found";
  }
  else if (status == 500)
  {
    reason = "Internal Server Error";
  }
  std::ostringstream oss;
  oss << "HTTP/1.1 " << status << " " << reason << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
  return oss.str();
}

}  // namespace

RestTestServer::RestTestServer() = default;

RestTestServer::~RestTestServer()
{
  this->stop();
}

bool RestTestServer::start()
{
  if (this->running_)
  {
    return true;
  }

  const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0)
  {
    return false;
  }

  const int yes = 1;
  (void)::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(this->port_);
  if (::bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    ::close(listenFd);
    return false;
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(listenFd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
  {
    ::close(listenFd);
    return false;
  }
  this->port_ = ntohs(addr.sin_port);

  if (::listen(listenFd, 16) != 0)
  {
    ::close(listenFd);
    return false;
  }

  const int flags = ::fcntl(listenFd, F_GETFL, 0);
  if (flags >= 0)
  {
    (void)::fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
  }

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->resetStateLocked();
  }

  this->listen_fd_ = listenFd;
  this->client_fd_.store(-1);
  this->running_ = true;
  this->thread_ = std::thread([this] { this->run(); });
  return true;
}

void RestTestServer::stop()
{
  this->running_ = false;
  const int clientFd = this->client_fd_.load();
  if (clientFd >= 0)
  {
    (void)::shutdown(clientFd, SHUT_RDWR);
  }
  if (this->listen_fd_ >= 0)
  {
    (void)::shutdown(this->listen_fd_, SHUT_RDWR);
  }
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }
  const int leftover = this->client_fd_.exchange(-1);
  if (leftover >= 0)
  {
    (void)::close(leftover);
  }
  this->closeListen();
}

void RestTestServer::closeListen()
{
  if (this->listen_fd_ >= 0)
  {
    (void)::close(this->listen_fd_);
    this->listen_fd_ = -1;
  }
}

void RestTestServer::resetStateLocked()
{
  this->mixer_ = Machine{false, false, 42.0, 21.5};
  this->pump_ = Machine{false, false, 125.0, 21.0};
  this->unknown_ = Machine{false, true, 72.0, 0.0};
  this->last_method_.clear();
  this->last_path_.clear();
  this->last_body_.clear();
  this->mixer_start_count_ = 0;
  this->pump_start_count_ = 0;
}

std::string RestTestServer::host() const
{
  return "127.0.0.1";
}

std::uint16_t RestTestServer::port() const
{
  return this->port_;
}

void RestTestServer::requireAuthorization(std::string headerValue)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->required_authorization_ = std::move(headerValue);
}

void RestTestServer::setSlowDelayMs(int delayMs)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->slow_delay_ms_ = delayMs;
}

bool RestTestServer::mixerRunning() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->mixer_.running;
}

bool RestTestServer::mixerFault() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->mixer_.fault;
}

double RestTestServer::mixerSpeed() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->mixer_.a;
}

double RestTestServer::mixerTemperature() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->mixer_.b;
}

void RestTestServer::setMixerRunning(bool running)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->mixer_.running = running;
}

void RestTestServer::setMixerFault(bool fault)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->mixer_.fault = fault;
}

void RestTestServer::setMixerSpeed(double speed)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->mixer_.a = speed;
}

void RestTestServer::setMixerTemperature(double temperature)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->mixer_.b = temperature;
}

bool RestTestServer::pumpRunning() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->pump_.running;
}

bool RestTestServer::pumpFault() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->pump_.fault;
}

double RestTestServer::pumpFlow() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->pump_.a;
}

double RestTestServer::pumpPressure() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->pump_.b;
}

void RestTestServer::setPumpRunning(bool running)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->pump_.running = running;
}

void RestTestServer::setPumpFault(bool fault)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->pump_.fault = fault;
}

void RestTestServer::setPumpFlow(double flow)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->pump_.a = flow;
}

void RestTestServer::setPumpPressure(double pressure)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->pump_.b = pressure;
}

bool RestTestServer::unknownRunning() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->unknown_.running;
}

bool RestTestServer::unknownFault() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->unknown_.fault;
}

double RestTestServer::unknownTemperature() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->unknown_.a;
}

void RestTestServer::setUnknownRunning(bool running)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->unknown_.running = running;
}

void RestTestServer::setUnknownFault(bool fault)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->unknown_.fault = fault;
}

void RestTestServer::setUnknownTemperature(double temperature)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->unknown_.a = temperature;
}

std::string RestTestServer::lastMethod() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->last_method_;
}

std::string RestTestServer::lastPath() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->last_path_;
}

std::string RestTestServer::lastBody() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->last_body_;
}

int RestTestServer::mixerStartCount() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->mixer_start_count_;
}

int RestTestServer::pumpStartCount() const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->pump_start_count_;
}

void RestTestServer::run()
{
  while (this->running_)
  {
    pollfd pfd;
    pfd.fd = this->listen_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int prc = ::poll(&pfd, 1, 50);
    if (!this->running_)
    {
      break;
    }
    if (prc <= 0 || (pfd.revents & POLLIN) == 0)
    {
      continue;
    }

    const int client = ::accept(this->listen_fd_, nullptr, nullptr);
    if (client < 0)
    {
      continue;
    }
    this->client_fd_.store(client);
    this->handleClient(client);
    const int current = this->client_fd_.exchange(-1);
    if (current >= 0)
    {
      (void)::close(current);
    }
  }
}

void RestTestServer::handleClient(int clientFd)
{
  std::string raw;
  if (!readAtLeast(clientFd, &raw, 4, 2000))
  {
    return;
  }

  std::string::size_type headerEnd = raw.find("\r\n\r\n");
  while (headerEnd == std::string::npos)
  {
    if (!readAtLeast(clientFd, &raw, raw.size() + 1, 2000))
    {
      return;
    }
    headerEnd = raw.find("\r\n\r\n");
  }

  const std::string head = raw.substr(0, headerEnd);
  const auto lineEnd = head.find("\r\n");
  if (lineEnd == std::string::npos)
  {
    return;
  }
  const std::string requestLine = head.substr(0, lineEnd);
  std::istringstream line(requestLine);
  std::string method;
  std::string target;
  std::string version;
  line >> method >> target >> version;
  auto query = target.find('?');
  if (query != std::string::npos)
  {
    target.resize(query);
  }

  std::size_t contentLength = 0;
  const std::string headers = head.substr(lineEnd + 2);
  const std::string lengthText = headerValue(headers, "Content-Length");
  if (!lengthText.empty())
  {
    try
    {
      contentLength = static_cast<std::size_t>(std::stoul(lengthText));
    }
    catch (...)
    {
      contentLength = 0;
    }
  }
  const std::string authorization = headerValue(headers, "Authorization");

  const std::size_t bodyOffset = headerEnd + 4;
  if (!readAtLeast(clientFd, &raw, bodyOffset + contentLength, 2000))
  {
    return;
  }
  const std::string body = raw.substr(bodyOffset, contentLength);

  int delayMs = 0;
  int status = 200;
  std::string responseBody;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (target == "/slow")
    {
      delayMs = this->slow_delay_ms_;
    }
    responseBody = this->handleRequestLocked(
        method, target, authorization, body, &status);
  }

  if (delayMs > 0)
  {
    pollfd idle;
    idle.fd = clientFd;
    idle.events = 0;
    idle.revents = 0;
    (void)::poll(&idle, 1, delayMs);
  }

  const std::string response = httpResponse(status, responseBody);
  std::size_t sent = 0;
  while (sent < response.size())
  {
    const ssize_t n = ::send(
        clientFd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
    if (n <= 0)
    {
      break;
    }
    sent += static_cast<std::size_t>(n);
  }
}

std::string RestTestServer::handleRequestLocked(
    const std::string &method,
    const std::string &path,
    const std::string &authorization,
    const std::string &body,
    int *status)
{
  this->last_method_ = method;
  this->last_path_ = path;
  this->last_body_ = body;

  if (!this->required_authorization_.empty() &&
      authorization != this->required_authorization_)
  {
    *status = 401;
    return "{\"error\":\"unauthorized\"}";
  }

  auto mixerJson = [this]() {
    std::ostringstream oss;
    oss << "{\"running\":" << jsonBool(this->mixer_.running)
        << ",\"fault\":" << jsonBool(this->mixer_.fault)
        << ",\"telemetry\":{\"speed\":" << this->mixer_.a
        << ",\"temperature\":" << this->mixer_.b << "}}";
    return oss.str();
  };
  auto pumpJson = [this]() {
    std::ostringstream oss;
    oss << "{\"running\":" << jsonBool(this->pump_.running)
        << ",\"fault\":" << jsonBool(this->pump_.fault)
        << ",\"telemetry\":{\"flow_rate\":" << this->pump_.a
        << ",\"pressure\":" << this->pump_.b << "}}";
    return oss.str();
  };
  auto unknownJson = [this]() {
    std::ostringstream oss;
    oss << "{\"running\":" << jsonBool(this->unknown_.running)
        << ",\"fault\":" << jsonBool(this->unknown_.fault)
        << ",\"telemetry\":{\"temperature\":" << this->unknown_.a << "}}";
    return oss.str();
  };

  *status = 200;
  if ((path == "/health" || path == "/gw/health") && method == "GET")
  {
    return "{\"status\":\"ok\"}";
  }
  if ((path == "/api/mixer" || path == "/gw/api/mixer") && method == "GET")
  {
    return mixerJson();
  }
  if (path == "/api/mixer/start" && method == "POST")
  {
    this->mixer_.running = true;
    ++this->mixer_start_count_;
    return "{\"ok\":true}";
  }
  if (path == "/api/mixer/stop" && method == "POST")
  {
    this->mixer_.running = false;
    return "{\"ok\":true}";
  }
  if (path == "/api/mixer/speed" && method == "PUT")
  {
    double value = 0.0;
    if (extractJsonNumber(body, "value", &value))
    {
      this->mixer_.a = value;
    }
    return "{\"ok\":true}";
  }
  if (path == "/api/pump" && method == "GET")
  {
    return pumpJson();
  }
  if (path == "/api/pump/start" && method == "PATCH")
  {
    this->pump_.running = true;
    ++this->pump_start_count_;
    return "{\"ok\":true}";
  }
  if (path == "/api/pump/stop" && method == "POST")
  {
    this->pump_.running = false;
    return "{\"ok\":true}";
  }
  if (path == "/api/unknown" && method == "GET")
  {
    return unknownJson();
  }
  if (path == "/api/unknown/start" && method == "POST")
  {
    this->unknown_.running = true;
    return "{\"ok\":true}";
  }
  if (path == "/api/unknown/stop" && method == "POST")
  {
    this->unknown_.running = false;
    return "{\"ok\":true}";
  }
  if (path == "/fail" && method == "GET")
  {
    *status = 500;
    return "{\"error\":\"boom\"}";
  }
  if (path == "/slow" && method == "GET")
  {
    return "{\"status\":\"slow\"}";
  }

  *status = 404;
  return "{\"error\":\"not found\"}";
}

}  // namespace test
}  // namespace virtual_factory
