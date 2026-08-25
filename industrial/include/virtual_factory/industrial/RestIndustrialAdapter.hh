#ifndef VIRTUAL_FACTORY_REST_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_REST_INDUSTRIAL_ADAPTER_HH_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// HTTP methods used by mapped REST reads and commands.
/// DELETE is not part of Phase 6E.
enum class RestHttpMethod
{
  Get,
  Post,
  Put,
  Patch
};

enum class RestAuthKind
{
  None,
  Basic,
  Bearer
};

/// Credentials stay in adapter config. Never placed on Equipment.
/// Passwords and bearer tokens must not be logged.
struct RestAuthConfig
{
  RestAuthKind kind{RestAuthKind::None};
  std::string username;
  std::string password;
  std::string bearerToken;
};

/// Named command → HTTP write. Method must be POST, PUT, or PATCH.
/// `bodyTemplate` may contain `{{value}}`, replaced by execute()'s double.
struct RestCommandMapping
{
  std::string command;
  RestHttpMethod method{RestHttpMethod::Post};
  std::string path;
  std::string bodyTemplate;
};

/// Named telemetry point extracted from the equipment JSON resource.
/// `jsonPointer` is RFC 6901 (e.g. "/telemetry/speed").
struct RestTelemetryMapping
{
  std::string name;
  std::string jsonPointer;
  std::string unit;
};

/// One machine as seen through a REST gateway. Type is metadata, not a C++ class.
/// One GET of `telemetryPath` should carry multiple telemetry values when the
/// API allows it. Optional state/fault pointers read that same JSON document.
struct RestEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<RestCommandMapping> commands;
  std::string telemetryPath;
  std::vector<RestTelemetryMapping> telemetry;
  std::string statePointer;
  std::string faultPointer;
};

/// Config for one HTTP origin (one adapter instance).
/// Origin = scheme + host + port + optional base path (ADR-037).
struct RestAdapterConfig
{
  std::string scheme{"http"};
  std::string host{"127.0.0.1"};
  std::uint16_t port{80};
  std::string basePath;
  int timeoutMs{2000};
  /// Optional GET used by connect() to prove connectivity.
  std::string healthPath;
  RestAuthConfig auth;
  /// TLS certificate verification. Must stay true for production HTTPS.
  /// Set false only as an explicit development/testing opt-in.
  bool tlsVerify{true};
  std::vector<RestEquipmentMapping> equipment;
};

/// Production REST industrial gateway adapter (SoT Phase 6 slice 6E).
///
/// HTTP client to one industrial gateway/vendor origin. Not a fieldbus.
/// Not the future MES REST API. Translates configured paths and JSON Pointers
/// into GenericEquipment. Does not expose curl, nlohmann/json, URLs, or
/// credentials through Equipment.
///
/// One instance = one origin. N independent REST systems ⇒ N adapter
/// instances. Several logical machines on one API are several
/// GenericEquipment mappings, not extra adapter classes.
///
/// connect() after Faulted recreates the HTTP client session. Background
/// auto-reconnect is not implemented.
class RestIndustrialAdapter : public IndustrialAdapter
{
public:
  RestIndustrialAdapter(std::string id, RestAdapterConfig config);
  ~RestIndustrialAdapter() override;

  RestIndustrialAdapter(const RestIndustrialAdapter &) = delete;
  RestIndustrialAdapter &operator=(const RestIndustrialAdapter &) = delete;

  std::string id() const override;
  std::string protocol() const override;
  ConnectionState connectionState() const override;
  std::string lastError() const override;

  bool connect() override;
  void disconnect() override;

  std::vector<Equipment *> equipment() override;
  Equipment *equipmentById(const std::string &id) override;

  void poll() override;

private:
  class BoundEquipment;
  friend class BoundEquipment;

  struct ClientHandle;

  void bindEquipment();
  void enterFault(const std::string &reason);
  bool performMappedGet(const std::string &path, std::string *body);
  bool performMappedWrite(
      RestHttpMethod method,
      const std::string &path,
      const std::string &body);

  std::string id_;
  RestAdapterConfig config_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<ClientHandle> client_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
};

}  // namespace virtual_factory

#endif
