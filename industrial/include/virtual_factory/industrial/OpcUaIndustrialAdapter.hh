#ifndef VIRTUAL_FACTORY_OPCUA_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_OPCUA_INDUSTRIAL_ADAPTER_HH_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// String NodeId in a given OPC UA namespace (ns=N;s=Identifier).
/// MES/SCADA must not construct or consume these; they belong in adapter config.
struct OpcUaNodeRef
{
  std::uint16_t namespaceIndex{1};
  std::string identifier;
};

/// Map ICP config fields into an OpcUaNodeRef.
///
/// ICP configuration stores OPC UA addresses as NodeId text in `address`
/// (see configuration tests: "ns=1;s=Mixer.SpeedActual") plus optional
/// `namespaceIndex`. The open62541 string NodeId constructor expects the
/// bare identifier only. If `address` is already an expanded string NodeId
/// (`ns=N;s=Ident`), this splits it; otherwise `address` is treated as the
/// identifier and `namespaceIndex` is used as-is.
OpcUaNodeRef opcUaNodeRefFromConfig(
    std::uint16_t namespaceIndex, const std::string &address);

/// Named command → node write. Boolean pulse commands write `true`.
/// Commands whose names start with `set_` write the execute() double parameter.
struct OpcUaCommandMapping
{
  std::string command;
  OpcUaNodeRef node;
};

/// Named telemetry point → node read.
struct OpcUaTelemetryMapping
{
  std::string name;
  OpcUaNodeRef node;
  std::string unit;
};

/// One machine as seen through OPC UA. Type is metadata, not a C++ class.
struct OpcUaEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<OpcUaCommandMapping> commands;
  std::vector<OpcUaTelemetryMapping> telemetry;
  OpcUaNodeRef stateNode;
  OpcUaNodeRef faultNode;
};

/// Config for one OPC UA server (one endpoint). Not a general-purpose
/// configuration framework. One adapter instance = one server (ADR-026).
struct OpcUaAdapterConfig
{
  /// e.g. opc.tcp://192.168.1.10:4840 — this adapter's single endpoint
  std::string endpointUrl;
  std::vector<OpcUaEquipmentMapping> equipment;
};

/// Production OPC UA adapter (SoT Phase 6, open62541).
///
/// Translates configured nodes into the normalized Equipment model.
/// Does not expose NodeIds, namespaces, or open62541 types through Equipment.
///
/// One instance owns one UA_Client and one endpoint (one PLC / OPC UA server).
/// A factory with N servers uses N adapter instances. Independent
/// connect/poll/fault/reconnect; IndustrialAdapter::connectionState() stays
/// meaningful (ADR-026). Do not put multiple endpoints in one config.
///
/// First milestone security: anonymous, SecurityPolicy#None. That is
/// DEVELOPMENT ONLY and is not production industrial security (see ADR-025).
class OpcUaIndustrialAdapter : public IndustrialAdapter
{
public:
  OpcUaIndustrialAdapter(std::string id, OpcUaAdapterConfig config);
  ~OpcUaIndustrialAdapter() override;

  OpcUaIndustrialAdapter(const OpcUaIndustrialAdapter &) = delete;
  OpcUaIndustrialAdapter &operator=(const OpcUaIndustrialAdapter &) = delete;

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
  bool readBoolean(const OpcUaNodeRef &node, bool *value);
  bool readDouble(const OpcUaNodeRef &node, double *value);
  bool writeBoolean(const OpcUaNodeRef &node, bool value);
  bool writeDouble(const OpcUaNodeRef &node, double value);

  std::string id_;
  OpcUaAdapterConfig config_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<ClientHandle> client_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;

  /// Temporary poll diagnostics context (set by BoundEquipment::refreshFromServer).
  std::string debug_equipment_id_;
  std::string debug_point_name_;
};

}  // namespace virtual_factory

#endif
