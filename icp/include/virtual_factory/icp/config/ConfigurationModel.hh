#ifndef VIRTUAL_FACTORY_ICP_CONFIGURATION_MODEL_HH_
#define VIRTUAL_FACTORY_ICP_CONFIGURATION_MODEL_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace icp
{

/// Result of a configuration mutation, load, save, or validation.
struct ConfigIssue
{
  std::string path;
  std::string message;
};

struct ConfigResult
{
  bool ok{false};
  std::string message;
  std::vector<ConfigIssue> issues;
};

/// Credential *references* only. ICP-1B does not implement a secret vault.
/// Values are names such as "env:MQTT_PASSWORD" — never passwords themselves.
struct CredentialRefs
{
  std::string username;
  std::string passwordRef;
  std::string tokenRef;
};

struct TelemetryMappingRecord
{
  std::string name;
  std::string unit;
  /// Protocol-specific addressing (NodeId, register, topic, tag, offsets).
  std::string address;
  std::string encoding;
  std::string jsonPointer;
  std::uint16_t namespaceIndex{0};
  std::uint8_t unitId{0};
  std::string table;
  std::uint16_t registerAddress{0};
  int qos{0};
  std::size_t inputByteOffset{0};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
  std::string valueType;
};

struct CommandMappingRecord
{
  std::string command;
  std::string address;
  std::string bodyTemplate;
  std::string method;
  int qos{0};
  bool retain{false};
  std::uint16_t namespaceIndex{0};
  std::uint8_t unitId{0};
  std::string table;
  std::uint16_t registerAddress{0};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
  std::string valueType;
};

struct SignalMappingRecord
{
  bool mapped{false};
  std::string address;
  std::string encoding;
  std::string jsonPointer;
  std::uint16_t namespaceIndex{0};
  std::uint8_t unitId{0};
  std::string table;
  std::uint16_t registerAddress{0};
  int qos{0};
  std::size_t inputByteOffset{0};
  std::size_t bitOffset{0};
  std::string valueType;
};

struct ProfinetSubmoduleRecord
{
  std::uint16_t slot{0};
  std::uint16_t subslot{1};
  std::size_t inputLength{0};
  std::size_t outputLength{0};
};

struct ProfibusModuleRecord
{
  unsigned slot{0};
  std::string ident;
  std::size_t inputLength{0};
  std::size_t outputLength{0};
};

/// One GenericEquipment mapping under one adapter. No PLC C++ classes.
struct EquipmentMappingRecord
{
  std::string equipmentId;
  /// Optional; if set, must match the parent adapter's adapterId.
  std::string adapterId;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<TelemetryMappingRecord> telemetry;
  std::vector<CommandMappingRecord> commands;
  SignalMappingRecord state;
  SignalMappingRecord fault;

  /// REST: GET path that carries telemetry JSON.
  std::string telemetryPath;
  std::string statePointer;
  std::string faultPointer;

  /// PROFINET IO-Device identity (Designer-ready; no protocol stack here).
  std::string stationName;
  std::string ipAddress;
  std::uint16_t vendorId{0};
  std::uint16_t deviceId{0};
  std::vector<ProfinetSubmoduleRecord> submodules;

  /// PROFIBUS slave identity.
  unsigned stationAddress{0};
  std::vector<ProfibusModuleRecord> modules;
};

/// Connection parameters for one industrial source/session.
struct AdapterConnectionRecord
{
  std::string endpointUrl;
  std::string host;
  std::uint16_t port{0};
  int timeoutMs{0};
  int pollTimeoutMs{0};
  int keepaliveSeconds{0};
  std::string scheme;
  std::string basePath;
  std::string healthPath;
  std::string clientId;
  std::string path;
  std::string plcType;
  bool useTls{false};
  bool tlsVerify{true};
  std::string boardId;
  unsigned channel{0};
  std::string interfaceName;
  /// Controller/master station name (ICP metadata; live DCP/DP naming is firmware).
  std::string stationName;
  std::string configArtifactPath;
  std::string expectedFirmwareName;
  unsigned masterAddress{0};
  unsigned baudRateKbps{0};
  std::size_t processImageBytes{0};
};

/// One adapter instance (one protocol session). Protocol-oriented.
struct AdapterConfigRecord
{
  std::string adapterId;
  std::string protocol;
  /// Active stack: gateway | hilscher_native | softing_native | simulated (optional; inferred when empty).
  std::string implementation;
  bool enabled{true};
  std::string description;
  AdapterConnectionRecord connection;
  CredentialRefs credentials;
  std::vector<EquipmentMappingRecord> equipment;
};

/// Versioned ICP configuration document. Replaceable persistence later.
struct IcpConfigurationDocument
{
  static constexpr int kCurrentVersion = 1;
  static constexpr const char *kSchemaId = "virtual-factory.icp.config";

  std::string schema{kSchemaId};
  int version{kCurrentVersion};
  std::string name;
  std::vector<AdapterConfigRecord> adapters;
};

}  // namespace icp
}  // namespace virtual_factory

#endif
