#ifndef VIRTUAL_FACTORY_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_INDUSTRIAL_ADAPTER_HH_

#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>

namespace virtual_factory
{

/// Lifecycle of a protocol adapter's link to an industrial source.
/// Distinct from Equipment::fault(), which is a machine fault.
enum class ConnectionState
{
  Disconnected,
  Connected,
  Faulted
};

/// Gazebo-independent industrial adapter contract (SoT Phase 6).
///
/// Adapters translate a protocol/source into the normalized Equipment model.
/// MES/SCADA consume Equipment (and this adapter's connection state), not
/// OPC UA nodes, Modbus registers, REST payloads, vendor SDKs, or Gazebo ECM.
///
/// Protocol-oriented, not vendor- or machine-class-oriented. Do not create
/// SiemensAdapter / AllenBradleyAdapter / RobotAdapter unless a protocol
/// truly requires it. OPC UA is implemented; Modbus and REST come later.
///
/// Must not include Gazebo, open62541, libmodbus, HTTP libraries, or vendor SDKs.
///
/// One instance is one industrial source (one OPC UA server, one mock bus,
/// later one Modbus link). A factory with several PLCs uses several adapter
/// instances so connectionState() remains per-source (ADR-026).
class IndustrialAdapter
{
public:
  virtual ~IndustrialAdapter() = default;

  virtual std::string id() const = 0;

  /// Protocol family metadata, e.g. "mock", "opcua"; later "modbus", "rest".
  /// Not a closed enum and not a machine type.
  virtual std::string protocol() const = 0;

  virtual ConnectionState connectionState() const = 0;
  virtual std::string lastError() const = 0;

  bool connected() const
  {
    return this->connectionState() == ConnectionState::Connected;
  }

  virtual bool connect() = 0;
  virtual void disconnect() = 0;

  /// Normalized equipment exposed by this adapter.
  /// Empty while Disconnected. Last-known instances remain while Faulted.
  virtual std::vector<Equipment *> equipment() = 0;
  virtual Equipment *equipmentById(const std::string &id) = 0;

  /// Pull latest source values into the normalized Equipment objects.
  virtual void poll() = 0;
};

}  // namespace virtual_factory

#endif
