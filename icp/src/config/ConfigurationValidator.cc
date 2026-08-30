#include <virtual_factory/icp/config/ConfigurationValidator.hh>

#include <sstream>
#include <unordered_set>

namespace virtual_factory
{
namespace icp
{

namespace
{

void addIssue(
    ConfigResult *result, const std::string &path, const std::string &message)
{
  result->ok = false;
  result->issues.push_back({path, message});
  if (result->message.empty())
  {
    result->message = message;
  }
  else
  {
    result->message += "; " + message;
  }
}

bool looksLikePlainSecret(const std::string &ref)
{
  if (ref.empty())
  {
    return false;
  }
  // Allowed: env:NAME, file:/path, secret:NAME
  return ref.find(':') == std::string::npos;
}

std::string adapterPath(std::size_t index)
{
  std::ostringstream stream;
  stream << "/adapters/" << index;
  return stream.str();
}

}  // namespace

bool ConfigurationValidator::isSupportedProtocol(const std::string &protocol)
{
  return protocol == "mock" || protocol == "opcua" || protocol == "modbus"
         || protocol == "mqtt" || protocol == "rest" || protocol == "ethernetip"
         || protocol == "profinet" || protocol == "profibus";
}

ConfigResult ConfigurationValidator::validateAdapter(
    const AdapterConfigRecord &adapter)
{
  IcpConfigurationDocument document;
  document.adapters.push_back(adapter);
  return validate(document);
}

ConfigResult ConfigurationValidator::validate(
    const IcpConfigurationDocument &document)
{
  ConfigResult result;
  result.ok = true;
  result.message = "ok";

  if (document.schema != IcpConfigurationDocument::kSchemaId)
  {
    addIssue(
        &result,
        "/schema",
        "unsupported schema '" + document.schema + "'; expected "
            + IcpConfigurationDocument::kSchemaId);
  }
  if (document.version != IcpConfigurationDocument::kCurrentVersion)
  {
    addIssue(
        &result,
        "/version",
        "unsupported configuration version "
            + std::to_string(document.version) + " (supported: "
            + std::to_string(IcpConfigurationDocument::kCurrentVersion) + ")");
  }

  std::unordered_set<std::string> adapterIds;
  std::unordered_set<std::string> equipmentIds;

  for (std::size_t i = 0; i < document.adapters.size(); ++i)
  {
    const AdapterConfigRecord &adapter = document.adapters[i];
    const std::string base = adapterPath(i);

    if (adapter.adapterId.empty())
    {
      addIssue(&result, base + "/adapterId", "adapterId is required");
    }
    else if (!adapterIds.insert(adapter.adapterId).second)
    {
      addIssue(
          &result,
          base + "/adapterId",
          "duplicate adapterId '" + adapter.adapterId + "'");
    }

    if (!isSupportedProtocol(adapter.protocol))
    {
      addIssue(
          &result,
          base + "/protocol",
          "invalid protocol '" + adapter.protocol
              + "'; expected mock, opcua, modbus, mqtt, rest, ethernetip, "
                "profinet, or profibus");
    }

    if (looksLikePlainSecret(adapter.credentials.passwordRef))
    {
      addIssue(
          &result,
          base + "/credentials/passwordRef",
          "passwordRef must be a reference (env:NAME, file:PATH, or secret:NAME), "
          "not a plaintext password");
    }
    if (looksLikePlainSecret(adapter.credentials.tokenRef))
    {
      addIssue(
          &result,
          base + "/credentials/tokenRef",
          "tokenRef must be a reference (env:NAME, file:PATH, or secret:NAME), "
          "not a plaintext token");
    }

    const AdapterConnectionRecord &c = adapter.connection;
    if (adapter.protocol == "opcua")
    {
      if (c.endpointUrl.empty())
      {
        addIssue(
            &result,
            base + "/connection/endpointUrl",
            "OPC UA endpointUrl is required");
      }
    }
    else if (adapter.protocol == "modbus")
    {
      if (c.host.empty())
      {
        addIssue(&result, base + "/connection/host", "Modbus host is required");
      }
      if (c.port == 0)
      {
        addIssue(&result, base + "/connection/port", "Modbus port is required");
      }
    }
    else if (adapter.protocol == "mqtt")
    {
      if (c.host.empty())
      {
        addIssue(&result, base + "/connection/host", "MQTT host is required");
      }
      if (c.port == 0)
      {
        addIssue(&result, base + "/connection/port", "MQTT port is required");
      }
    }
    else if (adapter.protocol == "rest")
    {
      if (c.host.empty())
      {
        addIssue(&result, base + "/connection/host", "REST host is required");
      }
      if (c.port == 0)
      {
        addIssue(&result, base + "/connection/port", "REST port is required");
      }
      if (c.scheme.empty())
      {
        addIssue(
            &result, base + "/connection/scheme", "REST scheme is required");
      }
    }
    else if (adapter.protocol == "ethernetip")
    {
      if (c.host.empty())
      {
        addIssue(
            &result, base + "/connection/host", "EtherNet/IP host is required");
      }
      if (c.port == 0)
      {
        addIssue(
            &result, base + "/connection/port", "EtherNet/IP port is required");
      }
    }
    else if (adapter.protocol == "profinet")
    {
      if (c.boardId.empty())
      {
        addIssue(
            &result,
            base + "/connection/boardId",
            "PROFINET boardId is required");
      }
    }
    else if (adapter.protocol == "profibus")
    {
      if (c.boardId.empty())
      {
        addIssue(
            &result,
            base + "/connection/boardId",
            "PROFIBUS boardId is required");
      }
      if (c.baudRateKbps == 0)
      {
        addIssue(
            &result,
            base + "/connection/baudRateKbps",
            "PROFIBUS baudRateKbps is required");
      }
    }

    std::unordered_set<std::string> localEquipment;
    for (std::size_t e = 0; e < adapter.equipment.size(); ++e)
    {
      const EquipmentMappingRecord &eq = adapter.equipment[e];
      const std::string epath = base + "/equipment/" + std::to_string(e);
      if (eq.equipmentId.empty())
      {
        addIssue(&result, epath + "/equipmentId", "equipmentId is required");
        continue;
      }
      if (!localEquipment.insert(eq.equipmentId).second)
      {
        addIssue(
            &result,
            epath + "/equipmentId",
            "duplicate equipmentId '" + eq.equipmentId
                + "' on adapter '" + adapter.adapterId + "'");
      }
      if (!equipmentIds.insert(eq.equipmentId).second)
      {
        addIssue(
            &result,
            epath + "/equipmentId",
            "duplicate equipmentId '" + eq.equipmentId
                + "' across adapters");
      }
      if (eq.type.empty())
      {
        addIssue(&result, epath + "/type", "equipment type is required");
      }

      if (!eq.adapterId.empty() && eq.adapterId != adapter.adapterId)
      {
        addIssue(
            &result,
            epath + "/adapterId",
            "equipment.adapterId '" + eq.adapterId
                + "' does not match adapter '" + adapter.adapterId + "'");
      }

      std::unordered_set<std::string> telemetryNames;
      for (std::size_t t = 0; t < eq.telemetry.size(); ++t)
      {
        const TelemetryMappingRecord &tel = eq.telemetry[t];
        const std::string tpath = epath + "/telemetry/" + std::to_string(t);
        if (tel.name.empty())
        {
          addIssue(&result, tpath + "/name", "telemetry name is required");
        }
        else if (!telemetryNames.insert(tel.name).second)
        {
          addIssue(
              &result,
              tpath + "/name",
              "duplicate telemetry name '" + tel.name + "'");
        }
        if (adapter.protocol == "opcua" && tel.address.empty())
        {
          addIssue(
              &result, tpath + "/address", "OPC UA telemetry address (NodeId) is required");
        }
        if (adapter.protocol == "mqtt" && tel.address.empty())
        {
          addIssue(&result, tpath + "/address", "MQTT telemetry topic is required");
        }
        if (adapter.protocol == "ethernetip" && tel.address.empty())
        {
          addIssue(
              &result, tpath + "/address", "EtherNet/IP telemetry tag is required");
        }
        if (adapter.protocol == "modbus" && tel.address.empty()
            && tel.registerAddress == 0 && tel.table.empty())
        {
          addIssue(
              &result,
              tpath,
              "Modbus telemetry requires address, table, or registerAddress");
        }
        if (adapter.protocol == "rest" && tel.address.empty()
            && tel.jsonPointer.empty())
        {
          addIssue(
              &result,
              tpath,
              "REST telemetry requires jsonPointer or address");
        }
      }
      std::unordered_set<std::string> commandNames;
      for (std::size_t cmd = 0; cmd < eq.commands.size(); ++cmd)
      {
        const CommandMappingRecord &command = eq.commands[cmd];
        const std::string cpath = epath + "/commands/" + std::to_string(cmd);
        if (command.command.empty())
        {
          addIssue(&result, cpath + "/command", "command name is required");
        }
        else if (!commandNames.insert(command.command).second)
        {
          addIssue(
              &result,
              cpath + "/command",
              "duplicate command name '" + command.command + "'");
        }
      }

      if (adapter.protocol == "profinet" && eq.stationName.empty())
      {
        addIssue(
            &result,
            epath + "/stationName",
            "PROFINET stationName is required");
      }
      if (adapter.protocol == "profibus"
          && (eq.stationAddress == 0 || eq.stationAddress > 126))
      {
        addIssue(
            &result,
            epath + "/stationAddress",
            "PROFIBUS stationAddress must be 1-126");
      }
    }
  }

  if (result.ok)
  {
    result.message = "ok";
  }
  return result;
}

}  // namespace icp
}  // namespace virtual_factory
