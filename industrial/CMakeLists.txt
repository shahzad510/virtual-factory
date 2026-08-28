cmake_minimum_required(VERSION 3.16)

project(virtual_factory_industrial)

# Prefer workspace-local open62541 when the system package is not installed.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.deps/open62541")
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

# Prefer a workspace-local build of libplctag v2.7.1 when the system package
# is not installed (build from .deps/libplctag-src into .deps/libplctag).
set(_vf_libplctag_pc
    "${CMAKE_SOURCE_DIR}/.deps/libplctag/lib/pkgconfig")
if (EXISTS "${_vf_libplctag_pc}/libplctag.pc")
  set(ENV{PKG_CONFIG_PATH}
      "${_vf_libplctag_pc}:$ENV{PKG_CONFIG_PATH}")
endif()
pkg_check_modules(LIBPLCTAG REQUIRED libplctag)

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

# Prefer a workspace-local extract of Ubuntu libpaho-mqtt-dev when the
# system package is not installed (sudo apt install libpaho-mqtt-dev).
find_path(PAHO_MQTT_INCLUDE_DIR MQTTAsync.h
    PATHS
        /usr/include
        "${CMAKE_SOURCE_DIR}/.deps/paho-mqtt/usr/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/paho-mqtt/usr/include"
)
unset(PAHO_MQTT_LIBRARY CACHE)
find_library(PAHO_MQTT_LIBRARY NAMES paho-mqtt3as
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        "${CMAKE_SOURCE_DIR}/.deps/paho-mqtt/usr/lib/x86_64-linux-gnu"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/paho-mqtt/usr/lib/x86_64-linux-gnu"
)
if (NOT PAHO_MQTT_INCLUDE_DIR OR NOT PAHO_MQTT_LIBRARY)
  message(FATAL_ERROR
      "Eclipse Paho MQTT C (MQTTAsync.h / libpaho-mqtt3as) not found. "
      "Install libpaho-mqtt-dev or extract it to .deps/paho-mqtt")
endif()
get_filename_component(PAHO_MQTT_LIBRARY_DIR "${PAHO_MQTT_LIBRARY}" DIRECTORY)

add_library(virtual_factory_industrial STATIC
    src/MockIndustrialAdapter.cc
    src/OpcUaIndustrialAdapter.cc
    src/ModbusIndustrialAdapter.cc
    src/modbus_tcp_session.cc
    src/RestIndustrialAdapter.cc
    src/http_session.cc
    src/MqttIndustrialAdapter.cc
    src/mqtt_session.cc
    src/EtherNetIpIndustrialAdapter.cc
    src/eip_session.cc
)

target_include_directories(virtual_factory_industrial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${LIBMODBUS_INCLUDE_DIRS}
        ${LIBCURL_INCLUDE_DIRS}
        ${NLOHMANN_JSON_INCLUDE_DIR}
        ${PAHO_MQTT_INCLUDE_DIR}
        ${LIBPLCTAG_INCLUDE_DIRS}
)

target_link_libraries(virtual_factory_industrial
    PUBLIC
        virtual_factory_equipment
    PRIVATE
        open62541::open62541
        ${LIBMODBUS_LIBRARIES}
        ${LIBCURL_LIBRARIES}
        ${PAHO_MQTT_LIBRARY}
        ${LIBPLCTAG_LIBRARIES}
)

# Static library consumers must link protocol libraries used by adapter objects.
target_link_libraries(virtual_factory_industrial
    INTERFACE
        open62541::open62541
        ${LIBMODBUS_LIBRARIES}
        ${LIBCURL_LIBRARIES}
        ${PAHO_MQTT_LIBRARY}
        ${LIBPLCTAG_LIBRARIES}
)

if (LIBMODBUS_LIBRARY_DIRS OR PAHO_MQTT_LIBRARY_DIR OR LIBPLCTAG_LIBRARY_DIRS)
  set(_vf_industrial_rpath "")
  if (LIBMODBUS_LIBRARY_DIRS)
    list(APPEND _vf_industrial_rpath ${LIBMODBUS_LIBRARY_DIRS})
    target_link_directories(virtual_factory_industrial
        INTERFACE
            ${LIBMODBUS_LIBRARY_DIRS}
    )
  endif()
  if (PAHO_MQTT_LIBRARY_DIR)
    list(APPEND _vf_industrial_rpath ${PAHO_MQTT_LIBRARY_DIR})
    target_link_directories(virtual_factory_industrial
        INTERFACE
            ${PAHO_MQTT_LIBRARY_DIR}
    )
  endif()
  if (LIBPLCTAG_LIBRARY_DIRS)
    list(APPEND _vf_industrial_rpath ${LIBPLCTAG_LIBRARY_DIRS})
    target_link_directories(virtual_factory_industrial
        INTERFACE
            ${LIBPLCTAG_LIBRARY_DIRS}
    )
  endif()
  set_property(
      TARGET virtual_factory_industrial
      PROPERTY BUILD_RPATH "${_vf_industrial_rpath}")
endif()

# Visible to the root test target that compiles the libmodbus slave fixture.
set(VIRTUAL_FACTORY_LIBMODBUS_INCLUDE_DIRS
    "${LIBMODBUS_INCLUDE_DIRS}" PARENT_SCOPE)
set(VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS
    "${LIBMODBUS_LIBRARY_DIRS}" PARENT_SCOPE)
set(VIRTUAL_FACTORY_PAHO_MQTT_INCLUDE_DIR
    "${PAHO_MQTT_INCLUDE_DIR}" PARENT_SCOPE)
set(VIRTUAL_FACTORY_PAHO_MQTT_LIBRARY_DIR
    "${PAHO_MQTT_LIBRARY_DIR}" PARENT_SCOPE)
set(VIRTUAL_FACTORY_LIBPLCTAG_LIBRARY_DIRS
    "${LIBPLCTAG_LIBRARY_DIRS}" PARENT_SCOPE)

set_target_properties(virtual_factory_industrial PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
