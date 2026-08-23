#ifndef VIRTUAL_FACTORY_MOCK_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_MOCK_INDUSTRIAL_ADAPTER_HH_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// In-process simulation of an external industrial source (SoT Phase 6).
///
/// Proves the adapter architecture without OPC UA, Modbus, REST, or hardware.
/// Production protocol adapters are not implemented by this class.
///
/// MES/SCADA-facing code should depend on IndustrialAdapter / Equipment, not
/// on this mock type.
class MockIndustrialAdapter : public IndustrialAdapter
{
public:
  explicit MockIndustrialAdapter(std::string id);
  ~MockIndustrialAdapter() override;

  MockIndustrialAdapter(const MockIndustrialAdapter &) = delete;
  MockIndustrialAdapter &operator=(const MockIndustrialAdapter &) = delete;

  void addDevice(std::string equipmentId, std::string type);
  void addCapability(const std::string &equipmentId, std::string capability);
  void setSourceTelemetry(
      const std::string &equipmentId,
      std::string name,
      double value,
      std::string unit);

  void simulateCommunicationFailure(std::string reason);
  void clearCommunicationFailure();

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

  struct DeviceConfig
  {
    std::string id;
    std::string type;
    std::vector<std::string> capabilities;
    std::map<std::string, TelemetryPoint> sourceTelemetry;
  };

  DeviceConfig *findConfig(const std::string &equipmentId);
  const DeviceConfig *findConfig(const std::string &equipmentId) const;
  void bindDevices();
  void writeSourceTelemetry(
      const std::string &equipmentId,
      const std::string &name,
      double value,
      const std::string &unit);

  std::string id_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::vector<DeviceConfig> devices_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
};

}  // namespace virtual_factory

#endif
