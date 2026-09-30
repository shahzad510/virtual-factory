#include "hilscher_hardware_test_harness.hh"

#include <sstream>

namespace virtual_factory
{
namespace internal
{

namespace
{

HardwareTestStep step(
    const char *id,
    const char *title,
    HardwareTestStepStatus status,
    const std::string &detail)
{
  HardwareTestStep out;
  out.id = id;
  out.title = title;
  out.status = status;
  out.detail = detail;
  return out;
}

HardwareTestStepStatus hostStepStatus(const HilscherReadinessReport &readiness)
{
  if (!readiness.sdkCompiledIn)
  {
    return HardwareTestStepStatus::SkippedNoHardware;
  }
  if (readiness.state == HilscherReadinessState::ReadyForHostIoTest)
  {
    return HardwareTestStepStatus::PassedSoftwareBoundary;
  }
  if (readiness.state == HilscherReadinessState::SdkMissing
      || readiness.state == HilscherReadinessState::DriverInitFailed
      || readiness.state == HilscherReadinessState::NoBoard
      || readiness.state == HilscherReadinessState::NoChannel
      || readiness.state == HilscherReadinessState::WrongFirmware
      || readiness.state == HilscherReadinessState::FirmwareNotReady)
  {
    return HardwareTestStepStatus::SkippedNoHardware;
  }
  return HardwareTestStepStatus::ManualVerificationRequired;
}

std::string readinessDetail(const HilscherReadinessReport &readiness)
{
  return readiness.stateLabel + ": " + readiness.summary;
}

}  // namespace

const char *hardwareTestStepStatusLabel(HardwareTestStepStatus status)
{
  switch (status)
  {
    case HardwareTestStepStatus::NotRun:
      return "NOT_RUN";
    case HardwareTestStepStatus::PassedSoftwareBoundary:
      return "PASSED_SOFTWARE_BOUNDARY";
    case HardwareTestStepStatus::SkippedNoHardware:
      return "SKIPPED_NO_HARDWARE";
    case HardwareTestStepStatus::BlockedProtocolApi:
      return "BLOCKED_PROTOCOL_API";
    case HardwareTestStepStatus::ManualVerificationRequired:
      return "MANUAL_VERIFICATION_REQUIRED";
    case HardwareTestStepStatus::Failed:
      return "FAILED";
  }
  return "NOT_RUN";
}

HardwareTestPlan buildProfinetHardwareTestPlan(
    const HilscherReadinessRequest &request)
{
  HilscherReadinessRequest req = request;
  req.protocol = HilscherProtocolTarget::Profinet;
  if (req.expectedFirmwareName.empty())
  {
    req.expectedFirmwareName = "PROFINET";
  }

  HardwareTestPlan plan;
  plan.protocol = "profinet";
  plan.hardwareTarget =
      "CIFX 50E-RE + NXLFW-PNM + NXLIC-MASTER + GSDML + PROFINET IO-Device";
  plan.readiness = assessHilscherHardwareReadiness(req);

  const HardwareTestStepStatus host = hostStepStatus(plan.readiness);
  const std::string detail = readinessDetail(plan.readiness);
  const std::string protocolApi =
      "Hilscher Protocol API / firmware / hardware required. "
      "Do not invent DCP/AR mailbox structures.";

  plan.steps.push_back(step(
      "pn-01",
      "Controller / cifX host initialization",
      host,
      detail));
  plan.steps.push_back(step(
      "pn-02",
      "Network / interface / board-channel discovery",
      host,
      detail));
  plan.steps.push_back(step(
      "pn-03",
      "DCP discovery",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pn-04",
      "Station naming (DCP Set)",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi + " Station name remains ICP/SYCON metadata until then."));
  plan.steps.push_back(step(
      "pn-05",
      "Device identification",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pn-06",
      "Device configuration (SYCON artifact)",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Export config.nxd from SYCON.net / Communication Studio. "
      "Host can call xChannelDownload(DOWNLOAD_MODE_CONFIG) when hardware is present."));
  plan.steps.push_back(step(
      "pn-07",
      "AR establishment",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pn-08",
      "Slots / subslots",
      HardwareTestStepStatus::ManualVerificationRequired,
      "ICP-1B stores slot/subslot mapping metadata; live layout comes from SYCON + firmware."));
  plan.steps.push_back(step(
      "pn-09",
      "Process-data configuration",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Offsets/types configured in ICP-1B; image sizes from xChannelIOInfo when card is open."));
  plan.steps.push_back(step(
      "pn-10",
      "RT Class 1 cyclic IO",
      HardwareTestStepStatus::BlockedProtocolApi,
      "Cyclic plant IO requires firmware AR. Host xChannelIORead/Write is not a fake PROFINET stack."));
  plan.steps.push_back(step(
      "pn-11",
      "Input data path",
      host == HardwareTestStepStatus::PassedSoftwareBoundary
          ? HardwareTestStepStatus::ManualVerificationRequired
          : HardwareTestStepStatus::SkippedNoHardware,
      "SOFTWARE-INTEGRATION maps process image → GenericEquipment. Live inputs need hardware."));
  plan.steps.push_back(step(
      "pn-12",
      "Output data path",
      host == HardwareTestStepStatus::PassedSoftwareBoundary
          ? HardwareTestStepStatus::ManualVerificationRequired
          : HardwareTestStepStatus::SkippedNoHardware,
      "SOFTWARE-INTEGRATION maps commands → output image. Live outputs need hardware."));
  plan.steps.push_back(step(
      "pn-13",
      "Diagnostics",
      HardwareTestStepStatus::BlockedProtocolApi,
      "Common status block readable via cifX; protocol diagnostics need Protocol API."));
  plan.steps.push_back(step(
      "pn-14",
      "Communication loss",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Disconnect IO-Device on hardware; expect ConnectionState::Faulted, not machineFault."));
  plan.steps.push_back(step(
      "pn-15",
      "Recovery / explicit reconnect",
      HardwareTestStepStatus::ManualVerificationRequired,
      "ICP uses explicit connect() only — no application auto-reconnect."));
  plan.steps.push_back(step(
      "pn-16",
      "Multiple IO-Devices",
      HardwareTestStepStatus::ManualVerificationRequired,
      "One ProfinetIndustrialAdapter = one controller; many GenericEquipment mappings."));
  plan.steps.push_back(step(
      "pn-17",
      "GenericEquipment mapping",
      HardwareTestStepStatus::PassedSoftwareBoundary,
      "SOFTWARE-INTEGRATION covered without hardware."));
  plan.steps.push_back(step(
      "pn-18",
      "LiveStateCache mapping",
      HardwareTestStepStatus::PassedSoftwareBoundary,
      "SOFTWARE-INTEGRATION covered without hardware."));

  return plan;
}

HardwareTestPlan buildProfibusHardwareTestPlan(
    const HilscherReadinessRequest &request)
{
  HilscherReadinessRequest req = request;
  req.protocol = HilscherProtocolTarget::Profibus;
  if (req.expectedFirmwareName.empty())
  {
    req.expectedFirmwareName = "PROFIBUS";
  }

  HardwareTestPlan plan;
  plan.protocol = "profibus";
  plan.hardwareTarget =
      "CIFX 50E-DP + DPM firmware + master license + GSD + DP slave";
  plan.readiness = assessHilscherHardwareReadiness(req);

  const HardwareTestStepStatus host = hostStepStatus(plan.readiness);
  const std::string detail = readinessDetail(plan.readiness);
  const std::string protocolApi =
      "Hilscher Protocol API / firmware / hardware required. "
      "Do not invent DP packets or DP-V1 mailbox commands.";

  plan.steps.push_back(step(
      "pb-01", "Master / cifX host initialization", host, detail));
  plan.steps.push_back(step(
      "pb-02", "Channel discovery", host, detail));
  plan.steps.push_back(step(
      "pb-03",
      "Baud rate",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Baud is ICP/SYCON metadata; applied by DPM firmware from config.nxd."));
  plan.steps.push_back(step(
      "pb-04",
      "Slave configuration",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Configure in SYCON from GSD; download artifact via xChannelDownload when hardware present."));
  plan.steps.push_back(step(
      "pb-05",
      "Slave address",
      HardwareTestStepStatus::ManualVerificationRequired,
      "ICP-1B stores stationAddress 1–126; live bus uses SYCON artifact."));
  plan.steps.push_back(step(
      "pb-06",
      "Slave identity",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pb-07",
      "Modules",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Module layout is ICP mapping metadata; GSD interpretation is firmware/tooling."));
  plan.steps.push_back(step(
      "pb-08",
      "Cyclic DP IO",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pb-09",
      "Process image",
      host == HardwareTestStepStatus::PassedSoftwareBoundary
          ? HardwareTestStepStatus::ManualVerificationRequired
          : HardwareTestStepStatus::SkippedNoHardware,
      "Host xChannelIORead/Write available when channel is open; not fake DP."));
  plan.steps.push_back(step(
      "pb-10",
      "Diagnostics",
      HardwareTestStepStatus::BlockedProtocolApi,
      protocolApi));
  plan.steps.push_back(step(
      "pb-11",
      "Slave failure",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Disconnect slave on hardware; expect communication Faulted ≠ machineFault."));
  plan.steps.push_back(step(
      "pb-12",
      "Recovery / explicit reconnect",
      HardwareTestStepStatus::ManualVerificationRequired,
      "Explicit connect() only."));
  plan.steps.push_back(step(
      "pb-13",
      "Multiple slaves",
      HardwareTestStepStatus::ManualVerificationRequired,
      "One ProfibusIndustrialAdapter = one DP Master; many GenericEquipment mappings."));
  plan.steps.push_back(step(
      "pb-14",
      "GenericEquipment mapping",
      HardwareTestStepStatus::PassedSoftwareBoundary,
      "SOFTWARE-INTEGRATION covered without hardware."));
  plan.steps.push_back(step(
      "pb-15",
      "LiveStateCache mapping",
      HardwareTestStepStatus::PassedSoftwareBoundary,
      "SOFTWARE-INTEGRATION covered without hardware."));

  return plan;
}

std::string formatHardwareTestPlan(const HardwareTestPlan &plan)
{
  std::ostringstream out;
  out << "Hardware test plan (" << plan.protocol << ")\n";
  out << "  target: " << plan.hardwareTarget << "\n";
  out << "  readiness: " << plan.readiness.stateLabel << "\n";
  out << formatHilscherReadinessReport(plan.readiness);
  out << "  steps:\n";
  for (const HardwareTestStep &entry : plan.steps)
  {
    out << "    [" << hardwareTestStepStatusLabel(entry.status) << "] "
        << entry.id << " " << entry.title << " — " << entry.detail << "\n";
  }
  out << "  IMPORTANT: No step claims REAL PROFINET/PROFIBUS hardware validation "
         "until a physical card and peer are tested.\n";
  return out.str();
}

}  // namespace internal
}  // namespace virtual_factory
