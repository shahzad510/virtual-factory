#ifndef VIRTUAL_FACTORY_MODBUS_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_MODBUS_INDUSTRIAL_ADAPTER_HH_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// Modbus data table. Addresses are 0-based protocol addresses as used by
/// the wire format (and by libmodbus). MES/SCADA must not consume these;
/// they belong in adapter config.
enum class ModbusTable
{
  Coil,
  DiscreteInput,
  HoldingRegister,
  InputRegister
};

struct ModbusRef
{
  std::uint8_t unitId{1};
  ModbusTable table{ModbusTable::HoldingRegister};
  std::uint16_t address{0};
  /// Optional mappings (state/fault) are ignored when false.
  bool mapped{false};
};

inline ModbusRef makeModbusRef(
    std::uint8_t unitId, ModbusTable table, std::uint16_t address)
{
  return ModbusRef{unitId, table, address, true};
}

struct ModbusCommandMapping
{
  std::string command;
  ModbusRef target;
};

struct ModbusTelemetryMapping
{
  std::string name;
  ModbusRef source;
  std::string unit;
};

/// One machine as seen through Modbus. Type is metadata, not a C++ class.
struct ModbusEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<ModbusCommandMapping> commands;
  std::vector<ModbusTelemetryMapping> telemetry;
  ModbusRef stateCoil;
  ModbusRef faultCoil;
};

/// Config for one Modbus TCP endpoint (one TCP session).
/// One adapter instance = one host:port (ADR-026 analogue).
struct ModbusAdapterConfig
{
  std::string host{"127.0.0.1"};
  std::uint16_t port{502};
  int timeoutMs{2000};
  std::vector<ModbusEquipmentMapping> equipment;
};

/// Production Modbus TCP adapter (SoT Phase 6).
///
/// Translates configured coils/registers into the normalized Equipment model.
/// Does not expose Modbus addresses, function codes, or client types through
/// Equipment.
///
/// One instance owns one TCP session to one endpoint. A factory with N Modbus
/// TCP devices uses N adapter instances. Independent connect/poll/fault/
/// reconnect; IndustrialAdapter::connectionState() stays per-source.
///
/// Commands named `set_*` write the execute() double as a holding-register
/// uint16 (or coil true/false). Other commands write coil `true` (0xFF00) or
/// register 1. Discrete inputs and input registers are read-only.
///
/// `connect()` after Faulted recreates the client session. Background
/// auto-reconnect is not implemented.
class ModbusIndustrialAdapter : public IndustrialAdapter
{
public:
  ModbusIndustrialAdapter(std::string id, ModbusAdapterConfig config);
  ~ModbusIndustrialAdapter() override;

  ModbusIndustrialAdapter(const ModbusIndustrialAdapter &) = delete;
  ModbusIndustrialAdapter &operator=(const ModbusIndustrialAdapter &) = delete;

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
  bool readBoolean(const ModbusRef &ref, bool *value);
  bool readDouble(const ModbusRef &ref, double *value);
  bool writeBoolean(const ModbusRef &ref, bool value);
  bool writeDouble(const ModbusRef &ref, double value);

  std::string id_;
  ModbusAdapterConfig config_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<ClientHandle> client_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
};

}  // namespace virtual_factory

#endif
