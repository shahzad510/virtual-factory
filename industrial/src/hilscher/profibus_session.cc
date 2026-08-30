#include "profibus_session.hh"

#include "hilscher_availability.hh"

#include <algorithm>
#include <cctype>

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

}  // namespace

ProfibusSession::~ProfibusSession()
{
  this->close();
}

bool ProfibusSession::sdkAvailable() const
{
  return hilscherCifxSdkAvailable();
}

bool ProfibusSession::matchesFirmware(const std::string &name) const
{
  const std::string upper = toUpper(name);
  if (!this->config_.expectedFirmwareName.empty())
  {
    return upper.find(toUpper(this->config_.expectedFirmwareName)) != std::string::npos;
  }
  return upper.find("PROFIBUS") != std::string::npos
         || upper.find("DPM") != std::string::npos
         || upper.find("DP MASTER") != std::string::npos;
}

bool ProfibusSession::open(const ProfibusSessionConfig &config)
{
  this->close();
  this->config_ = config;

  if (config.configArtifactPath.empty())
  {
    this->last_error_ = "missing PROFIBUS configuration artifact path";
    return false;
  }

  if (!this->sdkAvailable())
  {
    this->last_error_ = hilscherCifxUnavailableReason();
    return false;
  }

  std::string error;
  if (!this->channel_.open(config.boardId, config.channel, &error))
  {
    this->last_error_ = error;
    return false;
  }

  CifxChannelInfo info;
  if (this->channel_.queryInfo(&info, &error))
  {
    this->firmware_name_ = info.firmwareName;
    if (!info.firmwareName.empty() && !this->matchesFirmware(info.firmwareName))
    {
      this->channel_.close();
      this->last_error_ =
          "cifX firmware is not a PROFIBUS DP Master (got '"
          + info.firmwareName
          + "'). Load CIFXDPM / NXLFW-DPM. HARDWARE VALIDATION PENDING.";
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

  this->open_ = true;
  this->last_error_.clear();
  return true;
}

void ProfibusSession::close()
{
  if (this->channel_.isOpen())
  {
    std::string ignored;
    this->channel_.setBusOn(false, 1000, &ignored);
    this->channel_.setHostReady(false, &ignored);
  }
  this->channel_.close();
  this->open_ = false;
  this->firmware_name_.clear();
}

bool ProfibusSession::connected() const
{
  return this->open_;
}

bool ProfibusSession::readInputArea(
    std::size_t byteOffset,
    std::size_t length,
    std::vector<std::uint8_t> *out)
{
  if (!this->open_)
  {
    this->last_error_ = "PROFIBUS session not connected";
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

bool ProfibusSession::writeOutputArea(
    std::size_t byteOffset, const std::vector<std::uint8_t> &data)
{
  if (!this->open_)
  {
    this->last_error_ = "PROFIBUS session not connected";
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

std::string ProfibusSession::firmwareName() const
{
  return this->firmware_name_;
}

std::string ProfibusSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
