#include "opcua_test_server.hh"

#include <open62541.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace virtual_factory
{
namespace test
{
namespace
{

std::uint16_t findFreeTcpPort()
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return 48410;
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    ::close(fd);
    return 48410;
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
  {
    ::close(fd);
    return 48410;
  }

  const std::uint16_t port = ntohs(addr.sin_port);
  ::close(fd);
  return port == 0 ? 48410 : port;
}

UA_NodeId stringNode(const char *identifier)
{
  return UA_NODEID_STRING_ALLOC(1, identifier);
}

}  // namespace

OpcUaTestServer::OpcUaTestServer() = default;

OpcUaTestServer::~OpcUaTestServer()
{
  this->stop();
}

bool OpcUaTestServer::start()
{
  if (this->server_ != nullptr)
  {
    return true;
  }

  if (this->port_ == 0)
  {
    this->port_ = findFreeTcpPort();
  }

  UA_ServerConfig config;
  std::memset(&config, 0, sizeof(config));
  const UA_StatusCode configStatus =
      UA_ServerConfig_setMinimal(&config, this->port_, nullptr);
  if (configStatus != UA_STATUSCODE_GOOD)
  {
    return false;
  }

  this->server_ = UA_Server_newWithConfig(&config);
  if (this->server_ == nullptr)
  {
    return false;
  }

  if (!this->addMixerNodes() || !this->addPumpNodes() ||
      !this->addUnknownMachineNodes())
  {
    this->stop();
    return false;
  }

  const UA_StatusCode startup = UA_Server_run_startup(this->server_);
  if (startup != UA_STATUSCODE_GOOD)
  {
    this->stop();
    return false;
  }

  this->iterate_ = true;
  this->thread_ = std::thread([this]() {
    while (this->iterate_)
    {
      UA_Server_run_iterate(this->server_, true);
    }
  });
  return true;
}

void OpcUaTestServer::stop()
{
  this->iterate_ = false;
  if (this->thread_.joinable())
  {
    this->thread_.join();
  }

  if (this->server_ != nullptr)
  {
    UA_Server_run_shutdown(this->server_);
    UA_Server_delete(this->server_);
    this->server_ = nullptr;
  }
}

std::uint16_t OpcUaTestServer::port() const
{
  return this->port_;
}

std::string OpcUaTestServer::endpointUrl() const
{
  return "opc.tcp://127.0.0.1:" + std::to_string(this->port_);
}

bool OpcUaTestServer::booleanNode(const char *identifier) const
{
  return this->readBoolean(identifier);
}

double OpcUaTestServer::doubleNode(const char *identifier) const
{
  return this->readDouble(identifier);
}

void OpcUaTestServer::setBooleanNode(const char *identifier, bool value)
{
  this->writeBoolean(identifier, value);
}

void OpcUaTestServer::setDoubleNode(const char *identifier, double value)
{
  this->writeDouble(identifier, value);
}

bool OpcUaTestServer::addMixerNodes()
{
  return this->addBooleanNode(kMixerRunning, false) &&
         this->addBooleanNode(kMixerFault, false) &&
         this->addBooleanNode(kMixerStart, false) &&
         this->addBooleanNode(kMixerStop, false) &&
         this->addDoubleNode(kMixerSpeedSetpoint, 0.0) &&
         this->addDoubleNode(kMixerSpeedActual, 42.0) &&
         this->addDoubleNode(kMixerTemperature, 25.5);
}

bool OpcUaTestServer::addPumpNodes()
{
  return this->addBooleanNode(kPumpRunning, true) &&
         this->addBooleanNode(kPumpFault, false) &&
         this->addBooleanNode(kPumpStart, false) &&
         this->addBooleanNode(kPumpStop, false) &&
         this->addDoubleNode(kPumpFlowRate, 125.4) &&
         this->addDoubleNode(kPumpPressure, 2.1);
}

bool OpcUaTestServer::addUnknownMachineNodes()
{
  return this->addBooleanNode(kUnknownRunning, false) &&
         this->addBooleanNode(kUnknownFault, true) &&
         this->addBooleanNode(kUnknownStart, false) &&
         this->addBooleanNode(kUnknownStop, false) &&
         this->addDoubleNode(kUnknownTemperature, 72.5);
}

bool OpcUaTestServer::runningNode() const
{
  return this->readBoolean(kRunning);
}

bool OpcUaTestServer::faultNode() const
{
  return this->readBoolean(kFault);
}

bool OpcUaTestServer::startNode() const
{
  return this->readBoolean(kStart);
}

bool OpcUaTestServer::stopNode() const
{
  return this->readBoolean(kStop);
}

double OpcUaTestServer::speedSetpoint() const
{
  return this->readDouble(kSpeedSetpoint);
}

double OpcUaTestServer::speedActual() const
{
  return this->readDouble(kSpeedActual);
}

double OpcUaTestServer::temperature() const
{
  return this->readDouble(kTemperature);
}

void OpcUaTestServer::setRunning(bool value)
{
  this->writeBoolean(kRunning, value);
}

void OpcUaTestServer::setFault(bool value)
{
  this->writeBoolean(kFault, value);
}

void OpcUaTestServer::setSpeedActual(double value)
{
  this->writeDouble(kSpeedActual, value);
}

void OpcUaTestServer::setTemperature(double value)
{
  this->writeDouble(kTemperature, value);
}

bool OpcUaTestServer::addBooleanNode(const char *identifier, bool initial)
{
  UA_VariableAttributes attr = UA_VariableAttributes_default;
  UA_Boolean value = initial ? UA_TRUE : UA_FALSE;
  UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
  attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                      const_cast<char *>(identifier));
  attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
  attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status = UA_Server_addVariableNode(
      this->server_,
      nodeId,
      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
      UA_QUALIFIEDNAME(1, const_cast<char *>(identifier)),
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
      attr,
      nullptr,
      nullptr);
  UA_NodeId_clear(&nodeId);
  return status == UA_STATUSCODE_GOOD;
}

bool OpcUaTestServer::addDoubleNode(const char *identifier, double initial)
{
  UA_VariableAttributes attr = UA_VariableAttributes_default;
  UA_Double value = initial;
  UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_DOUBLE]);
  attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                      const_cast<char *>(identifier));
  attr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
  attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status = UA_Server_addVariableNode(
      this->server_,
      nodeId,
      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
      UA_QUALIFIEDNAME(1, const_cast<char *>(identifier)),
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
      attr,
      nullptr,
      nullptr);
  UA_NodeId_clear(&nodeId);
  return status == UA_STATUSCODE_GOOD;
}

bool OpcUaTestServer::readBoolean(const char *identifier) const
{
  if (this->server_ == nullptr)
  {
    return false;
  }

  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status =
      UA_Server_readValue(this->server_, nodeId, &variant);
  UA_NodeId_clear(&nodeId);

  bool result = false;
  if (status == UA_STATUSCODE_GOOD && variant.type == &UA_TYPES[UA_TYPES_BOOLEAN] &&
      variant.data != nullptr)
  {
    result = *static_cast<UA_Boolean *>(variant.data);
  }
  UA_Variant_clear(&variant);
  return result;
}

double OpcUaTestServer::readDouble(const char *identifier) const
{
  if (this->server_ == nullptr)
  {
    return 0.0;
  }

  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status =
      UA_Server_readValue(this->server_, nodeId, &variant);
  UA_NodeId_clear(&nodeId);

  double result = 0.0;
  if (status == UA_STATUSCODE_GOOD && variant.type == &UA_TYPES[UA_TYPES_DOUBLE] &&
      variant.data != nullptr)
  {
    result = *static_cast<UA_Double *>(variant.data);
  }
  UA_Variant_clear(&variant);
  return result;
}

void OpcUaTestServer::writeBoolean(const char *identifier, bool value)
{
  if (this->server_ == nullptr)
  {
    return;
  }

  UA_Boolean uaValue = value ? UA_TRUE : UA_FALSE;
  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_Variant_setScalar(&variant, &uaValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
  UA_NodeId nodeId = stringNode(identifier);
  UA_Server_writeValue(this->server_, nodeId, variant);
  UA_NodeId_clear(&nodeId);
}

void OpcUaTestServer::writeDouble(const char *identifier, double value)
{
  if (this->server_ == nullptr)
  {
    return;
  }

  UA_Double uaValue = value;
  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_Variant_setScalar(&variant, &uaValue, &UA_TYPES[UA_TYPES_DOUBLE]);
  UA_NodeId nodeId = stringNode(identifier);
  UA_Server_writeValue(this->server_, nodeId, variant);
  UA_NodeId_clear(&nodeId);
}

}  // namespace test
}  // namespace virtual_factory
