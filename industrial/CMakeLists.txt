cmake_minimum_required(VERSION 3.16)

project(virtual_factory_industrial)

find_package(open62541 REQUIRED)
find_package(PkgConfig REQUIRED)

# Prefer a workspace-local extract of the Ubuntu libmodbus package when
# the system package is not installed (sudo apt install libmodbus-dev).
set(_vf_libmodbus_pc
    "${CMAKE_SOURCE_DIR}/.deps/libmodbus/usr/lib/x86_64-linux-gnu/pkgconfig")
if (EXISTS "${_vf_libmodbus_pc}/libmodbus.pc")
  set(ENV{PKG_CONFIG_PATH}
      "${_vf_libmodbus_pc}:$ENV{PKG_CONFIG_PATH}")
endif()
pkg_check_modules(LIBMODBUS REQUIRED libmodbus)
pkg_check_modules(LIBCURL REQUIRED libcurl)

# Prefer a workspace-local extract of Ubuntu nlohmann-json3-dev when the
# system package is not installed (sudo apt install nlohmann-json3-dev).
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
    PATHS
        /usr/include
        "${CMAKE_SOURCE_DIR}/.deps/nlohmann-json/usr/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/nlohmann-json/usr/include"
    NO_DEFAULT_PATH
)
if (NOT NLOHMANN_JSON_INCLUDE_DIR)
  find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
endif()
if (NOT NLOHMANN_JSON_INCLUDE_DIR)
  message(FATAL_ERROR
      "nlohmann/json.hpp not found. Install nlohmann-json3-dev or extract it to .deps/nlohmann-json")
endif()

add_library(virtual_factory_industrial STATIC
    src/MockIndustrialAdapter.cc
    src/OpcUaIndustrialAdapter.cc
    src/ModbusIndustrialAdapter.cc
    src/modbus_tcp_session.cc
    src/RestIndustrialAdapter.cc
    src/http_session.cc
)

target_include_directories(virtual_factory_industrial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${LIBMODBUS_INCLUDE_DIRS}
        ${LIBCURL_INCLUDE_DIRS}
        ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(virtual_factory_industrial
    PUBLIC
        virtual_factory_equipment
    PRIVATE
        open62541::open62541
        ${LIBMODBUS_LIBRARIES}
        ${LIBCURL_LIBRARIES}
)

# Static library consumers must link protocol libraries used by adapter objects.
target_link_libraries(virtual_factory_industrial
    INTERFACE
        open62541::open62541
        ${LIBMODBUS_LIBRARIES}
        ${LIBCURL_LIBRARIES}
)

if (LIBMODBUS_LIBRARY_DIRS)
  target_link_directories(virtual_factory_industrial
      INTERFACE
          ${LIBMODBUS_LIBRARY_DIRS}
  )
  set_property(
      TARGET virtual_factory_industrial
      PROPERTY BUILD_RPATH "${LIBMODBUS_LIBRARY_DIRS}")
endif()

# Visible to the root test target that compiles the libmodbus slave fixture.
set(VIRTUAL_FACTORY_LIBMODBUS_INCLUDE_DIRS
    "${LIBMODBUS_INCLUDE_DIRS}" PARENT_SCOPE)
set(VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS
    "${LIBMODBUS_LIBRARY_DIRS}" PARENT_SCOPE)

set_target_properties(virtual_factory_industrial PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
