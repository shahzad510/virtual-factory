cmake_minimum_required(VERSION 3.16)

project(virtual_factory)

# Gazebo-independent industrial equipment contract (SoT Phase 5)
# and industrial adapter layer (SoT Phase 6).
# The conveyor Gazebo plugin is still built from
# gazebo/plugins/conveyor so existing plugin build commands keep working.
add_subdirectory(equipment)
add_subdirectory(industrial)

if (VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS)
  set(CMAKE_BUILD_RPATH "${VIRTUAL_FACTORY_LIBMODBUS_LIBRARY_DIRS}")
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
set_tests_properties(opcua_multi_server_scalability_test PROPERTIES TIMEOUT 600)

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
