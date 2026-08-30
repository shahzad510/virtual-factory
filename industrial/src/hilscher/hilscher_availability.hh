#ifndef VIRTUAL_FACTORY_HILSCHER_AVAILABILITY_HH_
#define VIRTUAL_FACTORY_HILSCHER_AVAILABILITY_HH_

namespace virtual_factory
{
namespace internal
{

/// True when CMake detected cifX headers/libs (VF_HILSCHER_CIFX_AVAILABLE).
bool hilscherCifxSdkAvailable();

const char *hilscherCifxUnavailableReason();

}  // namespace internal
}  // namespace virtual_factory

#endif
