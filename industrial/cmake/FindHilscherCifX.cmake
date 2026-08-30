# Locate Hilscher cifX user-space API (libcifx / NXDRV-WIN import lib).
#
# Official Linux source: https://github.com/HilscherAutomation/nxdrvlinux
# Install prefix typically contains:
#   include/cifx/cifXUser.h
#   include/cifx/cifxlinux.h
#   lib/libcifx.so
#
# Feature flags default OFF. Finding the SDK does NOT enable native fieldbus.
# Set VF_ENABLE_HILSCHER_PROFINET and/or VF_ENABLE_HILSCHER_PROFIBUS to ON
# AND provide HILSCHER_CIFX_ROOT (or a standard install) to compile the real
# cifX backend.

option(VF_ENABLE_HILSCHER_PROFINET
    "Compile real Hilscher cifX PROFINET backend (requires libcifx)" OFF)
option(VF_ENABLE_HILSCHER_PROFIBUS
    "Compile real Hilscher cifX PROFIBUS backend (requires libcifx)" OFF)

set(HILSCHER_CIFX_ROOT "" CACHE PATH
    "Prefix containing include/cifx/cifXUser.h and lib/libcifx")

set(_VF_HILSCHER_HINTS
    "$ENV{HILSCHER_CIFX_ROOT}"
    "$ENV{CIFX_SDK_ROOT}"
    "${HILSCHER_CIFX_ROOT}"
    "/opt/cifx"
    "/usr/local"
    "/usr"
    "${CMAKE_SOURCE_DIR}/.deps/libcifx"
)

find_path(HILSCHER_CIFX_INCLUDE_DIR
    NAMES cifXUser.h
    HINTS ${_VF_HILSCHER_HINTS}
    PATH_SUFFIXES include include/cifx cifx)

find_library(HILSCHER_CIFX_LIBRARY
    NAMES cifx libcifx cifX32 cifX64
    HINTS ${_VF_HILSCHER_HINTS}
    PATH_SUFFIXES lib lib64 x86_64-linux-gnu)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    HilscherCifX
    DEFAULT_MSG
    HILSCHER_CIFX_INCLUDE_DIR
    HILSCHER_CIFX_LIBRARY)

set(VF_HILSCHER_CIFX_FOUND FALSE)
if (HilscherCifX_FOUND)
  set(VF_HILSCHER_CIFX_FOUND TRUE)
endif()

# Only compile/link the real backend when a feature flag is ON.
set(VF_HILSCHER_CIFX_AVAILABLE FALSE)
if (VF_HILSCHER_CIFX_FOUND
    AND (VF_ENABLE_HILSCHER_PROFINET OR VF_ENABLE_HILSCHER_PROFIBUS))
  set(VF_HILSCHER_CIFX_AVAILABLE TRUE)
  message(STATUS
      "Hilscher cifX SDK: ${HILSCHER_CIFX_LIBRARY} "
      "(include ${HILSCHER_CIFX_INCLUDE_DIR}); real backend ENABLED")
elseif (VF_ENABLE_HILSCHER_PROFINET OR VF_ENABLE_HILSCHER_PROFIBUS)
  message(WARNING
      "VF_ENABLE_HILSCHER_PROFINET/PROFIBUS is ON but cifX SDK was not found. "
      "Adapters will compile with stub backends (BLOCKED BY SDK). "
      "Set HILSCHER_CIFX_ROOT to the libcifx install prefix "
      "(cifXUser.h + libcifx). Do not commit proprietary firmware.")
elseif (VF_HILSCHER_CIFX_FOUND)
  message(STATUS
      "Hilscher cifX SDK found but VF_ENABLE_HILSCHER_PROFINET/PROFIBUS are "
      "OFF — adapters use stub backends. SDK: ${HILSCHER_CIFX_LIBRARY}")
endif()
