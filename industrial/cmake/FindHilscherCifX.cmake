# Locate Hilscher cifX user-space API (libcifx / cifX API DLL import lib).
#
# Set VF_ENABLE_HILSCHER_PROFINET or VF_ENABLE_HILSCHER_PROFIBUS to ON to
# attempt linking. When headers/libs are missing, adapters compile with stub
# backends (VF_HILSCHER_CIFX_AVAILABLE=0).

option(VF_ENABLE_HILSCHER_PROFINET
    "Enable Hilscher cifX PROFINET backend (requires SDK on host)" OFF)
option(VF_ENABLE_HILSCHER_PROFIBUS
    "Enable Hilscher cifX PROFIBUS backend (requires SDK on host)" OFF)

set(_VF_HILSCHER_HINTS
    "$ENV{HILSCHER_CIFX_ROOT}"
    "$ENV{CIFX_SDK_ROOT}"
    "/opt/cifx"
    "/usr/local/cifx"
    "${CMAKE_SOURCE_DIR}/.deps/hilscher-cifx"
)

find_path(HILSCHER_CIFX_INCLUDE_DIR
    NAMES cifXAPI.h
    HINTS ${_VF_HILSCHER_HINTS}
    PATH_SUFFIXES include inc API)

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

if (HilscherCifX_FOUND)
  set(VF_HILSCHER_CIFX_AVAILABLE TRUE)
  message(STATUS "Hilscher cifX SDK found: ${HILSCHER_CIFX_LIBRARY}")
else()
  set(VF_HILSCHER_CIFX_AVAILABLE FALSE)
  if (VF_ENABLE_HILSCHER_PROFINET OR VF_ENABLE_HILSCHER_PROFIBUS)
    message(WARNING
        "Hilscher cifX SDK not found. Native PROFINET/PROFIBUS adapters will "
        "compile with stub backends (BLOCKED BY SDK/HARDWARE). Set "
        "HILSCHER_CIFX_ROOT or install NXDRV + development headers.")
  endif()
endif()
