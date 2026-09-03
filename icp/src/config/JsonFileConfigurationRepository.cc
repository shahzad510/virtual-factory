#include <virtual_factory/icp/config/JsonFileConfigurationRepository.hh>

#include <virtual_factory/icp/config/AdapterImplementation.hh>
#include <virtual_factory/icp/config/ConfigurationValidator.hh>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace virtual_factory
{
namespace icp
{

namespace
{

using json = nlohmann::json;

ConfigResult failResult(const std::string &path, const std::string &message)
{
  ConfigResult result;
  result.ok = false;
  result.message = message;
  result.issues.push_back({path, message});
  return result;
}

ConfigResult okResult()
{
  ConfigResult result;
  result.ok = true;
  result.message = "ok";
  return result;
}

void addIssue(ConfigResult *result, const std::string &path, const std::string &message)
{
  result->ok = false;
  result->issues.push_back({path, message});
  if (result->message.empty() || result->message == "ok")
  {
    result->message = message;
  }
  else
  {
    result->message += "; " + message;
  }
}

void rejectUnknownKeys(
    ConfigResult *result,
    const json &object,
    const std::set<std::string> &allowed,
    const std::string &path)
{
  if (!object.is_object())
  {
    return;
  }
  for (auto it = object.begin(); it != object.end(); ++it)
  {
    if (allowed.count(it.key()) == 0)
    {
      addIssue(
          result,
          path + "/" + it.key(),
          "unknown field '" + it.key() + "' (refusing silent data loss)");
    }
  }
}

void rejectPlaintextSecretKeys(ConfigResult *result, const json &node, const std::string &path)
{
  if (node.is_object())
  {
    for (auto it = node.begin(); it != node.end(); ++it)
    {
      const std::string &key = it.key();
      if (key == "password" || key == "token" || key == "bearerToken"
          || key == "secret" || key == "passwd")
      {
        addIssue(
            result,
            path + "/" + key,
            "plaintext secret field '" + key
                + "' is not allowed; use passwordRef or tokenRef "
                  "(env:NAME, file:PATH, or secret:NAME)");
      }
      rejectPlaintextSecretKeys(result, it.value(), path + "/" + key);
    }
  }
  else if (node.is_array())
  {
    for (std::size_t i = 0; i < node.size(); ++i)
    {
      rejectPlaintextSecretKeys(result, node[i], path + "/" + std::to_string(i));
    }
  }
}

bool requireObject(ConfigResult *result, const json &node, const std::string &path)
{
  if (!node.is_object())
  {
    addIssue(result, path, "expected a JSON object");
    return false;
  }
  return true;
}

bool requireArray(ConfigResult *result, const json &node, const std::string &path)
{
  if (!node.is_array())
  {
    addIssue(result, path, "expected a JSON array");
    return false;
  }
  return true;
}

std::string asString(ConfigResult *result, const json &object, const char *key, const std::string &path)
{
  if (!object.contains(key))
  {
    return {};
  }
  if (!object[key].is_string())
  {
    addIssue(result, path + "/" + key, "expected a string");
    return {};
  }
  return object[key].get<std::string>();
}

int asInt(ConfigResult *result, const json &object, const char *key, const std::string &path, int fallback)
{
  if (!object.contains(key))
  {
    return fallback;
  }
  if (!object[key].is_number_integer())
  {
    addIssue(result, path + "/" + key, "expected an integer");
    return fallback;
  }
  return object[key].get<int>();
}

unsigned asUnsigned(
    ConfigResult *result, const json &object, const char *key, const std::string &path, unsigned fallback)
{
  if (!object.contains(key))
  {
    return fallback;
  }
  if (!object[key].is_number_unsigned() && !object[key].is_number_integer())
  {
    addIssue(result, path + "/" + key, "expected an unsigned integer");
    return fallback;
  }
  if (object[key].is_number_integer() && object[key].get<int>() < 0)
  {
    addIssue(result, path + "/" + key, "expected a non-negative integer");
    return fallback;
  }
  return object[key].get<unsigned>();
}

std::uint16_t asUint16(
    ConfigResult *result, const json &object, const char *key, const std::string &path, std::uint16_t fallback)
{
  const unsigned value = asUnsigned(result, object, key, path, fallback);
  if (value > 65535)
  {
    addIssue(result, path + "/" + key, "value exceeds uint16 range");
    return fallback;
  }
  return static_cast<std::uint16_t>(value);
}

std::uint8_t asUint8(
    ConfigResult *result, const json &object, const char *key, const std::string &path, std::uint8_t fallback)
{
  const unsigned value = asUnsigned(result, object, key, path, fallback);
  if (value > 255)
  {
    addIssue(result, path + "/" + key, "value exceeds uint8 range");
    return fallback;
  }
  return static_cast<std::uint8_t>(value);
}

std::size_t asSize(
    ConfigResult *result, const json &object, const char *key, const std::string &path, std::size_t fallback)
{
  if (!object.contains(key))
  {
    return fallback;
  }
  if (!object[key].is_number_unsigned() && !object[key].is_number_integer())
  {
    addIssue(result, path + "/" + key, "expected a size integer");
    return fallback;
  }
  if (object[key].is_number_integer() && object[key].get<int>() < 0)
  {
    addIssue(result, path + "/" + key, "expected a non-negative integer");
    return fallback;
  }
  return object[key].get<std::size_t>();
}

bool asBool(ConfigResult *result, const json &object, const char *key, const std::string &path, bool fallback)
{
  if (!object.contains(key))
  {
    return fallback;
  }
  if (!object[key].is_boolean())
  {
    addIssue(result, path + "/" + key, "expected a boolean");
    return fallback;
  }
  return object[key].get<bool>();
}

TelemetryMappingRecord parseTelemetry(
    ConfigResult *result, const json &object, const std::string &path)
{
  TelemetryMappingRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"name", "unit", "address", "encoding", "jsonPointer", "namespaceIndex",
       "unitId", "table", "registerAddress", "qos", "inputByteOffset",
       "outputByteOffset", "bitOffset", "valueType"},
      path);
  record.name = asString(result, object, "name", path);
  record.unit = asString(result, object, "unit", path);
  record.address = asString(result, object, "address", path);
  record.encoding = asString(result, object, "encoding", path);
  record.jsonPointer = asString(result, object, "jsonPointer", path);
  record.namespaceIndex = asUint16(result, object, "namespaceIndex", path, 0);
  record.unitId = asUint8(result, object, "unitId", path, 0);
  record.table = asString(result, object, "table", path);
  record.registerAddress = asUint16(result, object, "registerAddress", path, 0);
  record.qos = asInt(result, object, "qos", path, 0);
  record.inputByteOffset = asSize(result, object, "inputByteOffset", path, 0);
  record.outputByteOffset = asSize(result, object, "outputByteOffset", path, 0);
  record.bitOffset = asSize(result, object, "bitOffset", path, 0);
  record.valueType = asString(result, object, "valueType", path);
  return record;
}

CommandMappingRecord parseCommand(
    ConfigResult *result, const json &object, const std::string &path)
{
  CommandMappingRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"command", "address", "bodyTemplate", "method", "qos", "retain",
       "namespaceIndex", "unitId", "table", "registerAddress",
       "outputByteOffset", "bitOffset", "valueType"},
      path);
  record.command = asString(result, object, "command", path);
  record.address = asString(result, object, "address", path);
  record.bodyTemplate = asString(result, object, "bodyTemplate", path);
  record.method = asString(result, object, "method", path);
  record.qos = asInt(result, object, "qos", path, 0);
  record.retain = asBool(result, object, "retain", path, false);
  record.namespaceIndex = asUint16(result, object, "namespaceIndex", path, 0);
  record.unitId = asUint8(result, object, "unitId", path, 0);
  record.table = asString(result, object, "table", path);
  record.registerAddress = asUint16(result, object, "registerAddress", path, 0);
  record.outputByteOffset = asSize(result, object, "outputByteOffset", path, 0);
  record.bitOffset = asSize(result, object, "bitOffset", path, 0);
  record.valueType = asString(result, object, "valueType", path);
  return record;
}

SignalMappingRecord parseSignal(
    ConfigResult *result, const json &object, const std::string &path)
{
  SignalMappingRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"mapped", "address", "encoding", "jsonPointer", "namespaceIndex",
       "unitId", "table", "registerAddress", "qos", "inputByteOffset",
       "bitOffset", "valueType"},
      path);
  record.mapped = asBool(result, object, "mapped", path, false);
  record.address = asString(result, object, "address", path);
  record.encoding = asString(result, object, "encoding", path);
  record.jsonPointer = asString(result, object, "jsonPointer", path);
  record.namespaceIndex = asUint16(result, object, "namespaceIndex", path, 0);
  record.unitId = asUint8(result, object, "unitId", path, 0);
  record.table = asString(result, object, "table", path);
  record.registerAddress = asUint16(result, object, "registerAddress", path, 0);
  record.qos = asInt(result, object, "qos", path, 0);
  record.inputByteOffset = asSize(result, object, "inputByteOffset", path, 0);
  record.bitOffset = asSize(result, object, "bitOffset", path, 0);
  record.valueType = asString(result, object, "valueType", path);
  return record;
}

ProfinetSubmoduleRecord parseSubmodule(
    ConfigResult *result, const json &object, const std::string &path)
{
  ProfinetSubmoduleRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result, object, {"slot", "subslot", "inputLength", "outputLength"}, path);
  record.slot = asUint16(result, object, "slot", path, 0);
  record.subslot = asUint16(result, object, "subslot", path, 1);
  record.inputLength = asSize(result, object, "inputLength", path, 0);
  record.outputLength = asSize(result, object, "outputLength", path, 0);
  return record;
}

ProfibusModuleRecord parseModule(
    ConfigResult *result, const json &object, const std::string &path)
{
  ProfibusModuleRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result, object, {"slot", "ident", "inputLength", "outputLength"}, path);
  record.slot = asUnsigned(result, object, "slot", path, 0);
  record.ident = asString(result, object, "ident", path);
  record.inputLength = asSize(result, object, "inputLength", path, 0);
  record.outputLength = asSize(result, object, "outputLength", path, 0);
  return record;
}

AdapterConnectionRecord parseConnection(
    ConfigResult *result, const json &object, const std::string &path)
{
  AdapterConnectionRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"endpointUrl", "host", "port", "timeoutMs", "pollTimeoutMs",
       "keepaliveSeconds", "scheme", "basePath", "healthPath", "clientId",
       "path", "plcType", "useTls", "tlsVerify", "boardId", "channel",
       "interfaceName", "stationName", "configArtifactPath",
       "expectedFirmwareName", "masterAddress", "baudRateKbps",
       "processImageBytes"},
      path);
  record.endpointUrl = asString(result, object, "endpointUrl", path);
  record.host = asString(result, object, "host", path);
  record.port = asUint16(result, object, "port", path, 0);
  record.timeoutMs = asInt(result, object, "timeoutMs", path, 0);
  record.pollTimeoutMs = asInt(result, object, "pollTimeoutMs", path, 0);
  record.keepaliveSeconds = asInt(result, object, "keepaliveSeconds", path, 0);
  record.scheme = asString(result, object, "scheme", path);
  record.basePath = asString(result, object, "basePath", path);
  record.healthPath = asString(result, object, "healthPath", path);
  record.clientId = asString(result, object, "clientId", path);
  record.path = asString(result, object, "path", path);
  record.plcType = asString(result, object, "plcType", path);
  record.useTls = asBool(result, object, "useTls", path, false);
  record.tlsVerify = asBool(result, object, "tlsVerify", path, true);
  record.boardId = asString(result, object, "boardId", path);
  record.channel = asUnsigned(result, object, "channel", path, 0);
  record.interfaceName = asString(result, object, "interfaceName", path);
  record.stationName = asString(result, object, "stationName", path);
  record.configArtifactPath = asString(result, object, "configArtifactPath", path);
  record.expectedFirmwareName = asString(result, object, "expectedFirmwareName", path);
  record.masterAddress = asUnsigned(result, object, "masterAddress", path, 0);
  record.baudRateKbps = asUnsigned(result, object, "baudRateKbps", path, 0);
  record.processImageBytes = asSize(result, object, "processImageBytes", path, 0);
  return record;
}

CredentialRefs parseCredentials(
    ConfigResult *result, const json &object, const std::string &path)
{
  CredentialRefs record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(result, object, {"username", "passwordRef", "tokenRef"}, path);
  record.username = asString(result, object, "username", path);
  record.passwordRef = asString(result, object, "passwordRef", path);
  record.tokenRef = asString(result, object, "tokenRef", path);
  return record;
}

EquipmentMappingRecord parseEquipment(
    ConfigResult *result, const json &object, const std::string &path)
{
  EquipmentMappingRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"equipmentId", "adapterId", "type", "capabilities", "telemetry",
       "commands", "state", "fault", "telemetryPath", "statePointer",
       "faultPointer", "stationName", "ipAddress", "vendorId", "deviceId",
       "submodules", "stationAddress", "modules"},
      path);
  record.equipmentId = asString(result, object, "equipmentId", path);
  record.adapterId = asString(result, object, "adapterId", path);
  record.type = asString(result, object, "type", path);
  if (object.contains("capabilities"))
  {
    if (!requireArray(result, object["capabilities"], path + "/capabilities"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["capabilities"].size(); ++i)
    {
      if (!object["capabilities"][i].is_string())
      {
        addIssue(
            result,
            path + "/capabilities/" + std::to_string(i),
            "expected a string");
        continue;
      }
      record.capabilities.push_back(object["capabilities"][i].get<std::string>());
    }
  }
  if (object.contains("telemetry"))
  {
    if (!requireArray(result, object["telemetry"], path + "/telemetry"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["telemetry"].size(); ++i)
    {
      record.telemetry.push_back(
          parseTelemetry(result, object["telemetry"][i], path + "/telemetry/" + std::to_string(i)));
    }
  }
  if (object.contains("commands"))
  {
    if (!requireArray(result, object["commands"], path + "/commands"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["commands"].size(); ++i)
    {
      record.commands.push_back(
          parseCommand(result, object["commands"][i], path + "/commands/" + std::to_string(i)));
    }
  }
  if (object.contains("state"))
  {
    record.state = parseSignal(result, object["state"], path + "/state");
  }
  if (object.contains("fault"))
  {
    record.fault = parseSignal(result, object["fault"], path + "/fault");
  }
  record.telemetryPath = asString(result, object, "telemetryPath", path);
  record.statePointer = asString(result, object, "statePointer", path);
  record.faultPointer = asString(result, object, "faultPointer", path);
  record.stationName = asString(result, object, "stationName", path);
  record.ipAddress = asString(result, object, "ipAddress", path);
  record.vendorId = asUint16(result, object, "vendorId", path, 0);
  record.deviceId = asUint16(result, object, "deviceId", path, 0);
  if (object.contains("submodules"))
  {
    if (!requireArray(result, object["submodules"], path + "/submodules"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["submodules"].size(); ++i)
    {
      record.submodules.push_back(
          parseSubmodule(result, object["submodules"][i], path + "/submodules/" + std::to_string(i)));
    }
  }
  record.stationAddress = asUnsigned(result, object, "stationAddress", path, 0);
  if (object.contains("modules"))
  {
    if (!requireArray(result, object["modules"], path + "/modules"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["modules"].size(); ++i)
    {
      record.modules.push_back(
          parseModule(result, object["modules"][i], path + "/modules/" + std::to_string(i)));
    }
  }
  return record;
}

AdapterConfigRecord parseAdapter(
    ConfigResult *result, const json &object, const std::string &path)
{
  AdapterConfigRecord record;
  if (!requireObject(result, object, path))
  {
    return record;
  }
  rejectUnknownKeys(
      result,
      object,
      {"adapterId", "protocol", "implementation", "enabled", "description", "connection",
       "credentials", "equipment"},
      path);
  record.adapterId = asString(result, object, "adapterId", path);
  record.protocol = asString(result, object, "protocol", path);
  record.implementation = asString(result, object, "implementation", path);
  record.enabled = asBool(result, object, "enabled", path, true);
  record.description = asString(result, object, "description", path);
  if (object.contains("connection"))
  {
    record.connection = parseConnection(result, object["connection"], path + "/connection");
  }
  if (object.contains("credentials"))
  {
    record.credentials = parseCredentials(result, object["credentials"], path + "/credentials");
  }
  if (object.contains("equipment"))
  {
    if (!requireArray(result, object["equipment"], path + "/equipment"))
    {
      return record;
    }
    for (std::size_t i = 0; i < object["equipment"].size(); ++i)
    {
      record.equipment.push_back(
          parseEquipment(result, object["equipment"][i], path + "/equipment/" + std::to_string(i)));
    }
  }
  normalizeLegacyAdapterImplementation(&record);
  return record;
}

ConfigResult migrateDocument(IcpConfigurationDocument *document)
{
  if (document->version == IcpConfigurationDocument::kCurrentVersion)
  {
    return okResult();
  }
  if (document->version > IcpConfigurationDocument::kCurrentVersion)
  {
    return failResult(
        "/version",
        "unsupported configuration version " + std::to_string(document->version)
            + " (supported: " + std::to_string(IcpConfigurationDocument::kCurrentVersion)
            + "); newer files cannot be loaded by this ICP");
  }
  return failResult(
      "/version",
      "no migration path from configuration version " + std::to_string(document->version)
          + " to " + std::to_string(IcpConfigurationDocument::kCurrentVersion));
}

json signalToJson(const SignalMappingRecord &record)
{
  return json{
      {"mapped", record.mapped},
      {"address", record.address},
      {"encoding", record.encoding},
      {"jsonPointer", record.jsonPointer},
      {"namespaceIndex", record.namespaceIndex},
      {"unitId", static_cast<unsigned>(record.unitId)},
      {"table", record.table},
      {"registerAddress", record.registerAddress},
      {"qos", record.qos},
      {"inputByteOffset", record.inputByteOffset},
      {"bitOffset", record.bitOffset},
      {"valueType", record.valueType},
  };
}

json telemetryToJson(const TelemetryMappingRecord &record)
{
  return json{
      {"name", record.name},
      {"unit", record.unit},
      {"address", record.address},
      {"encoding", record.encoding},
      {"jsonPointer", record.jsonPointer},
      {"namespaceIndex", record.namespaceIndex},
      {"unitId", static_cast<unsigned>(record.unitId)},
      {"table", record.table},
      {"registerAddress", record.registerAddress},
      {"qos", record.qos},
      {"inputByteOffset", record.inputByteOffset},
      {"outputByteOffset", record.outputByteOffset},
      {"bitOffset", record.bitOffset},
      {"valueType", record.valueType},
  };
}

json commandToJson(const CommandMappingRecord &record)
{
  return json{
      {"command", record.command},
      {"address", record.address},
      {"bodyTemplate", record.bodyTemplate},
      {"method", record.method},
      {"qos", record.qos},
      {"retain", record.retain},
      {"namespaceIndex", record.namespaceIndex},
      {"unitId", static_cast<unsigned>(record.unitId)},
      {"table", record.table},
      {"registerAddress", record.registerAddress},
      {"outputByteOffset", record.outputByteOffset},
      {"bitOffset", record.bitOffset},
      {"valueType", record.valueType},
  };
}

json connectionToJson(const AdapterConnectionRecord &record)
{
  return json{
      {"endpointUrl", record.endpointUrl},
      {"host", record.host},
      {"port", record.port},
      {"timeoutMs", record.timeoutMs},
      {"pollTimeoutMs", record.pollTimeoutMs},
      {"keepaliveSeconds", record.keepaliveSeconds},
      {"scheme", record.scheme},
      {"basePath", record.basePath},
      {"healthPath", record.healthPath},
      {"clientId", record.clientId},
      {"path", record.path},
      {"plcType", record.plcType},
      {"useTls", record.useTls},
      {"tlsVerify", record.tlsVerify},
      {"boardId", record.boardId},
      {"channel", record.channel},
      {"interfaceName", record.interfaceName},
      {"stationName", record.stationName},
      {"configArtifactPath", record.configArtifactPath},
      {"expectedFirmwareName", record.expectedFirmwareName},
      {"masterAddress", record.masterAddress},
      {"baudRateKbps", record.baudRateKbps},
      {"processImageBytes", record.processImageBytes},
  };
}

json equipmentToJson(const EquipmentMappingRecord &record)
{
  json telemetry = json::array();
  for (const TelemetryMappingRecord &item : record.telemetry)
  {
    telemetry.push_back(telemetryToJson(item));
  }
  json commands = json::array();
  for (const CommandMappingRecord &item : record.commands)
  {
    commands.push_back(commandToJson(item));
  }
  json submodules = json::array();
  for (const ProfinetSubmoduleRecord &item : record.submodules)
  {
    submodules.push_back(json{
        {"slot", item.slot},
        {"subslot", item.subslot},
        {"inputLength", item.inputLength},
        {"outputLength", item.outputLength},
    });
  }
  json modules = json::array();
  for (const ProfibusModuleRecord &item : record.modules)
  {
    modules.push_back(json{
        {"slot", item.slot},
        {"ident", item.ident},
        {"inputLength", item.inputLength},
        {"outputLength", item.outputLength},
    });
  }
  return json{
      {"equipmentId", record.equipmentId},
      {"adapterId", record.adapterId},
      {"type", record.type},
      {"capabilities", record.capabilities},
      {"telemetry", telemetry},
      {"commands", commands},
      {"state", signalToJson(record.state)},
      {"fault", signalToJson(record.fault)},
      {"telemetryPath", record.telemetryPath},
      {"statePointer", record.statePointer},
      {"faultPointer", record.faultPointer},
      {"stationName", record.stationName},
      {"ipAddress", record.ipAddress},
      {"vendorId", record.vendorId},
      {"deviceId", record.deviceId},
      {"submodules", submodules},
      {"stationAddress", record.stationAddress},
      {"modules", modules},
  };
}

json adapterToJson(const AdapterConfigRecord &record)
{
  json equipment = json::array();
  for (const EquipmentMappingRecord &item : record.equipment)
  {
    equipment.push_back(equipmentToJson(item));
  }
  return json{
      {"adapterId", record.adapterId},
      {"protocol", record.protocol},
      {"implementation", record.implementation},
      {"enabled", record.enabled},
      {"description", record.description},
      {"connection", connectionToJson(record.connection)},
      {"credentials",
       json{
           {"username", record.credentials.username},
           {"passwordRef", record.credentials.passwordRef},
           {"tokenRef", record.credentials.tokenRef},
       }},
      {"equipment", equipment},
  };
}

}  // namespace

JsonFileConfigurationRepository::JsonFileConfigurationRepository(std::string path)
    : path_(std::move(path))
{
}

const std::string &JsonFileConfigurationRepository::path() const
{
  return path_;
}

std::string JsonFileConfigurationRepository::toJsonText(const IcpConfigurationDocument &document)
{
  json adapters = json::array();
  for (const AdapterConfigRecord &adapter : document.adapters)
  {
    adapters.push_back(adapterToJson(adapter));
  }
  json root = json{
      {"schema", document.schema},
      {"version", document.version},
      {"name", document.name},
      {"adapters", adapters},
  };
  return root.dump(2) + "\n";
}

ConfigResult JsonFileConfigurationRepository::parseText(
    const std::string &jsonText, IcpConfigurationDocument *out)
{
  if (out == nullptr)
  {
    return failResult("/", "output document pointer is null");
  }
  if (jsonText.empty())
  {
    return failResult("/", "corrupt configuration: empty file");
  }

  json root;
  try
  {
    root = json::parse(jsonText);
  }
  catch (const json::parse_error &error)
  {
    return failResult("/", std::string("corrupt configuration: ") + error.what());
  }

  ConfigResult result = okResult();
  if (!requireObject(&result, root, "/"))
  {
    return result;
  }
  rejectUnknownKeys(&result, root, {"schema", "version", "name", "adapters"}, "");
  rejectPlaintextSecretKeys(&result, root, "");
  if (!result.ok)
  {
    return result;
  }

  IcpConfigurationDocument document;
  document.schema = asString(&result, root, "schema", "");
  if (!root.contains("version"))
  {
    addIssue(&result, "/version", "version field is required");
  }
  else if (!root["version"].is_number_integer())
  {
    addIssue(&result, "/version", "version must be an integer");
  }
  else
  {
    document.version = root["version"].get<int>();
  }
  document.name = asString(&result, root, "name", "");
  if (root.contains("adapters"))
  {
    if (!requireArray(&result, root["adapters"], "/adapters"))
    {
      return result;
    }
    for (std::size_t i = 0; i < root["adapters"].size(); ++i)
    {
      document.adapters.push_back(
          parseAdapter(&result, root["adapters"][i], "/adapters/" + std::to_string(i)));
    }
  }
  if (!result.ok)
  {
    return result;
  }

  const ConfigResult migrated = migrateDocument(&document);
  if (!migrated.ok)
  {
    return migrated;
  }

  *out = std::move(document);
  return okResult();
}

ConfigResult JsonFileConfigurationRepository::load(IcpConfigurationDocument *out)
{
  if (path_.empty())
  {
    return failResult("/", "configuration path is empty");
  }
  std::ifstream input(path_, std::ios::binary);
  if (!input)
  {
    if (errno == ENOENT)
    {
      // First-run / no saved configuration yet — not an error for standalone ICP.
      *out = IcpConfigurationDocument{};
      out->name = "default";
      ConfigResult result;
      result.ok = true;
      result.message =
          "configuration file not found; using empty in-memory configuration";
      return result;
    }
    return failResult(path_, "cannot open configuration file '" + path_ + "'");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input && !input.eof())
  {
    return failResult(path_, "cannot read configuration file '" + path_ + "'");
  }
  return parseText(buffer.str(), out);
}

ConfigResult JsonFileConfigurationRepository::save(const IcpConfigurationDocument &document)
{
  if (path_.empty())
  {
    return failResult("/", "configuration path is empty");
  }

  const ConfigResult validated = ConfigurationValidator::validate(document);
  if (!validated.ok)
  {
    return validated;
  }

  const std::string text = toJsonText(document);
  const std::string tmpPath = path_ + ".tmp";

  {
    std::ofstream output(tmpPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
      return failResult(
          tmpPath, "cannot write temporary configuration file '" + tmpPath + "'");
    }
    output << text;
    output.flush();
    if (!output)
    {
      return failResult(
          tmpPath, "failed writing temporary configuration file '" + tmpPath + "'");
    }
  }

  const int fd = ::open(tmpPath.c_str(), O_RDONLY);
  if (fd >= 0)
  {
    ::fsync(fd);
    ::close(fd);
  }

  if (::rename(tmpPath.c_str(), path_.c_str()) != 0)
  {
    const int err = errno;
    ::unlink(tmpPath.c_str());
    return failResult(
        path_,
        std::string("atomic replace failed for '") + path_ + "': " + std::strerror(err));
  }
  return okResult();
}

}  // namespace icp
}  // namespace virtual_factory
