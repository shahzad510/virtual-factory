#ifndef VIRTUAL_FACTORY_PROFIBUS_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_PROFIBUS_INDUSTRIAL_ADAPTER_HH_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

enum class ProfibusValueType
{
  Bool,
  Uint8,
  Int16,
  Uint16,
  Int32,
  Real
};

struct ProfibusProcessMapping
{
  std::string name;
  ProfibusValueType valueType{ProfibusValueType::Int16};
  std::size_t inputByteOffset{0};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
  std::string unit;
};

struct ProfibusCommandMapping
{
  std::string command;
  ProfibusValueType valueType{ProfibusValueType::Bool};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
};

struct ProfibusSignalMapping
{
  ProfibusValueType valueType{ProfibusValueType::Bool};
  std::size_t inputByteOffset{0};
  std::size_t bitOffset{0};
  bool mapped{false};
};

/// Module layout is ICP mapping metadata. GSD interpretation is applied by
/// DP Master firmware from the SYCON artifact — not invented here.
struct ProfibusModuleMapping
{
  unsigned slot{0};
  std::string moduleType;
};

struct ProfibusSlaveMapping
{
  unsigned stationAddress{0};
  std::uint16_t vendorId{0};
  std::uint16_t deviceId{0};
  std::vector<ProfibusModuleMapping> modules;
};

struct ProfibusEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  ProfibusSlaveMapping slave;
  std::vector<ProfibusCommandMapping> commands;
  std::vector<ProfibusProcessMapping> telemetry;
  ProfibusSignalMapping state;
  ProfibusSignalMapping fault;
};

/// Native PROFIBUS DP Master adapter (Hilscher cifX).
///
/// One adapter instance = one DP Master → many DP slaves.
///
/// **Status:** IMPLEMENTED TO SOFTWARE BOUNDARY. Cyclic DP IO, slave
/// diagnostics, and recovery require CIFX 50E-DP + DPM firmware/license +
/// real DP slave. HARDWARE VALIDATION PENDING.
///
/// Gateway PROFIBUS remains SUPPORTED.
class ProfibusIndustrialAdapter : public IndustrialAdapter
{
public:
  struct AdapterConfig
  {
    std::string boardId;
    unsigned channel{0};
    unsigned masterAddress{1};
    unsigned baudRateKbps{19200};
    std::string configArtifactPath;
    int pollTimeoutMs{0};
    std::vector<ProfibusEquipmentMapping> equipment;
  };

  ProfibusIndustrialAdapter(std::string id, AdapterConfig config);
  ~ProfibusIndustrialAdapter() override;

  ProfibusIndustrialAdapter(const ProfibusIndustrialAdapter &) = delete;
  ProfibusIndustrialAdapter &operator=(const ProfibusIndustrialAdapter &) = delete;

  std::string id() const override;
  std::string protocol() const override;
  ConnectionState connectionState() const override;
  std::string lastError() const override;

  bool connect() override;
  void disconnect() override;

  std::vector<Equipment *> equipment() override;
  Equipment *equipmentById(const std::string &id) override;

  void poll() override;

  bool hilscherSdkPresent() const;

  const AdapterConfig &config() const;

private:
  class BoundEquipment;
  friend class BoundEquipment;

  struct SessionHandle;

  void bindEquipment();
  void enterFault(const std::string &reason);
  int operationTimeoutMs() const;
  bool writeMappedCommand(
      const ProfibusCommandMapping &mapped, double parameter, std::string *error);
  std::size_t requiredInputBytes() const;
  std::size_t requiredOutputBytes() const;

  std::string id_;
  AdapterConfig config_;
  ConnectionState connection_state_{ConnectionState::Disconnected};
  std::string last_error_;
  std::unique_ptr<SessionHandle> session_;
  std::vector<std::unique_ptr<BoundEquipment>> bound_;
  std::vector<std::uint8_t> output_image_;
};

}  // namespace virtual_factory

#endif
