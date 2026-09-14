#ifndef VIRTUAL_FACTORY_PROFINET_INDUSTRIAL_ADAPTER_HH_
#define VIRTUAL_FACTORY_PROFINET_INDUSTRIAL_ADAPTER_HH_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <virtual_factory/equipment/Equipment.hh>
#include <virtual_factory/industrial/IndustrialAdapter.hh>

namespace virtual_factory
{

enum class ProfinetValueType
{
  Bool,
  Uint8,
  Int16,
  Uint16,
  Int32,
  Real
};

struct ProfinetProcessMapping
{
  std::string name;
  ProfinetValueType valueType{ProfinetValueType::Int16};
  std::size_t inputByteOffset{0};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
  std::string unit;
};

struct ProfinetCommandMapping
{
  std::string command;
  ProfinetValueType valueType{ProfinetValueType::Bool};
  std::size_t outputByteOffset{0};
  std::size_t bitOffset{0};
};

struct ProfinetSignalMapping
{
  ProfinetValueType valueType{ProfinetValueType::Bool};
  std::size_t inputByteOffset{0};
  std::size_t bitOffset{0};
  bool mapped{false};
};

/// Slot/subslot layout is ICP mapping metadata. GSDML interpretation and
/// DCP/AR addressing are applied by NXLFW-PNM from the SYCON artifact.
struct ProfinetSubslotMapping
{
  std::uint16_t slot{0};
  std::uint16_t subslot{1};
  std::string moduleType;
  std::size_t inputLength{0};
  std::size_t outputLength{0};
};

struct ProfinetIoDeviceMapping
{
  std::string stationName;
  std::string ipAddress;
  std::uint16_t vendorId{0};
  std::uint16_t deviceId{0};
  std::vector<ProfinetSubslotMapping> subslots;
};

struct ProfinetEquipmentMapping
{
  std::string id;
  std::string type;
  std::vector<std::string> capabilities;
  ProfinetIoDeviceMapping device;
  std::vector<ProfinetCommandMapping> commands;
  std::vector<ProfinetProcessMapping> telemetry;
  ProfinetSignalMapping state;
  ProfinetSignalMapping fault;
};

/// Native PROFINET IO-Controller adapter (Hilscher cifX).
///
/// One adapter instance = one PROFINET controller / network segment → many
/// IO-Devices mapped to GenericEquipment.
///
/// **Status:** IMPLEMENTED TO SOFTWARE BOUNDARY. Cyclic IO, DCP, AR, and
/// device diagnostics require CIFX 50E-RE + NXLFW-PNM + NXLIC-MASTER +
/// real/software IO-Device. HARDWARE VALIDATION PENDING.
///
/// Gateway PROFINET remains SUPPORTED.
class ProfinetIndustrialAdapter : public IndustrialAdapter
{
public:
  struct AdapterConfig
  {
    std::string boardId;
    unsigned channel{0};
    /// Controller/interface station name (ICP metadata; encoded in SYCON).
    std::string stationName;
    std::string configArtifactPath;
    /// Optional firmware name substring (e.g. "PROFINET"); empty = default check.
    std::string expectedFirmwareName;
    int pollTimeoutMs{0};
    std::vector<ProfinetEquipmentMapping> equipment;
  };

  ProfinetIndustrialAdapter(std::string id, AdapterConfig config);
  ~ProfinetIndustrialAdapter() override;

  ProfinetIndustrialAdapter(const ProfinetIndustrialAdapter &) = delete;
  ProfinetIndustrialAdapter &operator=(const ProfinetIndustrialAdapter &) = delete;

  std::string id() const override;
  std::string protocol() const override;
  ConnectionState connectionState() const override;
  std::string lastError() const override;

  bool connect() override;
  void disconnect() override;

  std::vector<Equipment *> equipment() override;
  Equipment *equipmentById(const std::string &id) override;

  void poll() override;

  /// True when this build compiled the real cifX backend (flags ON + SDK).
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
      const ProfinetCommandMapping &mapped, double parameter, std::string *error);
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
