#include "hilscher_hardware_readiness.hh"

#include "hilscher_availability.hh"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace virtual_factory
{
namespace internal
{

namespace
{

std::string toUpper(std::string value)
{
  for (char &ch : value)
  {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool firmwareMatches(
    const std::string &firmwareName,
    const std::string &expected,
    HilscherProtocolTarget protocol)
{
  const std::string upper = toUpper(firmwareName);
  if (!expected.empty())
  {
    return upper.find(toUpper(expected)) != std::string::npos;
  }
  if (protocol == HilscherProtocolTarget::Profinet)
  {
    return upper.find("PROFINET") != std::string::npos
           || upper.find("PNM") != std::string::npos;
  }
  if (protocol == HilscherProtocolTarget::Profibus)
  {
    return upper.find("PROFIBUS") != std::string::npos
           || upper.find("DPM") != std::string::npos
           || upper.find("CIFXDPM") != std::string::npos;
  }
  return !firmwareName.empty();
}

bool boardIdMatches(const CifxEnumeratedBoard &board, const std::string &boardId)
{
  if (boardId.empty())
  {
    return true;
  }
  return board.boardName == boardId || board.boardAlias == boardId;
}

const CifxEnumeratedBoard *findBoard(
    const CifxInventory &inventory, const std::string &boardId)
{
  for (const CifxEnumeratedBoard &board : inventory.boards)
  {
    if (boardIdMatches(board, boardId))
    {
      return &board;
    }
  }
  return nullptr;
}

const CifxEnumeratedChannel *findChannel(
    const CifxEnumeratedBoard &board, unsigned channel)
{
  for (const CifxEnumeratedChannel &entry : board.channels)
  {
    if (entry.channel == channel)
    {
      return &entry;
    }
  }
  if (channel < board.channels.size())
  {
    return &board.channels[channel];
  }
  return nullptr;
}

}  // namespace

const char *hilscherReadinessStateLabel(HilscherReadinessState state)
{
  switch (state)
  {
    case HilscherReadinessState::SdkMissing:
      return "SDK_MISSING";
    case HilscherReadinessState::DriverInitFailed:
      return "DRIVER_MISSING";
    case HilscherReadinessState::NoBoard:
      return "NO_BOARD";
    case HilscherReadinessState::NoChannel:
      return "NO_CHANNEL";
    case HilscherReadinessState::WrongFirmware:
      return "WRONG_FIRMWARE";
    case HilscherReadinessState::FirmwareNotReady:
      return "FIRMWARE_NOT_READY";
    case HilscherReadinessState::LicenseManualVerificationRequired:
      return "LICENSE_NOT_READY";
    case HilscherReadinessState::ReadyForHostIoTest:
      return "READY_FOR_TEST";
    case HilscherReadinessState::ManualVerificationRequired:
      return "MANUAL_VERIFICATION_REQUIRED";
  }
  return "MANUAL_VERIFICATION_REQUIRED";
}

HilscherReadinessReport assessHilscherHardwareReadiness(
    const HilscherReadinessRequest &request)
{
  HilscherReadinessReport report;
  report.state = HilscherReadinessState::SdkMissing;
  report.stateLabel = hilscherReadinessStateLabel(report.state);
  report.sdkCompiledIn = hilscherCifxSdkAvailable();

  report.manualChecks.push_back(
      "NXLIC-MASTER / master license validity: MANUAL_VERIFICATION_REQUIRED "
      "(confirm in SYCON.net / Communication Studio / Hilscher tools)");
  report.manualChecks.push_back(
      "GSDML/GSD engineering and SYCON config.nxd: MANUAL_VERIFICATION_REQUIRED");
  report.manualChecks.push_back(
      "Live DCP/AR/RT Class 1 or DP cyclic plant IO: HARDWARE VALIDATION PENDING "
      "(Hilscher Protocol API / firmware / hardware required)");

  if (!report.sdkCompiledIn)
  {
    report.summary = hilscherCifxUnavailableReason();
    report.notes.push_back(
        "Rebuild with -DVF_ENABLE_HILSCHER_PROFINET=ON and/or "
        "-DVF_ENABLE_HILSCHER_PROFIBUS=ON and HILSCHER_CIFX_ROOT set.");
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    return report;
  }

  std::string enumError;
  const bool enumerated = CifxDriver::enumerate(&report.inventory, &enumError);
  report.driverError =
      enumerated ? report.inventory.lastDriverError : enumError;
  if (!enumerated)
  {
    report.state = HilscherReadinessState::DriverInitFailed;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "cifX driver initialization / open failed. "
        "Install NXDRV / nxdrvlinux + uio_netx, then reconnect the CIFX card.";
    if (!report.driverError.empty())
    {
      report.notes.push_back(report.driverError);
    }
    return report;
  }

  report.driverInitialized = true;
  report.driverVersion = report.inventory.driverVersion;

  if (report.inventory.boards.empty())
  {
    report.state = HilscherReadinessState::NoBoard;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "cifX driver is up but no boards were enumerated (xDriverEnumBoards). "
        "HARDWARE VALIDATION PENDING — insert CIFX 50E-RE / CIFX 50E-DP.";
    return report;
  }

  const CifxEnumeratedBoard *board =
      findBoard(report.inventory, request.boardId);
  if (board == nullptr)
  {
    report.state = HilscherReadinessState::NoBoard;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "Requested boardId '" + request.boardId
        + "' was not found among enumerated cifX boards.";
    return report;
  }

  report.selectedBoardFound = true;
  report.selectedBoardName = board->boardName;
  report.selectedBoardAlias = board->boardAlias;
  report.selectedSerialNumber = board->serialNumber;
  report.selectedDeviceNumber = board->deviceNumber;
  report.selectedLicenseFlags1 = board->licenseFlags1;
  report.selectedLicenseFlags2 = board->licenseFlags2;
  report.selectedNetxLicenseId = board->netxLicenseId;

  if (board->boardError != 0)
  {
    report.notes.push_back(
        "BOARD_INFORMATION.lBoardError=" + std::to_string(board->boardError)
        + " (board-specific data may be unreliable)");
  }
  if (board->systemError != 0)
  {
    report.notes.push_back(
        "BOARD_INFORMATION.ulSystemError=" + std::to_string(board->systemError));
  }

  if (board->channels.empty() || board->channelCount == 0)
  {
    report.state = HilscherReadinessState::NoChannel;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "Board '" + board->boardName
        + "' has no communication channels. Load protocol firmware.";
    return report;
  }

  const CifxEnumeratedChannel *channel =
      findChannel(*board, request.channel);
  if (channel == nullptr)
  {
    report.state = HilscherReadinessState::NoChannel;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "Channel " + std::to_string(request.channel)
        + " not found on board '" + board->boardName + "'.";
    return report;
  }

  report.selectedChannelFound = true;
  report.selectedFirmwareName = channel->firmwareName;
  if (channel->channelError != 0)
  {
    report.notes.push_back(
        "CHANNEL_INFORMATION.ulChannelError="
        + std::to_string(channel->channelError));
  }

  if (channel->firmwareName.empty()
      && channel->firmwareMajor == 0 && channel->firmwareMinor == 0)
  {
    report.state = HilscherReadinessState::FirmwareNotReady;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "Channel firmware name/version is empty. Load NXLFW-PNM or DPM firmware "
        "via Hilscher tools. HARDWARE VALIDATION PENDING.";
    return report;
  }

  if (!firmwareMatches(
          channel->firmwareName, request.expectedFirmwareName, request.protocol))
  {
    report.state = HilscherReadinessState::WrongFirmware;
    report.stateLabel = hilscherReadinessStateLabel(report.state);
    report.summary =
        "Firmware '" + channel->firmwareName
        + "' does not match the expected PROFINET/PROFIBUS controller/master "
          "firmware. Load NXLFW-PNM or CIFXDPM/NXLFW-DPM.";
    return report;
  }

  // Optional deeper probe: open the channel for IO area / host/bus state.
  const std::string openId =
      !board->boardAlias.empty() ? board->boardAlias : board->boardName;
  CifxChannel probeChannel;
  std::string openError;
  if (probeChannel.open(openId, channel->channel, &openError))
  {
    report.channelProbe.opened = true;
    if (!probeChannel.queryInfo(&report.channelProbe.info, &openError))
    {
      report.notes.push_back("xChannelInfo probe failed: " + openError);
    }
    probeChannel.close();
  }
  else
  {
    report.channelProbe.opened = false;
    report.channelProbe.openError = openError;
    report.notes.push_back(
        "Channel open probe failed (card busy or firmware not ready): "
        + openError);
  }

  // License flags are visible but not interpreted as master-license proof.
  report.notes.push_back(
      "License flags from SYSTEM_INFO_BLOCK: flags1="
      + std::to_string(board->licenseFlags1) + " flags2="
      + std::to_string(board->licenseFlags2) + " netxLicenseId="
      + std::to_string(board->netxLicenseId)
      + " — do not treat as NXLIC-MASTER confirmation.");

  report.state = HilscherReadinessState::ReadyForHostIoTest;
  report.stateLabel = hilscherReadinessStateLabel(report.state);
  report.summary =
      "Driver, board, channel, and matching firmware are visible via cifX. "
      "Host process-image APIs may be exercised. This is NOT a REAL PROFINET "
      "or PROFIBUS hardware validation. License + GSDML/GSD + IO-Device/slave "
      "remain MANUAL_VERIFICATION_REQUIRED.";
  report.notes.push_back(
      "READY_FOR_TEST means host cifX preflight only — not plant cyclic IO.");
  return report;
}

std::string formatHilscherReadinessReport(const HilscherReadinessReport &report)
{
  std::ostringstream out;
  out << "Hilscher hardware readiness\n";
  out << "  state: " << report.stateLabel << "\n";
  out << "  summary: " << report.summary << "\n";
  out << "  sdkCompiledIn: " << (report.sdkCompiledIn ? "yes" : "no") << "\n";
  out << "  driverInitialized: " << (report.driverInitialized ? "yes" : "no")
      << "\n";
  out << "  driverVersion: "
      << (report.driverVersion.empty() ? "(none)" : report.driverVersion) << "\n";
  if (!report.driverError.empty())
  {
    out << "  driverError: " << report.driverError << "\n";
  }
  out << "  boardCount: " << report.inventory.boardCount << "\n";
  for (std::size_t b = 0; b < report.inventory.boards.size(); ++b)
  {
    const CifxEnumeratedBoard &board = report.inventory.boards[b];
    out << "  board[" << b << "]: name=" << board.boardName
        << " alias=" << board.boardAlias
        << " serial=" << board.serialNumber
        << " device=" << board.deviceNumber
        << " channels=" << board.channelCount << "\n";
    for (const CifxEnumeratedChannel &ch : board.channels)
    {
      out << "    channel[" << ch.channel << "]: fw='" << ch.firmwareName
          << "' " << ch.firmwareMajor << "." << ch.firmwareMinor << "."
          << ch.firmwareBuild
          << " channelError=" << ch.channelError
          << " inAreas=" << ch.inputAreaCount
          << " outAreas=" << ch.outputAreaCount << "\n";
    }
  }
  if (report.selectedBoardFound)
  {
    out << "  selectedBoard: " << report.selectedBoardName
        << " / " << report.selectedBoardAlias
        << " serial=" << report.selectedSerialNumber << "\n";
  }
  if (report.selectedChannelFound)
  {
    out << "  selectedFirmware: " << report.selectedFirmwareName << "\n";
  }
  if (report.channelProbe.opened)
  {
    out << "  channelProbe: opened"
        << " hostReady="
        << (report.channelProbe.info.hostReadyKnown
                ? (report.channelProbe.info.hostReady ? "yes" : "no")
                : "unknown")
        << " busOn="
        << (report.channelProbe.info.busOnKnown
                ? (report.channelProbe.info.busOn ? "yes" : "no")
                : "unknown")
        << " inBytes=" << report.channelProbe.info.inputAreaBytes
        << " outBytes=" << report.channelProbe.info.outputAreaBytes << "\n";
  }
  else if (!report.channelProbe.openError.empty())
  {
    out << "  channelProbe: " << report.channelProbe.openError << "\n";
  }
  for (const std::string &note : report.notes)
  {
    out << "  note: " << note << "\n";
  }
  for (const std::string &check : report.manualChecks)
  {
    out << "  manual: " << check << "\n";
  }
  return out.str();
}

}  // namespace internal
}  // namespace virtual_factory
