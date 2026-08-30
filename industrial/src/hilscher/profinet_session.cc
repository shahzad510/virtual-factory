#include "profinet_session.hh"

#include "hilscher_availability.hh"

namespace virtual_factory
{
namespace internal
{

ProfinetSession::~ProfinetSession()
{
  this->close();
}

bool ProfinetSession::sdkAvailable() const
{
  return hilscherCifxSdkAvailable();
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

#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  this->last_error_ =
      "Hilscher cifX PROFINET backend not yet integrated (SDK present)";
  return false;
#else
  this->last_error_ = hilscherCifxUnavailableReason();
  return false;
#endif
}

void ProfinetSession::close()
{
#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  // impl teardown when real backend lands
#endif
  this->open_ = false;
}

bool ProfinetSession::connected() const
{
  return this->open_;
}

bool ProfinetSession::readInputArea(
    std::size_t /*byteOffset*/,
    std::size_t /*length*/,
    std::vector<std::uint8_t> * /*out*/)
{
  this->last_error_ = "PROFINET session not connected";
  return false;
}

bool ProfinetSession::writeOutputArea(
    std::size_t /*byteOffset*/,
    const std::vector<std::uint8_t> & /*data*/)
{
  this->last_error_ = "PROFINET session not connected";
  return false;
}

std::string ProfinetSession::lastError() const
{
  return this->last_error_;
}

}  // namespace internal
}  // namespace virtual_factory
