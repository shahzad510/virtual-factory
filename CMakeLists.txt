cmake_minimum_required(VERSION 3.16)

project(virtual_factory)

# Gazebo-independent industrial equipment contract (SoT Phase 5)
# and industrial adapter layer (SoT Phase 6).
# ICP runtime foundation (ICP-1A) lives under icp/.
# The conveyor Gazebo plugin is still built from
# gazebo/plugins/conveyor so existing plugin build commands keep working.
add_subdirectory(equipment)
add_subdirectory(industrial)
add_subdirectory(icp)

if (VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS OR VIRTUAL_FACTORY_PAHO_MQTT_LIBRARY_DIR OR VIRTUAL_FACTORY_LIBPLCTAG_LIBRARY_DIRS)
  set(CMAKE_BUILD_RPATH
      "${VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS};${VIRTUAL_FACTORY_PAHO_MQTT_LIBRARY_DIR};${VIRTUAL_FACTORY_LIBPLCTAG_LIBRARY_DIRS}")
endif()

enable_testing()

add_executable(equipment_test
    tests/equipment_test.cc
)
target_link_libraries(equipment_test PRIVATE virtual_factory_equipment)
add_test(NAME equipment_test COMMAND equipment_test)

add_executable(industrial_adapter_test
    tests/industrial_adapter_test.cc
)
target_link_libraries(industrial_adapter_test PRIVATE virtual_factory_industrial)
add_test(NAME industrial_adapter_test COMMAND industrial_adapter_test)

add_executable(opcua_adapter_test
    tests/opcua_adapter_test.cc
    tests/opcua_test_server.cc
)
target_include_directories(opcua_adapter_test PRIVATE tests)
target_link_libraries(opcua_adapter_test PRIVATE virtual_factory_industrial)
add_test(NAME opcua_adapter_test COMMAND opcua_adapter_test)

add_executable(opcua_multi_server_scalability_test
    tests/opcua_multi_server_scalability_test.cc
    tests/opcua_scale_plc_server.cc
)
target_include_directories(opcua_multi_server_scalability_test PRIVATE tests)
target_link_libraries(opcua_multi_server_scalability_test PRIVATE virtual_factory_industrial)
add_test(NAME opcua_multi_server_scalability_test COMMAND opcua_multi_server_scalability_test)
set_tests_properties(opcua_multi_server_scalability_test PROPERTIES TIMEOUT 1200)

add_executable(modbus_adapter_test
    tests/modbus_adapter_test.cc
    tests/modbus_test_server.cc
)
target_include_directories(modbus_adapter_test
    PRIVATE
        tests
        ${VIRTUAL_FACTORY_LIBMODBUS_INCLUDE_DIRS}
)
target_link_libraries(modbus_adapter_test PRIVATE virtual_factory_industrial pthread)
if (VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS)
  set_property(
      TARGET modbus_adapter_test
      PROPERTY BUILD_RPATH "${VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS}")
endif()
add_test(NAME modbus_adapter_test COMMAND modbus_adapter_test)

add_executable(rest_adapter_test
    tests/rest_adapter_test.cc
    tests/rest_test_server.cc
)
target_include_directories(rest_adapter_test PRIVATE tests)
target_link_libraries(rest_adapter_test PRIVATE virtual_factory_industrial pthread)
add_test(NAME rest_adapter_test COMMAND rest_adapter_test)

find_program(MOSQUITTO_EXECUTABLE mosquitto
    PATHS
        /usr/sbin
        /usr/bin
        "${CMAKE_SOURCE_DIR}/.deps/mosquitto/usr/sbin"
)
find_program(MOSQUITTO_PASSWD mosquitto_passwd
    PATHS
        /usr/bin
        "${CMAKE_SOURCE_DIR}/.deps/mosquitto/usr/bin"
)
set(MOSQUITTO_LIBDIR "")
if (MOSQUITTO_EXECUTABLE)
  get_filename_component(_mosq_prefix "${MOSQUITTO_EXECUTABLE}" DIRECTORY)
  get_filename_component(_mosq_prefix "${_mosq_prefix}" DIRECTORY)
  if (EXISTS "${_mosq_prefix}/lib/x86_64-linux-gnu")
    set(MOSQUITTO_LIBDIR "${_mosq_prefix}/lib/x86_64-linux-gnu")
  endif()
endif()
if (NOT MOSQUITTO_EXECUTABLE)
  message(FATAL_ERROR
      "mosquitto broker not found for mqtt_adapter_test. "
      "Install mosquitto or extract it to .deps/mosquitto "
      "(DEVELOPMENT/INTEGRATION VALIDATION ONLY).")
endif()

add_executable(mqtt_adapter_test
    tests/mqtt_adapter_test.cc
    tests/mqtt_test_broker.cc
)
target_include_directories(mqtt_adapter_test
    PRIVATE
        tests
        ${VIRTUAL_FACTORY_PAHO_MQTT_INCLUDE_DIR}
)
target_compile_definitions(mqtt_adapter_test
    PRIVATE
        VF_MOSQUITTO_EXECUTABLE="${MOSQUITTO_EXECUTABLE}"
        VF_MOSQUITTO_PASSWD="${MOSQUITTO_PASSWD}"
        VF_MOSQUITTO_LIBDIR="${MOSQUITTO_LIBDIR}"
)
find_library(PAHO_MQTT_SYNC_LIBRARY paho-mqtt3cs
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        "${CMAKE_SOURCE_DIR}/.deps/paho-mqtt/usr/lib/x86_64-linux-gnu"
)
if (NOT PAHO_MQTT_SYNC_LIBRARY)
  message(FATAL_ERROR
      "libpaho-mqtt3cs not found for mqtt_adapter_test helper client")
endif()
get_filename_component(PAHO_MQTT_SYNC_LIBRARY_DIR
    "${PAHO_MQTT_SYNC_LIBRARY}" DIRECTORY)
target_link_libraries(mqtt_adapter_test
    PRIVATE
        virtual_factory_industrial
        pthread
        ${PAHO_MQTT_SYNC_LIBRARY}
)
set(_mqtt_test_rpath "${VIRTUAL_FACTORY_PAHO_MQTT_LIBRARY_DIR}")
if (PAHO_MQTT_SYNC_LIBRARY_DIR)
  list(APPEND _mqtt_test_rpath "${PAHO_MQTT_SYNC_LIBRARY_DIR}")
endif()
set_property(
    TARGET mqtt_adapter_test
    PROPERTY BUILD_RPATH "${_mqtt_test_rpath}")
add_test(NAME mqtt_adapter_test COMMAND mqtt_adapter_test)
set_tests_properties(mqtt_adapter_test PROPERTIES TIMEOUT 120)

add_executable(mqtt_multi_equipment_scalability_test
    tests/mqtt_multi_equipment_scalability_test.cc
    tests/mqtt_test_broker.cc
)
target_include_directories(mqtt_multi_equipment_scalability_test
    PRIVATE
        tests
        ${VIRTUAL_FACTORY_PAHO_MQTT_INCLUDE_DIR}
)
target_compile_definitions(mqtt_multi_equipment_scalability_test
    PRIVATE
        VF_MOSQUITTO_EXECUTABLE="${MOSQUITTO_EXECUTABLE}"
        VF_MOSQUITTO_PASSWD="${MOSQUITTO_PASSWD}"
        VF_MOSQUITTO_LIBDIR="${MOSQUITTO_LIBDIR}"
)
target_link_libraries(mqtt_multi_equipment_scalability_test
    PRIVATE
        virtual_factory_industrial
        pthread
        ${PAHO_MQTT_SYNC_LIBRARY}
)
set_property(
    TARGET mqtt_multi_equipment_scalability_test
    PROPERTY BUILD_RPATH "${_mqtt_test_rpath}")
add_test(NAME mqtt_multi_equipment_scalability_test
    COMMAND mqtt_multi_equipment_scalability_test)
set_tests_properties(mqtt_multi_equipment_scalability_test PROPERTIES TIMEOUT 900)

find_program(AB_SERVER_EXECUTABLE ab_server
    PATHS
        "${CMAKE_SOURCE_DIR}/.deps/libplctag/bin"
        /usr/bin
        /usr/local/bin
)
set(LIBPLCTAG_LIBDIR "")
if (AB_SERVER_EXECUTABLE)
  get_filename_component(_ab_prefix "${AB_SERVER_EXECUTABLE}" DIRECTORY)
  get_filename_component(_ab_prefix "${_ab_prefix}" DIRECTORY)
  if (EXISTS "${_ab_prefix}/lib")
    set(LIBPLCTAG_LIBDIR "${_ab_prefix}/lib")
  endif()
endif()
if (NOT AB_SERVER_EXECUTABLE)
  message(FATAL_ERROR
      "libplctag ab_server not found for eip_adapter_test. "
      "Build libplctag v2.7.1 into .deps/libplctag "
      "(DEVELOPMENT/INTEGRATION VALIDATION ONLY).")
endif()

add_executable(eip_adapter_test
    tests/eip_adapter_test.cc
    tests/eip_test_server.cc
)
target_include_directories(eip_adapter_test PRIVATE tests)
target_compile_definitions(eip_adapter_test
    PRIVATE
        VF_AB_SERVER_EXECUTABLE="${AB_SERVER_EXECUTABLE}"
        VF_LIBPLCTAG_LIBRARY_DIR="${LIBPLCTAG_LIBDIR}"
)
target_link_libraries(eip_adapter_test PRIVATE virtual_factory_industrial pthread)
set(_eip_test_rpath "${VIRTUAL_FACTORY_LIBPLCTAG_LIBRARY_DIRS}")
if (LIBPLCTAG_LIBDIR)
  list(APPEND _eip_test_rpath "${LIBPLCTAG_LIBDIR}")
endif()
set_property(
    TARGET eip_adapter_test
    PROPERTY BUILD_RPATH "${_eip_test_rpath}")
add_test(NAME eip_adapter_test COMMAND eip_adapter_test)
set_tests_properties(eip_adapter_test PROPERTIES TIMEOUT 300)

add_executable(icp_runtime_test
    tests/icp_runtime_test.cc
    tests/opcua_test_server.cc
)
target_include_directories(icp_runtime_test PRIVATE tests)
target_link_libraries(icp_runtime_test PRIVATE virtual_factory_icp)
add_test(NAME icp_runtime_test COMMAND icp_runtime_test)
set_tests_properties(icp_runtime_test PROPERTIES TIMEOUT 120)

add_executable(native_fieldbus_scaffolding_test
    tests/native_fieldbus_scaffolding_test.cc
)
target_link_libraries(native_fieldbus_scaffolding_test
    PRIVATE virtual_factory_industrial virtual_factory_icp pthread)
add_test(NAME native_fieldbus_scaffolding_test
    COMMAND native_fieldbus_scaffolding_test)
set_tests_properties(native_fieldbus_scaffolding_test PROPERTIES TIMEOUT 60)
