#include <virtual_factory/industrial/RestIndustrialAdapter.hh>

#include <virtual_factory/equipment/GenericEquipment.hh>

#include "http_session.hh"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <utility>

namespace virtual_factory
{

namespace
{

internal::HttpMethod toHttpMethod(RestHttpMethod method)
{
  switch (method)
  {
    case RestHttpMethod::Get:
      return internal::HttpMethod::Get;
    case RestHttpMethod::Post:
      return internal::HttpMethod::Post;
    case RestHttpMethod::Put:
      return internal::HttpMethod::Put;
    case RestHttpMethod::Patch:
      return internal::HttpMethod::Patch;
  }
  return internal::HttpMethod::Get;
}

bool isWriteMethod(RestHttpMethod method)
{
  return method == RestHttpMethod::Post || method == RestHttpMethod::Put ||
         method == RestHttpMethod::Patch;
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

bool pointerValue(
    const nlohmann::json &doc, const std::string &pointer, nlohmann::json *out)
{
  if (out == nullptr || pointer.empty())
  {
    return false;
  }
  try
  {
    *out = doc.at(nlohmann::json::json_pointer(pointer));
    return true;
  }
  catch (...)
  {
    return false;
  }
}

}  // namespace

struct RestIndustrialAdapter::ClientHandle
{
  internal::HttpSession session;
};

class RestIndustrialAdapter::BoundEquipment : public Equipment
{
public:
  BoundEquipment(
      RestIndustrialAdapter *adapter, const RestEquipmentMapping *mapping)
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

    const RestCommandMapping *mapped = nullptr;
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

    if (!isWriteMethod(mapped->method))
    {
      return {false, "command mapping must be POST, PUT, or PATCH"};
    }

    const std::string body = substituteValue(mapped->bodyTemplate, parameter);
    if (!this->adapter_->performMappedWrite(
            mapped->method, mapped->path, body))
    {
      return {false, "http write failed"};
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
    if (this->mapping_->telemetryPath.empty())
    {
      return this->mapping_->telemetry.empty() &&
             this->mapping_->statePointer.empty() &&
             this->mapping_->faultPointer.empty();
    }

    std::string body;
    if (!this->adapter_->performMappedGet(this->mapping_->telemetryPath, &body))
    {
      return false;
    }

    nlohmann::json doc;
    try
    {
      doc = nlohmann::json::parse(body);
    }
    catch (...)
    {
      this->adapter_->enterFault("invalid JSON in telemetry response");
      return false;
    }

    for (const auto &point : this->mapping_->telemetry)
    {
      nlohmann::json node;
      double value = 0.0;
      if (!pointerValue(doc, point.jsonPointer, &node) ||
          !jsonAsDouble(node, &value))
      {
        this->adapter_->enterFault(
            std::string("missing or invalid telemetry pointer ") +
            point.jsonPointer);
        return false;
      }
      this->inner_.setTelemetry(point.name, value, point.unit);
    }

    if (!this->mapping_->statePointer.empty())
    {
      nlohmann::json node;
      bool running = false;
      if (!pointerValue(doc, this->mapping_->statePointer, &node) ||
          !jsonAsBool(node, &running))
      {
        this->adapter_->enterFault("missing or invalid state pointer");
        return false;
      }
      this->inner_.setOperationalState(
          running ? OperationalState::Running : OperationalState::Stopped);
    }

    if (!this->mapping_->faultPointer.empty())
    {
      nlohmann::json node;
      bool fault = false;
      if (!pointerValue(doc, this->mapping_->faultPointer, &node) ||
          !jsonAsBool(node, &fault))
      {
        this->adapter_->enterFault("missing or invalid fault pointer");
        return false;
      }
      this->inner_.setFault(fault);
    }

    return true;
  }

private:
  RestIndustrialAdapter *adapter_;
  const RestEquipmentMapping *mapping_;
  GenericEquipment inner_;
};

RestIndustrialAdapter::RestIndustrialAdapter(
    std::string id, RestAdapterConfig config)
    : id_(std::move(id)),
      config_(std::move(config)),
      client_(std::make_unique<ClientHandle>())
{
}

RestIndustrialAdapter::~RestIndustrialAdapter()
{
  this->disconnect();
}

std::string RestIndustrialAdapter::id() const
{
  return this->id_;
}

std::string RestIndustrialAdapter::protocol() const
{
  return "rest";
}

ConnectionState RestIndustrialAdapter::connectionState() const
{
  return this->connection_state_;
}

std::string RestIndustrialAdapter::lastError() const
{
  return this->last_error_;
}

bool RestIndustrialAdapter::connect()
{
  if (this->connection_state_ == ConnectionState::Connected)
  {
    return true;
  }

  if (this->config_.host.empty() || this->config_.port == 0 ||
      this->config_.scheme.empty())
  {
    this->enterFault("missing REST origin scheme, host, or port");
    return false;
  }

  internal::HttpSessionConfig sessionConfig;
  sessionConfig.scheme = this->config_.scheme;
  sessionConfig.host = this->config_.host;
  sessionConfig.port = this->config_.port;
  sessionConfig.basePath = this->config_.basePath;
  sessionConfig.timeoutMs = this->config_.timeoutMs;
  sessionConfig.tlsVerify = this->config_.tlsVerify;
  if (this->config_.auth.kind == RestAuthKind::Basic)
  {
    sessionConfig.useBasicAuth = true;
    sessionConfig.username = this->config_.auth.username;
    sessionConfig.password = this->config_.auth.password;
  }
  else if (this->config_.auth.kind == RestAuthKind::Bearer)
  {
    sessionConfig.useBearerAuth = true;
    sessionConfig.bearerToken = this->config_.auth.bearerToken;
  }

  this->client_->session.close();
  this->client_->session.configure(sessionConfig);

  if (!this->config_.healthPath.empty())
  {
    std::string body;
    if (!this->performMappedGet(this->config_.healthPath, &body))
    {
      return false;
    }
  }
  else if (!this->client_->session.probeConnect())
  {
    this->enterFault(
        std::string("REST origin connect failed: ") +
        this->client_->session.lastError());
    return false;
  }

  this->bindEquipment();
  this->connection_state_ = ConnectionState::Connected;
  this->last_error_.clear();
  return true;
}

void RestIndustrialAdapter::disconnect()
{
  this->bound_.clear();
  if (this->client_)
  {
    this->client_->session.close();
  }
  this->connection_state_ = ConnectionState::Disconnected;
  this->last_error_.clear();
}

std::vector<Equipment *> RestIndustrialAdapter::equipment()
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

Equipment *RestIndustrialAdapter::equipmentById(const std::string &id)
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

void RestIndustrialAdapter::poll()
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
      if (this->last_error_.empty())
      {
        this->enterFault(
            std::string("REST read failed during poll: ") +
            this->client_->session.lastError());
      }
      return;
    }
  }
}

void RestIndustrialAdapter::bindEquipment()
{
  this->bound_.clear();
  for (const auto &mapping : this->config_.equipment)
  {
    this->bound_.push_back(std::make_unique<BoundEquipment>(this, &mapping));
  }
}

void RestIndustrialAdapter::enterFault(const std::string &reason)
{
  this->connection_state_ = ConnectionState::Faulted;
  this->last_error_ = reason;
}

bool RestIndustrialAdapter::performMappedGet(
    const std::string &path, std::string *body)
{
  if (path.empty() || body == nullptr)
  {
    this->enterFault("REST GET failed: empty path");
    return false;
  }

  const internal::HttpResponse response =
      this->client_->session.request(internal::HttpMethod::Get, path, "");
  if (!response.success())
  {
    this->enterFault(
        response.error.empty() ? this->client_->session.lastError()
                               : response.error);
    return false;
  }
  *body = response.body;
  return true;
}

bool RestIndustrialAdapter::performMappedWrite(
    RestHttpMethod method,
    const std::string &path,
    const std::string &body)
{
  if (!isWriteMethod(method) || path.empty())
  {
    this->enterFault("REST write failed: invalid method or path");
    return false;
  }

  const internal::HttpResponse response = this->client_->session.request(
      toHttpMethod(method), path, body);
  if (!response.success())
  {
    this->enterFault(
        response.error.empty() ? this->client_->session.lastError()
                               : response.error);
    return false;
  }
  return true;
}

}  // namespace virtual_factory
