cmake_minimum_required(VERSION 3.16)

project(virtual_factory_equipment)

add_library(virtual_factory_equipment STATIC
    src/Conveyor.cc
    src/GenericEquipment.cc
)

target_include_directories(virtual_factory_equipment
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(virtual_factory_equipment PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(equipment_test
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/equipment_test.cc
  )
  target_link_libraries(equipment_test PRIVATE virtual_factory_equipment)
  add_test(NAME equipment_test COMMAND equipment_test)
endif()
