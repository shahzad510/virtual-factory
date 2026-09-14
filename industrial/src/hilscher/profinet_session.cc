#include "profinet_session.hh"

#include "hilscher_availability.hh"

#include <algorithm>
#include <cctype>
#include <fstream>

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

bool configFileReadable(const std::string &path)
{
  std::ifstream file(path, std::ios::binary);
  return static_cast<bool>(file);
}

}  // namespace

ProfinetSession::~ProfinetSession()
{
  this->close();
}

bool ProfinetSession::sdkAvailable() const
{
  return hilscherCifxSdkAvailable();
}

bool ProfinetSession::matchesFirmware(const std::string &name) const
{
  const std::string upper = toUpper(name);
  if (!this->config_.expectedFirmwareName.empty())
  {
    return upper.find(toUpper(this->config_.expectedFirmwareName))
           != std::string::npos;
  }
  return upper.find("PROFINET") != std::string::npos
         || upper.find("PNM") != std::string::npos;
}

bool ProfinetSession::open(const ProfinetSessionConfig &config)
{
  this->close();
  this->config_ = config;

  if (config.configArtifactPath.empty())
  {
    this->last_error_ = "missing PROFINET configuration artifact path";
    return false;
  }

  if (!configFileReadable(config.configArtifactPath))
  {
    this->last_error_ =
        "cannot read PROFINET configuration artifact: " + config.configArtifactPath;
    return false;
  }

  if (!this->sdkAvailable())
  {
    this->last_error_ = hilscherCifxUnavailableReason();
    return false;
  }

  std::string error;
  CifxInventory inventory;
  if (!CifxDriver::enumerate(&inventory, &error))
  {
    this->last_error_ = error;
    return false;
  }
  if (inventory.boards.empty())
  {
    this->last_error_ =
        "no cifX boards enumerated (xDriverEnumBoards). "
        "HARDWARE VALIDATION PENDING — CIFX 50E-RE + NXLFW-PNM + NXLIC-MASTER.";
    return false;
  }

  if (!this->channel_.open(config.boardId, config.channel, &error))
  {
    this->last_error_ = error;
    return false;
  }

  CifxChannelInfo info;
  if (this->channel_.queryInfo(&info, &error))
  {
    this->firmware_name_ = info.firmwareName;
    this->input_area_bytes_ = info.inputAreaBytes;
    this->output_area_bytes_ = info.outputAreaBytes;
    if (!info.firmwareName.empty() && !this->matchesFirmware(info.firmwareName))
    {
      this->channel_.close();
      this->last_error_ =
          "cifX firmware is not a PROFINET IO-Controller (got '"
          + info.firmwareName
          + "'). Load NXLFW-PNM. HARDWARE VALIDATION PENDING. "
            "Requires Hilscher Protocol API / firmware / hardware.";
      return false;
    }
  }

  if (!this->channel_.setHostReady(true, &error))
  {
    this->channel_.close();
    this->last_error_ = error;
    return false;
  }

  if (!this->channel_.downloadConfigFile(config.configArtifactPath, &error))
  {
    this->channel_.close();
    this->last_error_ = error;
    return false;
  }

  const unsigned timeout =
      config.ioTimeoutMs > 0 ? config.ioTimeoutMs : 1000U;
  if (!this->channel_.setBusOn(true, timeout, &error))
  {
    this->channel_.close();
    this->last_error_ = error;
    return false;
  }

  (void)this->channel_.startWatchdog(&error);

  this->open_ = true;
  this->last_error_.clear();
  return true;
}

void ProfinetSession::close()
{
  if (this->channel_.isOpen())
  {
    std::string ignored;
    this->channel_.stopWatchdog(&ignored);
    this->channel_.setBusOn(false, 1000, &ignored);
    this->channel_.setHostReady(false, &ignored);
  }
  this->channel_.close();
  this->open_ = false;
  this->firmware_name_.clear();
  this->input_area_bytes_ = 0;
  this->output_area_bytes_ = 0;
}

bool ProfinetSession::connected() const
{
  return this->open_;
}

bool ProfinetSession::readInputArea(
    std::size_t byteOffset,
    std::size_t length,
    std::vector<std::uint8_t> *out)
{
  if (!this->open_)
  {
    this->last_error_ = "PROFINET session not connected";
    return false;
  }
  std::string error;
  if (!this->channel_.readInput(
          byteOffset, length, out, this->config_.ioTimeoutMs, &error))
  {
    this->last_error_ = error;
    return false;
  }
  return true;
}

bool ProfinetSession::writeOutputArea(
    std::size_t byteOffset, const std::vector<std::uint8_t> &data)
{
  if (!this->open_)
  {
    this->last_error_ = "PROFINET session not connected";
    return false;
  }
  std::string error;
  if (!this->channel_.writeOutput(
          byteOffset, data, this->config_.ioTimeoutMs, &error))
  {
    this->last_error_ = error;
    return false;
  }
  return true;
}

bool ProfinetSession::triggerWatchdog()
{
  if (!this->open_)
  {
    return false;
  }
  std::string error;
  return this->channel_.triggerWatchdog(&error);
}

bool ProfinetSession::readCommonStatus(
    std::size_t length, std::vector<std::uint8_t> *out)
{
  if (!this->open_)
  {
    this->last_error_ = "PROFINET session not connected";
    return false;
  }
  std::string error;
  if (!this->channel_.readCommonStatus(length, out, &error))
  {
    this->last_error_ = error;
    return false;
  }
  return true;
}

std::size_t ProfinetSession::inputAreaBytes() const
{
  return this->input_area_bytes_;
}

std::size_t ProfinetSession::outputAreaBytes() const
{
  return this->output_area_bytes_;
}

std::string ProfinetSession::firmwareName() const
{
  return this->firmware_name_;
}

std::string ProfinetSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
