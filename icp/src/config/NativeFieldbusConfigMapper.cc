#include <virtual_factory/icp/config/NativeFieldbusConfigMapper.hh>

#include <string>

namespace virtual_factory
{
namespace icp
{

namespace
{

std::string toUpper(std::string value)
{
  for (char &ch : value)
  {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
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

bool parseProfinetValueType(
    const std::string &text,
    ProfinetValueType defaultType,
    ProfinetValueType *out,
    ConfigResult *result,
    const std::string &path)
{
  if (text.empty())
  {
    *out = defaultType;
    return true;
  }
  const std::string upper = toUpper(text);
  if (upper == "BOOL")
  {
    *out = ProfinetValueType::Bool;
    return true;
  }
  if (upper == "UINT8")
  {
    *out = ProfinetValueType::Uint8;
    return true;
  }
  if (upper == "INT16")
  {
    *out = ProfinetValueType::Int16;
    return true;
  }
  if (upper == "UINT16")
  {
    *out = ProfinetValueType::Uint16;
    return true;
  }
  if (upper == "INT32")
  {
    *out = ProfinetValueType::Int32;
    return true;
  }
  if (upper == "REAL" || upper == "FLOAT")
  {
    *out = ProfinetValueType::Real;
    return true;
  }
  addIssue(result, path, "unsupported PROFINET valueType '" + text + "'");
  return false;
}

bool parseProfibusValueType(
    const std::string &text,
    ProfibusValueType defaultType,
    ProfibusValueType *out,
    ConfigResult *result,
    const std::string &path)
{
  if (text.empty())
  {
    *out = defaultType;
    return true;
  }
  const std::string upper = toUpper(text);
  if (upper == "BOOL")
  {
    *out = ProfibusValueType::Bool;
    return true;
  }
  if (upper == "UINT8")
  {
    *out = ProfibusValueType::Uint8;
    return true;
  }
  if (upper == "INT16")
  {
    *out = ProfibusValueType::Int16;
    return true;
  }
  if (upper == "UINT16")
  {
    *out = ProfibusValueType::Uint16;
    return true;
  }
  if (upper == "INT32")
  {
    *out = ProfibusValueType::Int32;
    return true;
  }
  if (upper == "REAL" || upper == "FLOAT")
  {
    *out = ProfibusValueType::Real;
    return true;
  }
  addIssue(result, path, "unsupported PROFIBUS valueType '" + text + "'");
  return false;
}

}  // namespace

ConfigResult NativeFieldbusConfigMapper::toProfinet(
    const AdapterConfigRecord &record,
    ProfinetIndustrialAdapter::AdapterConfig *out)
{
  ConfigResult result;
  result.ok = true;
  if (out == nullptr)
  {
    addIssue(&result, "", "PROFINET AdapterConfig output is required");
    return result;
  }
  *out = {};
  if (record.protocol != "profinet")
  {
    addIssue(
        &result,
        "/protocol",
        "expected protocol 'profinet', got '" + record.protocol + "'");
    return result;
  }

  out->boardId = record.connection.boardId;
  out->channel = record.connection.channel;
  out->stationName = record.connection.stationName;
  if (out->stationName.empty())
  {
    out->stationName = record.connection.interfaceName;
  }
  out->configArtifactPath = record.connection.configArtifactPath;
  out->expectedFirmwareName = record.connection.expectedFirmwareName;
  out->pollTimeoutMs = record.connection.pollTimeoutMs;

  for (std::size_t e = 0; e < record.equipment.size(); ++e)
  {
    const EquipmentMappingRecord &eq = record.equipment[e];
    const std::string epath = "/equipment/" + std::to_string(e);
    ProfinetEquipmentMapping mapped;
    mapped.id = eq.equipmentId;
    mapped.type = eq.type;
    mapped.capabilities = eq.capabilities;
    mapped.device.stationName = eq.stationName;
    mapped.device.ipAddress = eq.ipAddress;
    mapped.device.vendorId = eq.vendorId;
    mapped.device.deviceId = eq.deviceId;
    for (const ProfinetSubmoduleRecord &sub : eq.submodules)
    {
      ProfinetSubslotMapping slot;
      slot.slot = sub.slot;
      slot.subslot = sub.subslot;
      slot.inputLength = sub.inputLength;
      slot.outputLength = sub.outputLength;
      mapped.device.subslots.push_back(slot);
    }
    for (std::size_t t = 0; t < eq.telemetry.size(); ++t)
    {
      const TelemetryMappingRecord &tel = eq.telemetry[t];
      ProfinetProcessMapping point;
      point.name = tel.name;
      point.unit = tel.unit;
      point.inputByteOffset = tel.inputByteOffset;
      point.outputByteOffset = tel.outputByteOffset;
      point.bitOffset = tel.bitOffset;
      if (!parseProfinetValueType(
              tel.valueType,
              ProfinetValueType::Int16,
              &point.valueType,
              &result,
              epath + "/telemetry/" + std::to_string(t) + "/valueType"))
      {
        continue;
      }
      mapped.telemetry.push_back(point);
    }
    for (std::size_t c = 0; c < eq.commands.size(); ++c)
    {
      const CommandMappingRecord &command = eq.commands[c];
      ProfinetCommandMapping mappedCommand;
      mappedCommand.command = command.command;
      mappedCommand.outputByteOffset = command.outputByteOffset;
      mappedCommand.bitOffset = command.bitOffset;
      if (!parseProfinetValueType(
              command.valueType,
              ProfinetValueType::Bool,
              &mappedCommand.valueType,
              &result,
              epath + "/commands/" + std::to_string(c) + "/valueType"))
      {
        continue;
      }
      mapped.commands.push_back(mappedCommand);
    }
    mapped.state.mapped = eq.state.mapped;
    mapped.state.inputByteOffset = eq.state.inputByteOffset;
    mapped.state.bitOffset = eq.state.bitOffset;
    parseProfinetValueType(
        eq.state.valueType,
        ProfinetValueType::Bool,
        &mapped.state.valueType,
        &result,
        epath + "/state/valueType");
    mapped.fault.mapped = eq.fault.mapped;
    mapped.fault.inputByteOffset = eq.fault.inputByteOffset;
    mapped.fault.bitOffset = eq.fault.bitOffset;
    parseProfinetValueType(
        eq.fault.valueType,
        ProfinetValueType::Bool,
        &mapped.fault.valueType,
        &result,
        epath + "/fault/valueType");
    out->equipment.push_back(mapped);
  }

  if (result.ok)
  {
    result.message = "ok";
  }
  return result;
}

ConfigResult NativeFieldbusConfigMapper::toProfibus(
    const AdapterConfigRecord &record,
    ProfibusIndustrialAdapter::AdapterConfig *out)
{
  ConfigResult result;
  result.ok = true;
  if (out == nullptr)
  {
    addIssue(&result, "", "PROFIBUS AdapterConfig output is required");
    return result;
  }
  *out = {};
  if (record.protocol != "profibus")
  {
    addIssue(
        &result,
        "/protocol",
        "expected protocol 'profibus', got '" + record.protocol + "'");
    return result;
  }

  out->boardId = record.connection.boardId;
  out->channel = record.connection.channel;
  out->masterAddress = record.connection.masterAddress;
  if (out->masterAddress == 0)
  {
    out->masterAddress = 1;
  }
  out->baudRateKbps = record.connection.baudRateKbps;
  out->configArtifactPath = record.connection.configArtifactPath;
  out->expectedFirmwareName = record.connection.expectedFirmwareName;
  out->pollTimeoutMs = record.connection.pollTimeoutMs;

  for (std::size_t e = 0; e < record.equipment.size(); ++e)
  {
    const EquipmentMappingRecord &eq = record.equipment[e];
    const std::string epath = "/equipment/" + std::to_string(e);
    ProfibusEquipmentMapping mapped;
    mapped.id = eq.equipmentId;
    mapped.type = eq.type;
    mapped.capabilities = eq.capabilities;
    mapped.slave.stationAddress = eq.stationAddress;
    mapped.slave.vendorId = eq.vendorId;
    mapped.slave.deviceId = eq.deviceId;
    for (const ProfibusModuleRecord &module : eq.modules)
    {
      ProfibusModuleMapping mappedModule;
      mappedModule.slot = module.slot;
      mappedModule.moduleType = module.ident;
      mappedModule.inputLength = module.inputLength;
      mappedModule.outputLength = module.outputLength;
      mapped.slave.modules.push_back(mappedModule);
    }
    for (std::size_t t = 0; t < eq.telemetry.size(); ++t)
    {
      const TelemetryMappingRecord &tel = eq.telemetry[t];
      ProfibusProcessMapping point;
      point.name = tel.name;
      point.unit = tel.unit;
      point.inputByteOffset = tel.inputByteOffset;
      point.outputByteOffset = tel.outputByteOffset;
      point.bitOffset = tel.bitOffset;
      if (!parseProfibusValueType(
              tel.valueType,
              ProfibusValueType::Int16,
              &point.valueType,
              &result,
              epath + "/telemetry/" + std::to_string(t) + "/valueType"))
      {
        continue;
      }
      mapped.telemetry.push_back(point);
    }
    for (std::size_t c = 0; c < eq.commands.size(); ++c)
    {
      const CommandMappingRecord &command = eq.commands[c];
      ProfibusCommandMapping mappedCommand;
      mappedCommand.command = command.command;
      mappedCommand.outputByteOffset = command.outputByteOffset;
      mappedCommand.bitOffset = command.bitOffset;
      if (!parseProfibusValueType(
              command.valueType,
              ProfibusValueType::Bool,
              &mappedCommand.valueType,
              &result,
              epath + "/commands/" + std::to_string(c) + "/valueType"))
      {
        continue;
      }
      mapped.commands.push_back(mappedCommand);
    }
    mapped.state.mapped = eq.state.mapped;
    mapped.state.inputByteOffset = eq.state.inputByteOffset;
    mapped.state.bitOffset = eq.state.bitOffset;
    parseProfibusValueType(
        eq.state.valueType,
        ProfibusValueType::Bool,
        &mapped.state.valueType,
        &result,
        epath + "/state/valueType");
    mapped.fault.mapped = eq.fault.mapped;
    mapped.fault.inputByteOffset = eq.fault.inputByteOffset;
    mapped.fault.bitOffset = eq.fault.bitOffset;
    parseProfibusValueType(
        eq.fault.valueType,
        ProfibusValueType::Bool,
        &mapped.fault.valueType,
        &result,
        epath + "/fault/valueType");
    out->equipment.push_back(mapped);
  }

  if (result.ok)
  {
    result.message = "ok";
  }
  return result;
}

}  // namespace icp
}  // namespace virtual_factory
