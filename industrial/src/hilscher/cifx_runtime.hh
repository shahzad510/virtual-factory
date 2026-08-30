#ifndef VIRTUAL_FACTORY_CIFX_RUNTIME_HH_
#define VIRTUAL_FACTORY_CIFX_RUNTIME_HH_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace virtual_factory
{
namespace internal
{

struct CifxEnumeratedChannel
{
  unsigned boardIndex{0};
  unsigned channel{0};
  std::string firmwareName;
  unsigned firmwareMajor{0};
  unsigned firmwareMinor{0};
  unsigned firmwareBuild{0};
  unsigned firmwareRevision{0};
  unsigned firmwareYear{0};
  unsigned firmwareMonth{0};
  unsigned firmwareDay{0};
  std::uint32_t deviceNumber{0};
  std::uint32_t serialNumber{0};
  std::uint32_t channelError{0};
  std::uint32_t openCount{0};
  std::size_t mailboxSize{0};
  unsigned inputAreaCount{0};
  unsigned outputAreaCount{0};
};

struct CifxEnumeratedBoard
{
  std::string boardName;
  std::string boardAlias;
  unsigned boardId{0};
  unsigned channelCount{0};
  std::int32_t boardError{0};
  std::uint32_t systemError{0};
  std::uint32_t deviceNumber{0};
  std::uint32_t serialNumber{0};
  std::uint32_t dpmTotalSize{0};
  std::uint32_t licenseFlags1{0};
  std::uint32_t licenseFlags2{0};
  std::uint16_t netxLicenseId{0};
  std::uint16_t netxLicenseFlags{0};
  std::vector<CifxEnumeratedChannel> channels;
};

struct CifxInventory
{
  std::string driverVersion;
  unsigned boardCount{0};
  std::string lastDriverError;
  std::vector<CifxEnumeratedBoard> boards;
};

struct CifxChannelInfo
{
  std::string boardName;
  std::string boardAlias;
  std::string firmwareName;
  unsigned firmwareMajor{0};
  unsigned firmwareMinor{0};
  unsigned firmwareBuild{0};
  unsigned firmwareRevision{0};
  std::uint32_t deviceNumber{0};
  std::uint32_t serialNumber{0};
  std::uint32_t channelError{0};
  std::uint32_t openCount{0};
  std::size_t mailboxSize{0};
  unsigned inputAreaCount{0};
  unsigned outputAreaCount{0};
  std::size_t inputAreaBytes{0};
  std::size_t outputAreaBytes{0};
  /// Host/bus state when queried; false if query failed.
  bool hostReadyKnown{false};
  bool hostReady{false};
  bool busOnKnown{false};
  bool busOn{false};
};

/// Process-wide cifX driver (Linux: cifXDriverInit / cifXDriverDeinit).
/// Windows: kernel NXDRV-WIN is assumed already loaded; xDriverOpen is used.
///
/// No Hilscher types appear in this header.
class CifxDriver
{
public:
  static bool acquire(std::string *error);
  static void release();
  static bool acquired();

  /// xDriverOpen + xDriverGetInformation + xDriverEnumBoards/Channels.
  /// Succeeds with an empty board list when the driver is up but no card
  /// is present. Does not invent devices.
  static bool enumerate(CifxInventory *inventory, std::string *error);
};

/// One cifX communication channel. Uses only official cifX host APIs.
/// Does not invent PROFINET DCP/AR or PROFIBUS DP-V1 mailbox packet IDs.
class CifxChannel
{
public:
  CifxChannel() = default;
  ~CifxChannel();

  CifxChannel(const CifxChannel &) = delete;
  CifxChannel &operator=(const CifxChannel &) = delete;

  bool open(const std::string &boardId, unsigned channel, std::string *error);
  void close();
  bool isOpen() const;

  bool queryInfo(CifxChannelInfo *info, std::string *error);
  bool setHostReady(bool ready, std::string *error);
  bool queryHostReady(bool *ready, std::string *error);
  bool setBusOn(bool on, unsigned timeoutMs, std::string *error);
  bool queryBusOn(bool *on, std::string *error);
  bool downloadConfigFile(const std::string &path, std::string *error);

  bool startWatchdog(std::string *error);
  bool triggerWatchdog(std::string *error);
  bool stopWatchdog(std::string *error);

  /// Dual-port common status block (host DPM). Not a protocol mailbox command.
  bool readCommonStatus(
      std::size_t length, std::vector<std::uint8_t> *out, std::string *error);

  bool readInput(
      std::size_t offset,
      std::size_t length,
      std::vector<std::uint8_t> *out,
      unsigned timeoutMs,
      std::string *error);
  bool writeOutput(
      std::size_t offset,
      const std::vector<std::uint8_t> &data,
      unsigned timeoutMs,
      std::string *error);

  static std::string formatError(int32_t code);

private:
  bool open_{false};
  void *driver_{nullptr};
  void *channel_{nullptr};
};

}  // namespace internal
}  // namespace virtual_factory

#endif
