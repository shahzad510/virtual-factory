#include <virtual_factory/industrial/OpcUaIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include <open62541.h>

#include <string>
#include <utility>

namespace virtual_factory
{

namespace
{

UA_NodeId makeNodeId(const OpcUaNodeRef &node)
{
  return UA_NODEID_STRING_ALLOC(node.namespaceIndex, node.identifier.c_str());
}

std::string formatNodeId(const OpcUaNodeRef &node)
{
  return "ns=" + std::to_string(static_cast<unsigned>(node.namespaceIndex))
      + ";s=" + node.identifier;
}

}  // namespace

OpcUaNodeRef opcUaNodeRefFromConfig(
    std::uint16_t namespaceIndex, const std::string &address)
{
  // Expanded string NodeId: ns=<digits>;s=<identifier>
  // Example: "ns=2;s=MotorSpeed" → {2, "MotorSpeed"}
  if (address.size() >= 6 && address.compare(0, 3, "ns=") == 0)
  {
    const std::size_t semi = address.find(";s=");
    if (semi != std::string::npos && semi > 3)
    {
      unsigned long parsedNs = 0;
      bool numeric = true;
      for (std::size_t i = 3; i < semi; ++i)
      {
        const char ch = address[i];
        if (ch < '0' || ch > '9')
        {
          numeric = false;
          break;
        }
        parsedNs = parsedNs * 10UL + static_cast<unsigned long>(ch - '0');
        if (parsedNs > 65535UL)
        {
          numeric = false;
          break;
        }
      }
      const std::string ident = address.substr(semi + 3);
      if (numeric && !ident.empty())
      {
        OpcUaNodeRef node;
        node.namespaceIndex = static_cast<std::uint16_t>(parsedNs);
        node.identifier = ident;
        return node;
      }
    }
  }

  OpcUaNodeRef node;
  node.namespaceIndex = namespaceIndex;
  node.identifier = address;
  return node;
}

namespace
{

std::string statusCodeName(UA_StatusCode status)
{
  const char *name = UA_StatusCode_name(status);
  if (name == nullptr || name[0] == '\0')
  {
    return "StatusCode=0x" + std::to_string(static_cast<unsigned long>(status));
  }
  return std::string(name);
}

/// True when the StatusCode indicates session/secure-channel/transport loss.
/// Application-level failures (BadNodeIdUnknown, type mismatch, etc.) are soft.
bool isConnectivityStatus(UA_StatusCode status)
{
  switch (status)
  {
    case UA_STATUSCODE_BADCONNECTIONCLOSED:
    case UA_STATUSCODE_BADDISCONNECT:
    case UA_STATUSCODE_BADCONNECTIONREJECTED:
    case UA_STATUSCODE_BADSERVERNOTCONNECTED:
    case UA_STATUSCODE_BADSHUTDOWN:
    case UA_STATUSCODE_BADSESSIONCLOSED:
    case UA_STATUSCODE_BADSESSIONIDINVALID:
    case UA_STATUSCODE_BADSESSIONNOTACTIVATED:
    case UA_STATUSCODE_BADSECURECHANNELCLOSED:
    case UA_STATUSCODE_BADSECURECHANNELIDINVALID:
    case UA_STATUSCODE_BADSECURECHANNELTOKENUNKNOWN:
    // Dead/unreachable peer during sync I/O (blackhole, stalled TCP, etc.).
    case UA_STATUSCODE_BADTIMEOUT:
    case UA_STATUSCODE_BADNOTCONNECTED:
    case UA_STATUSCODE_BADCOMMUNICATIONERROR:
    case UA_STATUSCODE_BADTCPINTERNALERROR:
    case UA_STATUSCODE_BADTCPSERVERTOOBUSY:
    case UA_STATUSCODE_BADTCPNOTENOUGHRESOURCES:
      return true;
    default:
      return false;
  }
}

bool variantAsBool(const UA_Variant &value, bool *out)
{
  if (out == nullptr || value.data == nullptr || UA_Variant_isEmpty(&value))
  {
    return false;
  }

  if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN])
  {
    *out = *static_cast<const UA_Boolean *>(value.data);
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_BYTE])
  {
    *out = *static_cast<const UA_Byte *>(value.data) != 0;
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_INT32])
  {
    *out = *static_cast<const UA_Int32 *>(value.data) != 0;
    return true;
  }
  return false;
}

bool variantAsDouble(const UA_Variant &value, double *out)
{
  if (out == nullptr || value.data == nullptr || UA_Variant_isEmpty(&value))
  {
    return false;
  }

  if (value.type == &UA_TYPES[UA_TYPES_DOUBLE])
  {
    *out = *static_cast<const UA_Double *>(value.data);
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_FLOAT])
  {
    *out = static_cast<double>(*static_cast<const UA_Float *>(value.data));
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_INT32])
  {
    *out = static_cast<double>(*static_cast<const UA_Int32 *>(value.data));
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_UINT32])
  {
    *out = static_cast<double>(*static_cast<const UA_UInt32 *>(value.data));
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_INT16])
  {
    *out = static_cast<double>(*static_cast<const UA_Int16 *>(value.data));
    return true;
  }
  if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN])
  {
    *out = *static_cast<const UA_Boolean *>(value.data) ? 1.0 : 0.0;
    return true;
  }
  return false;
}

bool isSetCommand(const std::string &command)
{
  return command.size() > 4 && command.compare(0, 4, "set_") == 0;
}

}  // namespace

struct OpcUaIndustrialAdapter::ClientHandle
{
  UA_Client *client{nullptr};
};

class OpcUaIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      OpcUaIndustrialAdapter *adapter, const OpcUaEquipmentMapping *mapping)
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

    const OpcUaCommandMapping *mapped = nullptr;
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

    bool ok = false;
    if (isSetCommand(command))
    {
      ok = this->adapter_->writeDouble(mapped->node, parameter);
    }
    else
    {
      ok = this->adapter_->writeBoolean(mapped->node, true);
    }

    if (!ok)
    {
      return {false, "opc ua write failed"};
    }

    return {true, "written"};
  }

  std::vector<TelemetryPoint> telemetry() const override
  {
    return this->inner_.telemetry();
  }

  EquipmentStatus status() const override
  {
    return this->inner_.status();
  }

  bool refreshFromServer()
  {
    for (const auto &point : this->mapping_->telemetry)
    {
      double value = 0.0;
      if (!this->adapter_->readDouble(point.node, &value))
      {
        return false;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (!this->mapping_->stateNode.identifier.empty())
    {
      bool running = false;
      if (!this->adapter_->readBoolean(this->mapping_->stateNode, &running))
      {
        return false;
      }
      this->inner_.setOperationalState(
          running ? OperationalState::Running : OperationalState::Stopped);
    }

    if (!this->mapping_->faultNode.identifier.empty())
    {
      bool fault = false;
      if (!this->adapter_->readBoolean(this->mapping_->faultNode, &fault))
      {
        return false;
      }
      this->inner_.setFault(fault);
    }

    return true;
  }

private:
  OpcUaIndustrialAdapter *adapter_;
  const OpcUaEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

OpcUaIndustrialAdapter::OpcUaIndustrialAdapter(
    std::string id, OpcUaAdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      client_(std::make_unique<ClientHandle>())
{
}

OpcUaIndustrialAdapter::~OpcUaIndustrialAdapter()
{
  this->disconnect();
}

std::string OpcUaIndustrialAdapter::id() const
{
  return this->id_;
}

std::string OpcUaIndustrialAdapter::protocol() const
{
  return "opcua";
}

ConnectionState OpcUaIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string OpcUaIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool OpcUaIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.endpointUrl.empty())
  {
    this->enterFault("missing OPC UA endpoint URL");
    return false;
  }

  this->releaseClient();

  this->client_->client = UA_Client_new();
  if (this->client_->client == nullptr)
  {
    this->enterFault("failed to create OPC UA client");
    return false;
  }

  UA_ClientConfig *clientConfig = UA_Client_getConfig(this->client_->client);
  clientConfig->timeout = static_cast<UA_UInt32>(this->resolvedTimeoutMs());
  // Explicit ICP lifecycle owns reconnect. open62541 must not silently rebuild
  // a SecureChannel underneath connection_state_ (would desynchronize ICP).
  clientConfig->noReconnect = true;
  clientConfig->securityMode = UA_MESSAGESECURITYMODE_NONE;

  const UA_StatusCode status =
      UA_Client_connect(this->client_->client, this->config_.endpointUrl.c_str());
  if (status != UA_STATUSCODE_GOOD)
  {
    this->releaseClient();
    this->enterFault(
        std::string("OPC UA connect failed: ") + UA_StatusCode_name(status));
    return false;
  }

  this->bindEquipment();
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  this->last_service_status_ = UA_STATUSCODE_GOOD;
  this->last_failure_was_connectivity_ = false;
  return true;
}

int OpcUaIndustrialAdapter::resolvedTimeoutMs() const
{
  return this->config_.timeoutMs > 0 ? this->config_.timeoutMs : 2000;
}


void OpcUaIndustrialAdapter::releaseClient()
{
  if (this->client_ && this->client_->client != nullptr)
  {
    UA_Client *client = this->client_->client;
    this->client_->client = nullptr;

    // Bound teardown: a dead peer must not block ICP. Shrink the client timeout
    // and delete. Do NOT call UA_Client_disconnectSecureChannel here — open62541
    // can wait in an unbounded "while channel not CLOSED" loop on a wedged peer.
    // UA_Client_delete performs cleanup; with a short timeout, request waits are
    // bounded by clientConfig->timeout.
    UA_ClientConfig *clientConfig = UA_Client_getConfig(client);
    if (clientConfig != nullptr)
    {
      const UA_UInt32 teardownMs = 250;
      const UA_UInt32 configured =
          static_cast<UA_UInt32>(this->resolvedTimeoutMs());
      clientConfig->timeout = configured < teardownMs ? configured : teardownMs;
      clientConfig->noReconnect = true;
    }
    UA_Client_delete(client);
  }
}

void OpcUaIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  this->releaseClient();
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
  this->last_service_status_ = UA_STATUSCODE_GOOD;
  this->last_failure_was_connectivity_ = false;
}

std::vector<Equipment *> OpcUaIndustrialAdapter::equipment()
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

Equipment *OpcUaIndustrialAdapter::equipmentById(const std::string &id)
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

void OpcUaIndustrialAdapter::poll()
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

  for (auto &item : this->bound_)
  {
    if (!item->refreshFromServer())
    {
      const std::string detail =
          this->last_error_.empty() ? "OPC UA read failed during poll"
                                    : this->last_error_;
      // Soft failures (BadNodeId, type mismatch, etc.) must NOT tear down the
      // connection lifecycle. Only genuine session/channel loss → FAULTED.
      if (this->last_failure_was_connectivity_)
      {
        this->enterFault(detail);
        return;
      }
      // Keep CONNECTED; expose the read problem via lastError for operators.
      this->last_error_ = detail;
      return;
    }
  }
  this->last_error_.clear();
  this->last_failure_was_connectivity_ = false;
}

void OpcUaIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void OpcUaIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
  // With noReconnect, a dead SecureChannel/session is unusable. Drop the client
  // so connection_state_ matches reality and connect()/reconnect() must create
  // a fresh UA_Client. Keep bound_ equipment for last-known Faulted reads.
  this->releaseClient();
}

bool OpcUaIndustrialAdapter::readBoolean(const OpcUaNodeRef &node, bool *value)
{
  if (this->client_->client == nullptr || node.identifier.empty())
  {
    this->last_service_status_ = UA_STATUSCODE_BADINVALIDARGUMENT;
    // Null client while Connected means the session object is gone — connectivity.
    this->last_failure_was_connectivity_ = (this->client_->client == nullptr);
    this->last_error_ =
        "OPC UA read failed for " + formatNodeId(node)
        + ": invalid client or empty NodeId";
    return false;
  }

  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_NodeId nodeId = makeNodeId(node);

  const UA_StatusCode status =
      UA_Client_readValueAttribute(this->client_->client, nodeId, &variant);
  this->last_service_status_ = status;

  if (status != UA_STATUSCODE_GOOD)
  {
    this->last_failure_was_connectivity_ = isConnectivityStatus(status);
    this->last_error_ =
        "OPC UA read failed for " + formatNodeId(node) + ": "
        + statusCodeName(status);
    UA_NodeId_clear(&nodeId);
    UA_Variant_clear(&variant);
    return false;
  }

  bool converted = variantAsBool(variant, value);
  UA_NodeId_clear(&nodeId);

  if (!converted)
  {
    this->last_failure_was_connectivity_ = false;
    this->last_error_ =
        "OPC UA value type unsupported for " + formatNodeId(node);
    UA_Variant_clear(&variant);
    return false;
  }

  UA_Variant_clear(&variant);
  this->last_failure_was_connectivity_ = false;
  return true;
}

bool OpcUaIndustrialAdapter::readDouble(const OpcUaNodeRef &node, double *value)
{
  if (this->client_->client == nullptr || node.identifier.empty())
  {
    this->last_service_status_ = UA_STATUSCODE_BADINVALIDARGUMENT;
    this->last_failure_was_connectivity_ = (this->client_->client == nullptr);
    this->last_error_ =
        "OPC UA read failed for " + formatNodeId(node)
        + ": invalid client or empty NodeId";
    return false;
  }

  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_NodeId nodeId = makeNodeId(node);

  const UA_StatusCode status =
      UA_Client_readValueAttribute(this->client_->client, nodeId, &variant);
  this->last_service_status_ = status;

  if (status != UA_STATUSCODE_GOOD)
  {
    this->last_failure_was_connectivity_ = isConnectivityStatus(status);
    this->last_error_ =
        "OPC UA read failed for " + formatNodeId(node) + ": "
        + statusCodeName(status);
    UA_NodeId_clear(&nodeId);
    UA_Variant_clear(&variant);
    return false;
  }

  bool converted = variantAsDouble(variant, value);
  UA_NodeId_clear(&nodeId);

  if (!converted)
  {
    this->last_failure_was_connectivity_ = false;
    this->last_error_ =
        "OPC UA value type unsupported for " + formatNodeId(node);
    UA_Variant_clear(&variant);
    return false;
  }

  UA_Variant_clear(&variant);
  this->last_failure_was_connectivity_ = false;
  return true;
}

bool OpcUaIndustrialAdapter::writeBoolean(const OpcUaNodeRef &node, bool value)
{
  if (node.identifier.empty())
  {
    this->last_failure_was_connectivity_ = false;
    this->last_error_ = "OPC UA write failed: empty NodeId";
    return false;
  }
  if (this->client_->client == nullptr)
  {
    this->enterFault("OPC UA write failed: invalid client");
    return false;
  }

  UA_Boolean uaValue = value ? UA_TRUE : UA_FALSE;
  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_Variant_setScalar(&variant, &uaValue, &UA_TYPES[UA_TYPES_BOOLEAN]);

  UA_NodeId nodeId = makeNodeId(node);
  const UA_StatusCode status =
      UA_Client_writeValueAttribute(this->client_->client, nodeId, &variant);
  UA_NodeId_clear(&nodeId);
  this->last_service_status_ = status;

  if (status != UA_STATUSCODE_GOOD)
  {
    const std::string detail =
        std::string("OPC UA write failed: ") + UA_StatusCode_name(status);
    if (isConnectivityStatus(status))
    {
      this->enterFault(detail);
    }
    else
    {
      this->last_error_ = detail;
      this->last_failure_was_connectivity_ = false;
    }
    return false;
  }
  return true;
}

bool OpcUaIndustrialAdapter::writeDouble(const OpcUaNodeRef &node, double value)
{
  if (node.identifier.empty())
  {
    this->last_failure_was_connectivity_ = false;
    this->last_error_ = "OPC UA write failed: empty NodeId";
    return false;
  }
  if (this->client_->client == nullptr)
  {
    this->enterFault("OPC UA write failed: invalid client");
    return false;
  }

  UA_Double uaValue = value;
  UA_Variant variant;
  UA_Variant_init(&variant);
  UA_Variant_setScalar(&variant, &uaValue, &UA_TYPES[UA_TYPES_DOUBLE]);

  UA_NodeId nodeId = makeNodeId(node);
  const UA_StatusCode status =
      UA_Client_writeValueAttribute(this->client_->client, nodeId, &variant);
  UA_NodeId_clear(&nodeId);
  this->last_service_status_ = status;

  if (status != UA_STATUSCODE_GOOD)
  {
    const std::string detail =
        std::string("OPC UA write failed: ") + UA_StatusCode_name(status);
    if (isConnectivityStatus(status))
    {
      this->enterFault(detail);
    }
    else
    {
      this->last_error_ = detail;
      this->last_failure_was_connectivity_ = false;
    }
    return false;
  }
  return true;
}

}  // namespace virtual_factory
