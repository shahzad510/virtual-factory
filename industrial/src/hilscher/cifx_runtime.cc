#include "cifx_runtime.hh"

#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <vector>

#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
#ifdef __linux__
#include <cifxlinux.h>
#else
#include <cifXUser.h>
#endif
#include <cifXErrors.h>
#endif

namespace virtual_factory
{
namespace internal
{

namespace
{

std::mutex g_mutex;
int g_refcount = 0;
bool g_initialized = false;

#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE

std::string packedName(const char *data, std::size_t capacity)
{
  return std::string(data, strnlen(data, capacity));
}

std::string packedFwName(const std::uint8_t *data, std::size_t capacity)
{
  return packedName(reinterpret_cast<const char *>(data), capacity);
}

#endif

}  // namespace

#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE

std::string CifxChannel::formatError(int32_t code)
{
  char buffer[256] = {0};
  xDriverGetErrorDescription(code, buffer, sizeof(buffer));
  std::ostringstream stream;
  stream << "cifX error 0x" << std::hex << static_cast<unsigned>(code);
  if (buffer[0] != '\0')
  {
    stream << " (" << buffer << ")";
  }
  return stream.str();
}

bool CifxDriver::acquire(std::string *error)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized)
  {
    ++g_refcount;
    return true;
  }

#ifdef __linux__
  CIFX_LINUX_INIT init{};
  init.init_options = CIFX_DRIVER_INIT_AUTOSCAN;
  init.trace_level = 0;
  const int32_t rc = cifXDriverInit(&init);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "cifXDriverInit failed: " + CifxChannel::formatError(rc)
               + " (HARDWARE VALIDATION PENDING — NXDRV / uio_netx / cifX card)";
    }
    return false;
  }
#endif

  g_initialized = true;
  g_refcount = 1;
  return true;
}

void CifxDriver::release()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_refcount <= 0)
  {
    return;
  }
  --g_refcount;
  if (g_refcount == 0 && g_initialized)
  {
#ifdef __linux__
    cifXDriverDeinit();
#endif
    g_initialized = false;
  }
}

bool CifxDriver::acquired()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_initialized;
}

bool CifxDriver::enumerate(CifxInventory *inventory, std::string *error)
{
  if (inventory == nullptr)
  {
    if (error != nullptr)
    {
      *error = "cifX inventory output is null";
    }
    return false;
  }
  inventory->driverVersion.clear();
  inventory->boardCount = 0;
  inventory->boards.clear();

  if (!acquire(error))
  {
    return false;
  }

  CIFXHANDLE driver = nullptr;
  int32_t rc = xDriverOpen(&driver);
  if (rc != CIFX_NO_ERROR)
  {
    release();
    if (error != nullptr)
    {
      *error = "xDriverOpen failed: " + CifxChannel::formatError(rc)
               + " (HARDWARE VALIDATION PENDING if no cifX card is present)";
    }
    return false;
  }

  DRIVER_INFORMATION driverInfo{};
  rc = xDriverGetInformation(driver, sizeof(driverInfo), &driverInfo);
  if (rc == CIFX_NO_ERROR)
  {
    inventory->driverVersion = packedName(
        driverInfo.abDriverVersion, sizeof(driverInfo.abDriverVersion));
    inventory->boardCount = driverInfo.ulBoardCnt;
  }

  unsigned boardIndex = 0;
  BOARD_INFORMATION boardInfo{};
  while (xDriverEnumBoards(
             driver, boardIndex, sizeof(boardInfo), &boardInfo)
         == CIFX_NO_ERROR)
  {
    CifxEnumeratedBoard board;
    board.boardName =
        packedName(boardInfo.abBoardName, sizeof(boardInfo.abBoardName));
    board.boardAlias =
        packedName(boardInfo.abBoardAlias, sizeof(boardInfo.abBoardAlias));
    board.boardId = boardInfo.ulBoardID;
    board.channelCount = boardInfo.ulChannelCnt;

    unsigned channel = 0;
    CHANNEL_INFORMATION channelInfo{};
    while (xDriverEnumChannels(
               driver,
               boardIndex,
               channel,
               sizeof(channelInfo),
               &channelInfo)
           == CIFX_NO_ERROR)
    {
      CifxEnumeratedChannel entry;
      entry.boardIndex = boardIndex;
      entry.channel = channel;
      entry.firmwareName =
          packedFwName(channelInfo.abFWName, sizeof(channelInfo.abFWName));
      entry.firmwareMajor = channelInfo.usFWMajor;
      entry.firmwareMinor = channelInfo.usFWMinor;
      entry.firmwareBuild = channelInfo.usFWBuild;
      board.channels.push_back(entry);
      ++channel;
    }

    inventory->boards.push_back(std::move(board));
    ++boardIndex;
  }

  if (inventory->boardCount == 0)
  {
    inventory->boardCount = static_cast<unsigned>(inventory->boards.size());
  }

  xDriverClose(driver);
  release();
  return true;
}

CifxChannel::~CifxChannel()
{
  this->close();
}

bool CifxChannel::open(
    const std::string &boardId, unsigned channel, std::string *error)
{
  this->close();
  if (boardId.empty())
  {
    if (error != nullptr)
    {
      *error = "missing cifX board identifier";
    }
    return false;
  }

  if (!CifxDriver::acquire(error))
  {
    return false;
  }

  CIFXHANDLE driver = nullptr;
  int32_t rc = xDriverOpen(&driver);
  if (rc != CIFX_NO_ERROR)
  {
    CifxDriver::release();
    if (error != nullptr)
    {
      *error = "xDriverOpen failed: " + formatError(rc)
               + " (HARDWARE VALIDATION PENDING if no cifX card is present)";
    }
    return false;
  }

  CIFXHANDLE channelHandle = nullptr;
  std::vector<char> board(boardId.begin(), boardId.end());
  board.push_back('\0');
  rc = xChannelOpen(driver, board.data(), channel, &channelHandle);
  if (rc != CIFX_NO_ERROR)
  {
    xDriverClose(driver);
    CifxDriver::release();
    if (error != nullptr)
    {
      *error = "xChannelOpen(" + boardId + ", " + std::to_string(channel)
               + ") failed: " + formatError(rc)
               + " (HARDWARE VALIDATION PENDING — card/firmware/channel required)";
    }
    return false;
  }

  this->driver_ = driver;
  this->channel_ = channelHandle;
  this->open_ = true;
  return true;
}

void CifxChannel::close()
{
  if (this->channel_ != nullptr)
  {
    xChannelClose(static_cast<CIFXHANDLE>(this->channel_));
    this->channel_ = nullptr;
  }
  if (this->driver_ != nullptr)
  {
    xDriverClose(static_cast<CIFXHANDLE>(this->driver_));
    this->driver_ = nullptr;
  }
  if (this->open_)
  {
    CifxDriver::release();
    this->open_ = false;
  }
}

bool CifxChannel::isOpen() const
{
  return this->open_;
}

bool CifxChannel::queryInfo(CifxChannelInfo *info, std::string *error)
{
  if (!this->open_ || info == nullptr)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }

  CHANNEL_INFORMATION channelInfo{};
  const int32_t rc = xChannelInfo(
      static_cast<CIFXHANDLE>(this->channel_),
      sizeof(channelInfo),
      &channelInfo);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelInfo failed: " + formatError(rc);
    }
    return false;
  }

  info->boardName =
      packedName(channelInfo.abBoardName, sizeof(channelInfo.abBoardName));
  info->firmwareName =
      packedFwName(channelInfo.abFWName, sizeof(channelInfo.abFWName));
  info->firmwareMajor = channelInfo.usFWMajor;
  info->firmwareMinor = channelInfo.usFWMinor;
  info->firmwareBuild = channelInfo.usFWBuild;
  info->mailboxSize = channelInfo.ulMailboxSize;
  info->inputAreaCount = channelInfo.ulIOInAreaCnt;
  info->outputAreaCount = channelInfo.ulIOOutAreaCnt;

  CHANNEL_IO_INFORMATION inInfo{};
  if (xChannelIOInfo(
          static_cast<CIFXHANDLE>(this->channel_),
          CIFX_IO_INPUT_AREA,
          0,
          sizeof(inInfo),
          &inInfo)
      == CIFX_NO_ERROR)
  {
    info->inputAreaBytes = inInfo.ulTotalSize;
  }

  CHANNEL_IO_INFORMATION outInfo{};
  if (xChannelIOInfo(
          static_cast<CIFXHANDLE>(this->channel_),
          CIFX_IO_OUTPUT_AREA,
          0,
          sizeof(outInfo),
          &outInfo)
      == CIFX_NO_ERROR)
  {
    info->outputAreaBytes = outInfo.ulTotalSize;
  }

  return true;
}

bool CifxChannel::setHostReady(bool ready, std::string *error)
{
  if (!this->open_)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  uint32_t state = 0;
  const uint32_t cmd =
      ready ? CIFX_HOST_STATE_READY : CIFX_HOST_STATE_NOT_READY;
  const int32_t rc = xChannelHostState(
      static_cast<CIFXHANDLE>(this->channel_), cmd, &state, 2000);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelHostState failed: " + formatError(rc);
    }
    return false;
  }
  return true;
}

bool CifxChannel::setBusOn(bool on, unsigned timeoutMs, std::string *error)
{
  if (!this->open_)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  uint32_t state = 0;
  const uint32_t cmd = on ? CIFX_BUS_STATE_ON : CIFX_BUS_STATE_OFF;
  const int32_t rc = xChannelBusState(
      static_cast<CIFXHANDLE>(this->channel_),
      cmd,
      &state,
      timeoutMs);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelBusState failed: " + formatError(rc)
               + " (HARDWARE VALIDATION PENDING — bus/firmware/config required)";
    }
    return false;
  }
  return true;
}

bool CifxChannel::queryBusOn(bool *on, std::string *error)
{
  if (!this->open_ || on == nullptr)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  uint32_t state = 0;
  const int32_t rc = xChannelBusState(
      static_cast<CIFXHANDLE>(this->channel_),
      CIFX_BUS_STATE_GETSTATE,
      &state,
      0);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelBusState(GETSTATE) failed: " + formatError(rc);
    }
    return false;
  }
  *on = (state != 0);
  return true;
}

bool CifxChannel::downloadConfigFile(
    const std::string &path, std::string *error)
{
  if (!this->open_)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    if (error != nullptr)
    {
      *error = "cannot read configuration artifact: " + path;
    }
    return false;
  }
  std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());
  if (bytes.empty())
  {
    if (error != nullptr)
    {
      *error = "empty configuration artifact: " + path;
    }
    return false;
  }

  std::vector<char> name(path.begin(), path.end());
  name.push_back('\0');
  const int32_t rc = xChannelDownload(
      static_cast<CIFXHANDLE>(this->channel_),
      DOWNLOAD_MODE_CONFIG,
      name.data(),
      bytes.data(),
      static_cast<uint32_t>(bytes.size()),
      nullptr,
      nullptr,
      nullptr);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelDownload(DOWNLOAD_MODE_CONFIG) failed: "
               + formatError(rc)
               + " (HARDWARE VALIDATION PENDING — Protocol API / firmware)";
    }
    return false;
  }
  return true;
}

bool CifxChannel::startWatchdog(std::string *error)
{
  if (!this->open_)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  uint32_t trigger = 0;
  const int32_t rc = xChannelWatchdog(
      static_cast<CIFXHANDLE>(this->channel_), CIFX_WATCHDOG_START, &trigger);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelWatchdog(START) failed: " + formatError(rc);
    }
    return false;
  }
  return true;
}

bool CifxChannel::triggerWatchdog(std::string *error)
{
  return this->startWatchdog(error);
}

bool CifxChannel::stopWatchdog(std::string *error)
{
  if (!this->open_)
  {
    return true;
  }
  uint32_t trigger = 0;
  const int32_t rc = xChannelWatchdog(
      static_cast<CIFXHANDLE>(this->channel_), CIFX_WATCHDOG_STOP, &trigger);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelWatchdog(STOP) failed: " + formatError(rc);
    }
    return false;
  }
  return true;
}

bool CifxChannel::readCommonStatus(
    std::size_t length, std::vector<std::uint8_t> *out, std::string *error)
{
  if (!this->open_ || out == nullptr || length == 0)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  out->assign(length, 0);
  const int32_t rc = xChannelCommonStatusBlock(
      static_cast<CIFXHANDLE>(this->channel_),
      CIFX_CMD_READ_DATA,
      0,
      static_cast<uint32_t>(length),
      out->data());
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelCommonStatusBlock failed: " + formatError(rc);
    }
    return false;
  }
  return true;
}

bool CifxChannel::readInput(
    std::size_t offset,
    std::size_t length,
    std::vector<std::uint8_t> *out,
    unsigned timeoutMs,
    std::string *error)
{
  if (!this->open_ || out == nullptr)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  out->assign(length, 0);
  if (length == 0)
  {
    return true;
  }
  const int32_t rc = xChannelIORead(
      static_cast<CIFXHANDLE>(this->channel_),
      0,
      static_cast<uint32_t>(offset),
      static_cast<uint32_t>(length),
      out->data(),
      timeoutMs);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelIORead failed: " + formatError(rc)
               + " (HARDWARE VALIDATION PENDING — cyclic IO / AR / DP slaves)";
    }
    return false;
  }
  return true;
}

bool CifxChannel::writeOutput(
    std::size_t offset,
    const std::vector<std::uint8_t> &data,
    unsigned timeoutMs,
    std::string *error)
{
  if (!this->open_)
  {
    if (error != nullptr)
    {
      *error = "cifX channel not open";
    }
    return false;
  }
  if (data.empty())
  {
    return true;
  }
  std::vector<std::uint8_t> copy = data;
  const int32_t rc = xChannelIOWrite(
      static_cast<CIFXHANDLE>(this->channel_),
      0,
      static_cast<uint32_t>(offset),
      static_cast<uint32_t>(copy.size()),
      copy.data(),
      timeoutMs);
  if (rc != CIFX_NO_ERROR)
  {
    if (error != nullptr)
    {
      *error = "xChannelIOWrite failed: " + formatError(rc)
               + " (HARDWARE VALIDATION PENDING — cyclic IO / AR / DP slaves)";
    }
    return false;
  }
  return true;
}

#else  // SDK not compiled in

std::string CifxChannel::formatError(int32_t)
{
  return "Hilscher cifX SDK not compiled into this build";
}

bool CifxDriver::acquire(std::string *error)
{
  if (error != nullptr)
  {
    *error =
        "Hilscher cifX SDK not compiled in (VF_HILSCHER_CIFX_AVAILABLE=0). "
        "BLOCKED BY SDK/HARDWARE.";
  }
  return false;
}

void CifxDriver::release() {}

bool CifxDriver::acquired()
{
  return false;
}

bool CifxDriver::enumerate(CifxInventory *, std::string *error)
{
  if (error != nullptr)
  {
    *error =
        "Hilscher cifX SDK not compiled in (VF_HILSCHER_CIFX_AVAILABLE=0). "
        "BLOCKED BY SDK/HARDWARE.";
  }
  return false;
}

CifxChannel::~CifxChannel() = default;

bool CifxChannel::open(const std::string &, unsigned, std::string *error)
{
  if (error != nullptr)
  {
    *error = "Hilscher cifX SDK not compiled in. BLOCKED BY SDK/HARDWARE.";
  }
  return false;
}

void CifxChannel::close() {}

bool CifxChannel::isOpen() const
{
  return false;
}

bool CifxChannel::queryInfo(CifxChannelInfo *, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::setHostReady(bool, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::setBusOn(bool, unsigned, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::queryBusOn(bool *, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::downloadConfigFile(const std::string &, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::startWatchdog(std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::triggerWatchdog(std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::stopWatchdog(std::string *error)
{
  (void)error;
  return true;
}

bool CifxChannel::readCommonStatus(
    std::size_t, std::vector<std::uint8_t> *, std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::readInput(
    std::size_t,
    std::size_t,
    std::vector<std::uint8_t> *,
    unsigned,
    std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

bool CifxChannel::writeOutput(
    std::size_t,
    const std::vector<std::uint8_t> &,
    unsigned,
    std::string *error)
{
  if (error != nullptr)
  {
    *error = "cifX SDK not compiled in";
  }
  return false;
}

#endif

}  // namespace internal
}  // namespace virtual_factory
