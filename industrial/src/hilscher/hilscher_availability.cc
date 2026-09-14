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
  return "Hilscher cifX SDK not compiled into this build "
         "(VF_ENABLE_HILSCHER_PROFINET/PROFIBUS OFF or SDK not found; "
         "VF_HILSCHER_CIFX_AVAILABLE=0). Native fieldbus is "
         "BLOCKED BY SDK/HARDWARE.";
#endif
}

}  // namespace internal
}  // namespace virtual_factory
