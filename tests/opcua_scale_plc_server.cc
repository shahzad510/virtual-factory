#include "opcua_scale_plc_server.hh"

#include <open62541.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <sstream>

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
    return 0;
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
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

UA_NodeId stringNode(const std::string &identifier)
{
  return UA_NODEID_STRING_ALLOC(1, identifier.c_str());
}

std::string paddedIndex(int index)
{
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%03d", index);
  return buffer;
}

}  // namespace

OpcUaScalePlcServer::OpcUaScalePlcServer(int index)
    : index_(index),
      equipment_id_("PLC_" + paddedIndex(index) + "_MACHINE_001")
{
}

OpcUaScalePlcServer::~OpcUaScalePlcServer()
{
  this->stop();
}

int OpcUaScalePlcServer::index() const
{
  return this->index_;
}

bool OpcUaScalePlcServer::start()
{
  if (this->server_ != nullptr)
  {
    return true;
  }

  if (this->port_ == 0)
  {
    this->port_ = findFreeTcpPort();
    if (this->port_ == 0)
    {
      return false;
    }
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

  static UA_Logger quiet_logger = UA_Log_Stdout_withLevel(UA_LOGLEVEL_ERROR);
  UA_ServerConfig *live = UA_Server_getConfig(this->server_);
  if (live != nullptr)
  {
    live->logging = &quiet_logger;
  }

  if (!this->addNodes())
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

void OpcUaScalePlcServer::stop()
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

std::uint16_t OpcUaScalePlcServer::port() const
{
  return this->port_;
}

std::string OpcUaScalePlcServer::endpointUrl() const
{
  return "opc.tcp://127.0.0.1:" + std::to_string(this->port_);
}

std::string OpcUaScalePlcServer::equipmentId() const
{
  return this->equipment_id_;
}

std::string OpcUaScalePlcServer::runningNodeId() const
{
  return this->equipment_id_ + ".Running";
}

std::string OpcUaScalePlcServer::faultNodeId() const
{
  return this->equipment_id_ + ".Fault";
}

std::string OpcUaScalePlcServer::temperatureNodeId() const
{
  return this->equipment_id_ + ".Temperature";
}

std::string OpcUaScalePlcServer::pressureNodeId() const
{
  return this->equipment_id_ + ".Pressure";
}

std::string OpcUaScalePlcServer::speedNodeId() const
{
  return this->equipment_id_ + ".Speed";
}

std::string OpcUaScalePlcServer::startNodeId() const
{
  return this->equipment_id_ + ".Start";
}

std::string OpcUaScalePlcServer::stopNodeId() const
{
  return this->equipment_id_ + ".Stop";
}

std::string OpcUaScalePlcServer::speedSetpointNodeId() const
{
  return this->equipment_id_ + ".SpeedSetpoint";
}

double OpcUaScalePlcServer::expectedTemperature() const
{
  return 20.0 + static_cast<double>(this->index_);
}

double OpcUaScalePlcServer::expectedPressure() const
{
  return 1.0 + 0.01 * static_cast<double>(this->index_);
}

double OpcUaScalePlcServer::expectedSpeed() const
{
  return 10.0 + static_cast<double>(this->index_);
}

bool OpcUaScalePlcServer::startNode() const
{
  return this->readBoolean(this->startNodeId());
}

bool OpcUaScalePlcServer::stopNode() const
{
  return this->readBoolean(this->stopNodeId());
}

double OpcUaScalePlcServer::speedSetpoint() const
{
  return this->readDouble(this->speedSetpointNodeId());
}

double OpcUaScalePlcServer::temperature() const
{
  return this->readDouble(this->temperatureNodeId());
}

bool OpcUaScalePlcServer::addNodes()
{
  return this->addBooleanNode(this->runningNodeId(), false) &&
         this->addBooleanNode(this->faultNodeId(), false) &&
         this->addBooleanNode(this->startNodeId(), false) &&
         this->addBooleanNode(this->stopNodeId(), false) &&
         this->addDoubleNode(this->temperatureNodeId(),
                             this->expectedTemperature()) &&
         this->addDoubleNode(this->pressureNodeId(), this->expectedPressure()) &&
         this->addDoubleNode(this->speedNodeId(), this->expectedSpeed()) &&
         this->addDoubleNode(this->speedSetpointNodeId(), 0.0);
}

bool OpcUaScalePlcServer::addBooleanNode(
    const std::string &identifier, bool initial)
{
  UA_VariableAttributes attr = UA_VariableAttributes_default;
  UA_Boolean value = initial ? UA_TRUE : UA_FALSE;
  UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
  attr.displayName = UA_LOCALIZEDTEXT(
      const_cast<char *>("en-US"), const_cast<char *>(identifier.c_str()));
  attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
  attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status = UA_Server_addVariableNode(
      this->server_,
      nodeId,
      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
      UA_QUALIFIEDNAME(1, const_cast<char *>(identifier.c_str())),
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
      attr,
      nullptr,
      nullptr);
  UA_NodeId_clear(&nodeId);
  return status == UA_STATUSCODE_GOOD;
}

bool OpcUaScalePlcServer::addDoubleNode(
    const std::string &identifier, double initial)
{
  UA_VariableAttributes attr = UA_VariableAttributes_default;
  UA_Double value = initial;
  UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_DOUBLE]);
  attr.displayName = UA_LOCALIZEDTEXT(
      const_cast<char *>("en-US"), const_cast<char *>(identifier.c_str()));
  attr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
  attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

  UA_NodeId nodeId = stringNode(identifier);
  const UA_StatusCode status = UA_Server_addVariableNode(
      this->server_,
      nodeId,
      UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
      UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
      UA_QUALIFIEDNAME(1, const_cast<char *>(identifier.c_str())),
      UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
      attr,
      nullptr,
      nullptr);
  UA_NodeId_clear(&nodeId);
  return status == UA_STATUSCODE_GOOD;
}

bool OpcUaScalePlcServer::readBoolean(const std::string &identifier) const
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

double OpcUaScalePlcServer::readDouble(const std::string &identifier) const
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

}  // namespace test
}  // namespace virtual_factory
