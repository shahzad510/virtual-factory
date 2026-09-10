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

enum class ModbusTransport
{
  Tcp,
  Rtu
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

/// Config for one Modbus endpoint (one TCP session or one serial RTU session).
/// One adapter instance = one transport session (ADR-026 analogue).
struct ModbusAdapterConfig
{
  ModbusTransport transport{ModbusTransport::Tcp};

  /// TCP
  std::string host{"127.0.0.1"};
  std::uint16_t port{502};

  /// RTU / RS-485 serial
  std::string serialDevice;
  int baudRate{9600};
  char parity{'N'};  ///< 'N' | 'E' | 'O'
  int dataBits{8};
  int stopBits{1};

  int timeoutMs{2000};
  /// Used for RTU link verification when no equipment mappings exist.
  std::uint8_t linkUnitId{1};

  std::vector<ModbusEquipmentMapping> equipment;
};

/// Production Modbus adapter (TCP and RTU / RS-485).
///
/// Translates configured coils/registers into the normalized Equipment model.
/// Does not expose Modbus addresses, function codes, or client types through
/// Equipment.
///
/// One instance owns one transport session. A factory with N Modbus devices
/// uses N adapter instances. Independent connect/poll/fault/reconnect;
/// IndustrialAdapter::connectionState() stays per-source.
///
/// Commands named `set_*` write the execute() double as a holding-register
/// uint16 (or coil true/false). Other commands write coil `true` (0xFF00) or
/// register 1. Discrete inputs and input registers are read-only.
///
/// RTU `connect()` opens the serial port and verifies Modbus framing with a
/// lightweight probe (mapped register preferred; otherwise holding 0 on
/// linkUnitId). A Modbus exception response counts as link OK; timeout does not.
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

  const ModbusAdapterConfig &config() const
  {
    return this->config_;
  }

private:
  class BoundEquipment;
  friend class BoundEquipment;

  struct ClientHandle;

  void bindEquipment();
  void enterFault(const std::string &reason);
  bool openSession();
  bool verifyRtuLink();
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
