#include "modbus_test_server.hh"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <modbus.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace virtual_factory
{
namespace test
{

namespace
{

modbus_t *asCtx(void *ctx)
{
  return static_cast<modbus_t *>(ctx);
}

modbus_mapping_t *asMap(void *mapping)
{
  return static_cast<modbus_mapping_t *>(mapping);
}

}  // namespace

ModbusTestServer::ModbusTestServer() = default;

ModbusTestServer::~ModbusTestServer()
{
  this->stop();
}

bool ModbusTestServer::start()
{
  if (this->running_)
  {
    return true;
  }

  modbus_t *ctx = modbus_new_tcp("127.0.0.1", static_cast<int>(this->port_));
  if (ctx == nullptr)
  {
    return false;
  }
  if (modbus_set_slave(ctx, static_cast<int>(kUnitId)) == -1)
  {
    modbus_free(ctx);
    return false;
  }

  const int listenFd = modbus_tcp_listen(ctx, 8);
  if (listenFd < 0)
  {
    modbus_free(ctx);
    return false;
  }

  const int yes = 1;
  (void)::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (this->port_ == 0)
  {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    if (::getsockname(
            listenFd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
    {
      ::close(listenFd);
      modbus_free(ctx);
      return false;
    }
    this->port_ = ntohs(addr.sin_port);
  }

  const int flags = ::fcntl(listenFd, F_GETFL, 0);
  if (flags >= 0)
  {
    (void)::fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
  }

  modbus_mapping_t *mapping =
      modbus_mapping_new(kTableSize, kTableSize, kTableSize, kTableSize);
  if (mapping == nullptr)
  {
    ::close(listenFd);
    modbus_free(ctx);
    return false;
  }

  (void)modbus_set_indication_timeout(ctx, 0, 500000);

  this->ctx_ = ctx;
  this->mapping_ = mapping;
  this->listen_fd_ = listenFd;
  this->client_fd_.store(-1);
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->resetTablesLocked();
  }

  this->running_ = true;
  this->thread_ = std::thread([this] { this->run(); });
  return true;
}

void ModbusTestServer::stop()
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
  if (this->mapping_ != nullptr)
  {
    modbus_mapping_free(asMap(this->mapping_));
    this->mapping_ = nullptr;
  }
  if (this->ctx_ != nullptr)
  {
    (void)modbus_set_socket(asCtx(this->ctx_), -1);
    modbus_free(asCtx(this->ctx_));
    this->ctx_ = nullptr;
  }
}

void ModbusTestServer::closeListen()
{
  if (this->listen_fd_ >= 0)
  {
    (void)::close(this->listen_fd_);
    this->listen_fd_ = -1;
  }
}

void ModbusTestServer::resetTablesLocked()
{
  modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping == nullptr)
  {
    return;
  }
  std::memset(mapping->tab_bits, 0, static_cast<size_t>(mapping->nb_bits));
  std::memset(
      mapping->tab_input_bits, 0, static_cast<size_t>(mapping->nb_input_bits));
  std::memset(
      mapping->tab_registers,
      0,
      static_cast<size_t>(mapping->nb_registers) * sizeof(uint16_t));
  std::memset(
      mapping->tab_input_registers,
      0,
      static_cast<size_t>(mapping->nb_input_registers) * sizeof(uint16_t));
  mapping->tab_registers[kMixerSpeedActual] = 42;
  mapping->tab_input_registers[kMixerTemperature] = 71;
  mapping->tab_registers[kPumpFlow] = 125;
  mapping->tab_registers[kPumpPressure] = 21;
  mapping->tab_registers[kUnknownTemperature] = 72;
  mapping->tab_bits[kUnknownFault] = 1;
}

std::string ModbusTestServer::host() const
{
  return "127.0.0.1";
}

std::uint16_t ModbusTestServer::port() const
{
  return this->port_;
}

bool ModbusTestServer::coil(std::uint16_t address) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping == nullptr || address >= static_cast<std::uint16_t>(mapping->nb_bits))
  {
    return false;
  }
  return mapping->tab_bits[address] != 0;
}

void ModbusTestServer::setCoil(std::uint16_t address, bool value)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping != nullptr &&
      address < static_cast<std::uint16_t>(mapping->nb_bits))
  {
    mapping->tab_bits[address] = value ? 1 : 0;
  }
}

std::uint16_t ModbusTestServer::holding(std::uint16_t address) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping == nullptr ||
      address >= static_cast<std::uint16_t>(mapping->nb_registers))
  {
    return 0;
  }
  return mapping->tab_registers[address];
}

void ModbusTestServer::setHolding(std::uint16_t address, std::uint16_t value)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping != nullptr &&
      address < static_cast<std::uint16_t>(mapping->nb_registers))
  {
    mapping->tab_registers[address] = value;
  }
}

std::uint16_t ModbusTestServer::inputRegister(std::uint16_t address) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping == nullptr ||
      address >= static_cast<std::uint16_t>(mapping->nb_input_registers))
  {
    return 0;
  }
  return mapping->tab_input_registers[address];
}

void ModbusTestServer::setInputRegister(
    std::uint16_t address, std::uint16_t value)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping != nullptr &&
      address < static_cast<std::uint16_t>(mapping->nb_input_registers))
  {
    mapping->tab_input_registers[address] = value;
  }
}

bool ModbusTestServer::discreteInput(std::uint16_t address) const
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  const modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping == nullptr ||
      address >= static_cast<std::uint16_t>(mapping->nb_input_bits))
  {
    return false;
  }
  return mapping->tab_input_bits[address] != 0;
}

void ModbusTestServer::setDiscreteInput(std::uint16_t address, bool value)
{
  std::lock_guard<std::mutex> lock(this->mutex_);
  modbus_mapping_t *mapping = asMap(this->mapping_);
  if (mapping != nullptr &&
      address < static_cast<std::uint16_t>(mapping->nb_input_bits))
  {
    mapping->tab_input_bits[address] = value ? 1 : 0;
  }
}

void ModbusTestServer::run()
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

void ModbusTestServer::handleClient(int clientFd)
{
  modbus_t *ctx = asCtx(this->ctx_);
  if (ctx == nullptr)
  {
    return;
  }
  (void)modbus_set_socket(ctx, clientFd);

  uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
  while (this->running_)
  {
    pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int prc = ::poll(&pfd, 1, 50);
    if (!this->running_)
    {
      break;
    }
    if (prc <= 0)
    {
      continue;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      break;
    }
    if ((pfd.revents & POLLIN) == 0)
    {
      continue;
    }

    const int rc = modbus_receive(ctx, query);
    if (rc <= 0)
    {
      break;
    }

    std::lock_guard<std::mutex> lock(this->mutex_);
    (void)modbus_reply(ctx, query, rc, asMap(this->mapping_));
  }
}

}  // namespace test
}  // namespace virtual_factory
