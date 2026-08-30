#include "profibus_session.hh"

#include "hilscher_availability.hh"

namespace virtual_factory
{
namespace internal
{

ProfibusSession::~ProfibusSession()
{
  this->close();
}

bool ProfibusSession::sdkAvailable() const
{
  return hilscherCifxSdkAvailable();
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

#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  this->last_error_ =
      "Hilscher cifX PROFIBUS backend not yet integrated (SDK present)";
  return false;
#else
  this->last_error_ = hilscherCifxUnavailableReason();
  return false;
#endif
}

void ProfibusSession::close()
{
  this->open_ = false;
}

bool ProfibusSession::connected() const
{
  return this->open_;
}

bool ProfibusSession::readInputArea(
    std::size_t /*byteOffset*/,
    std::size_t /*length*/,
    std::vector<std::uint8_t> * /*out*/)
{
  this->last_error_ = "PROFIBUS session not connected";
  return false;
}

bool ProfibusSession::writeOutputArea(
    std::size_t /*byteOffset*/,
    const std::vector<std::uint8_t> & /*data*/)
{
  this->last_error_ = "PROFIBUS session not connected";
  return false;
}

std::string ProfibusSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
