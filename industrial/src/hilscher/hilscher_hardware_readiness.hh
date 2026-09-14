#ifndef VIRTUAL_FACTORY_HILSCHER_HARDWARE_READINESS_HH_
#define VIRTUAL_FACTORY_HILSCHER_HARDWARE_READINESS_HH_

#include <cstdint>
#include <string>
#include <vector>

#include "cifx_runtime.hh"

namespace virtual_factory
{
namespace internal
{

/// Pre-flight states for native Hilscher hardware validation.
/// Values are derived only from official cifX host APIs + build flags.
/// Protocol readiness (DCP/AR/cyclic plant IO) is never claimed here.
enum class HilscherReadinessState
{
  SdkMissing,
  DriverInitFailed,
  NoBoard,
  NoChannel,
  WrongFirmware,
  FirmwareNotReady,
  /// License bits may be readable; master-license validity needs SYCON/vendor tools.
  LicenseManualVerificationRequired,
  /// Board + matching channel firmware present. Still not a REAL fieldbus test.
  ReadyForHostIoTest,
  ManualVerificationRequired
};

enum class HilscherProtocolTarget
{
  Any,
  Profinet,
  Profibus
};

struct HilscherReadinessRequest
{
  HilscherProtocolTarget protocol{HilscherProtocolTarget::Any};
  /// Optional preferred board name/alias (e.g. "cifx0"). Empty = first match.
  std::string boardId;
  unsigned channel{0};
  /// Optional firmware substring; empty uses protocol defaults (PROFINET/PNM or PROFIBUS/DPM).
  std::string expectedFirmwareName;
};

struct HilscherChannelProbe
{
  bool opened{false};
  std::string openError;
  CifxChannelInfo info;
};

struct HilscherReadinessReport
{
  HilscherReadinessState state{HilscherReadinessState::SdkMissing};
  std::string stateLabel;
  std::string summary;
  bool sdkCompiledIn{false};
  bool driverInitialized{false};
  std::string driverVersion;
  std::string driverError;
  CifxInventory inventory;
  bool selectedBoardFound{false};
  std::string selectedBoardName;
  std::string selectedBoardAlias;
  std::uint32_t selectedSerialNumber{0};
  std::uint32_t selectedDeviceNumber{0};
  std::uint32_t selectedLicenseFlags1{0};
  std::uint32_t selectedLicenseFlags2{0};
  std::uint16_t selectedNetxLicenseId{0};
  bool selectedChannelFound{false};
  std::string selectedFirmwareName;
  HilscherChannelProbe channelProbe;
  std::vector<std::string> notes;
  std::vector<std::string> manualChecks;
};

/// Human-readable readiness state for logs and hardware reports.
const char *hilscherReadinessStateLabel(HilscherReadinessState state);

/// Enumerate cifX and classify readiness for optional PN/PB hardware tests.
/// Does not invent DCP/AR/DP packets. Does not claim plant IO works.
HilscherReadinessReport assessHilscherHardwareReadiness(
    const HilscherReadinessRequest &request = {});

/// Multi-line diagnostic text for operators connecting CIFX hardware.
std::string formatHilscherReadinessReport(const HilscherReadinessReport &report);

}  // namespace internal
}  // namespace virtual_factory

#endif
