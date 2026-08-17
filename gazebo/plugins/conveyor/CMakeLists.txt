cmake_minimum_required(VERSION 3.16)

project(VirtualFactoryConveyor)

find_package(gz-cmake3 REQUIRED)

find_package(gz-plugin2 REQUIRED COMPONENTS register)

find_package(gz-sim8 REQUIRED)

add_library(ConveyorSystem SHARED
    ConveyorSystem.cc
)

set_property(TARGET ConveyorSystem PROPERTY CXX_STANDARD 17)
set_property(TARGET ConveyorSystem PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(ConveyorSystem
    PRIVATE
        gz-plugin2::gz-plugin2
        gz-sim8::gz-sim8
)
