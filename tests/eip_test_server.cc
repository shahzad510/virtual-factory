#include "eip_test_server.hh"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

#ifndef VF_AB_SERVER_EXECUTABLE
#define VF_AB_SERVER_EXECUTABLE ""
#endif
#ifndef VF_LIBPLCTAG_LIBRARY_DIR
#define VF_LIBPLCTAG_LIBRARY_DIR ""
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
  const char *dir = VF_LIBPLCTAG_LIBRARY_DIR;
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

}  // namespace

EipTestServer::~EipTestServer()
{
  this->stop();
}

void EipTestServer::stop()
{
  if (this->pid_ > 0)
  {
    ::kill(this->pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i)
    {
      const pid_t waited = ::waitpid(this->pid_, &status, WNOHANG);
      if (waited == this->pid_)
      {
        break;
      }
      if (waited < 0)
      {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (::waitpid(this->pid_, &status, WNOHANG) == 0)
    {
      ::kill(this->pid_, SIGKILL);
      ::waitpid(this->pid_, &status, 0);
    }
    this->pid_ = -1;
  }
}

bool EipTestServer::start(const std::vector<std::string> &tags)
{
  this->stop();
  this->last_error_.clear();

  if (tags.empty())
  {
    this->last_error_ = "no tags configured for ab_server";
    return false;
  }

  const char *exe = VF_AB_SERVER_EXECUTABLE;
  if (exe == nullptr || exe[0] == '\0')
  {
    this->last_error_ = "VF_AB_SERVER_EXECUTABLE not configured";
    return false;
  }

  if (this->port_ == 0)
  {
    this->port_ = pickFreePort();
  }
  if (this->port_ == 0)
  {
    this->last_error_ = "failed to allocate free TCP port";
    return false;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::vector<std::string> argsStorage;
  argsStorage.emplace_back(exe);
  argsStorage.emplace_back("--plc=ControlLogix");
  argsStorage.emplace_back("--path=" + this->path_);
  argsStorage.emplace_back("--port=" + std::to_string(this->port_));
  for (const auto &tag : tags)
  {
    argsStorage.emplace_back("--tag=" + tag);
  }

  std::vector<char *> argv;
  argv.reserve(argsStorage.size() + 1);
  for (auto &arg : argsStorage)
  {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  const pid_t pid = ::fork();
  if (pid < 0)
  {
    this->last_error_ = "fork failed for ab_server";
    return false;
  }

  if (pid == 0)
  {
    applyLibraryPath();
    const int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd >= 0)
    {
      ::dup2(nullFd, STDIN_FILENO);
      ::dup2(nullFd, STDOUT_FILENO);
      ::dup2(nullFd, STDERR_FILENO);
      if (nullFd > STDERR_FILENO)
      {
        ::close(nullFd);
      }
    }
    ::execv(exe, argv.data());
    ::_exit(127);
  }

  this->pid_ = pid;
  if (!this->waitReady())
  {
    const std::string err = this->last_error_;
    const std::uint16_t keptPort = this->port_;
    this->stop();
    this->port_ = keptPort;
    if (this->last_error_.empty())
    {
      this->last_error_ = err.empty() ? "ab_server did not become ready" : err;
    }
    return false;
  }
  return true;
}

std::string EipTestServer::host() const { return "127.0.0.1"; }
std::uint16_t EipTestServer::port() const { return this->port_; }
std::string EipTestServer::path() const { return this->path_; }
std::string EipTestServer::lastError() const { return this->last_error_; }

bool EipTestServer::waitReady()
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline)
  {
    int status = 0;
    const pid_t waited = ::waitpid(this->pid_, &status, WNOHANG);
    if (waited == this->pid_)
    {
      this->last_error_ = "ab_server exited before becoming ready";
      this->pid_ = -1;
      return false;
    }
    if (tcpReady(this->host(), this->port_))
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  this->last_error_ = "timeout waiting for ab_server TCP listen";
  return false;
}

}  // namespace test
}  // namespace virtual_factory
