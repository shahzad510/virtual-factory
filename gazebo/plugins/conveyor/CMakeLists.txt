cmake_minimum_required(VERSION 3.16)

project(VirtualFactoryConveyor)

find_package(gz-cmake3 REQUIRED)

find_package(gz-plugin2 REQUIRED COMPONENTS register)

find_package(gz-sim8 REQUIRED)

# Gazebo-independent equipment contract (SoT Phase 5).
if (NOT TARGET virtual_factory_equipment)
  add_subdirectory(
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../equipment"
      "${CMAKE_CURRENT_BINARY_DIR}/equipment")
endif()

add_library(ConveyorSystem SHARED
    ConveyorSystem.cc
)

set_property(TARGET ConveyorSystem PROPERTY CXX_STANDARD 17)
set_property(TARGET ConveyorSystem PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(ConveyorSystem
    PRIVATE
        virtual_factory_equipment
        gz-plugin2::gz-plugin2
        gz-sim8::gz-sim8
)
