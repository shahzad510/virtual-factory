#ifndef VIRTUAL_FACTORY_ETHERNETIP_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_ETHERNETIP_INDUSTRIAL_ADAPTER_HH_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

/// How a Logix/CIP tag value is represented for telemetry or command writes.
enum class EtherNetIpValueType
{
  Bool,
  Dint,
  Real
};

struct EtherNetIpCommandMapping
{
  std::string command;
  std::string tag;
  EtherNetIpValueType valueType{EtherNetIpValueType::Bool};
};

struct EtherNetIpTelemetryMapping
{
  std::string name;
  std::string tag;
  EtherNetIpValueType valueType{EtherNetIpValueType::Dint};
  std::string unit;
};

struct EtherNetIpSignalMapping
{
  std::string tag;
  EtherNetIpValueType valueType{EtherNetIpValueType::Bool};
  bool mapped{false};
};

inline EtherNetIpSignalMapping makeEtherNetIpSignal(
    std::string tag,
    EtherNetIpValueType valueType = EtherNetIpValueType::Bool)
{
  EtherNetIpSignalMapping signal;
  signal.tag = std::move(tag);
  signal.valueType = valueType;
  signal.mapped = true;
  return signal;
}

struct EtherNetIpEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  std::vector<EtherNetIpCommandMapping> commands;
  std::vector<EtherNetIpTelemetryMapping> telemetry;
  EtherNetIpSignalMapping state;
  EtherNetIpSignalMapping fault;
};

struct EtherNetIpAdapterConfig
{
  std::string host{"127.0.0.1"};
  std::uint16_t port{44818};
  std::string path{"1,0"};
  std::string plcType{"ControlLogix"};
  int timeoutMs{2000};
  int pollTimeoutMs{0};
  std::vector<EtherNetIpEquipmentMapping> equipment;
};

/// Production EtherNet/IP adapter (SoT Phase 6 slice 6G, ADR-039).
/// Explicit CIP tag messaging via private libplctag session. Class 1 / implicit
/// I/O is NOT IMPLEMENTED.
class EtherNetIpIndustrialAdapter : public IndustrialAdapter
{
public:
  EtherNetIpIndustrialAdapter(std::string id, EtherNetIpAdapterConfig config);
  ~EtherNetIpIndustrialAdapter() override;

  EtherNetIpIndustrialAdapter(const EtherNetIpIndustrialAdapter &) = delete;
  EtherNetIpIndustrialAdapter &operator=(
      const EtherNetIpIndustrialAdapter &) = delete;

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
  int operationTimeoutMs() const;
  bool createMappedTags();
  bool readDouble(
      const std::string &tagKey,
      EtherNetIpValueType valueType,
      double *value);
  bool readBoolean(
      const std::string &tagKey,
      EtherNetIpValueType valueType,
      bool *value);
  bool writeDouble(
      const std::string &tagKey,
      EtherNetIpValueType valueType,
      double value);
  bool writeBoolean(
      const std::string &tagKey,
      EtherNetIpValueType valueType,
      bool value);
  static std::string tagKey(
      const std::string &equipmentId, const std::string &role, std::size_t index);

  std::string id_;
  EtherNetIpAdapterConfig config_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<ClientHandle> client_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
};

}  // namespace virtual_factory

#endif
