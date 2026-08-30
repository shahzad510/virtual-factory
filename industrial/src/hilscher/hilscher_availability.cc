#include "hilscher_availability.hh"

namespace virtual_factory
{
namespace internal
{

bool hilscherCifxSdkAvailable()
{
#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  return true;
#else
  return false;
#endif
}

const char *hilscherCifxUnavailableReason()
{
#if defined(VF_HILSCHER_CIFX_AVAILABLE) && VF_HILSCHER_CIFX_AVAILABLE
  return "";
#else
  return "Hilscher cifX SDK/hardware not available in this build "
         "(VF_HILSCHER_CIFX_AVAILABLE=0). Native fieldbus integration is "
         "BLOCKED BY SDK/HARDWARE.";
#endif
}

}  // namespace internal
}  // namespace virtual_factory
