cmake_minimum_required(VERSION 3.16)

project(virtual_factory_industrial)

find_package(open62541 REQUIRED)

add_library(virtual_factory_industrial STATIC
    src/MockIndustrialAdapter.cc
    src/OpcUaIndustrialAdapter.cc
)

target_include_directories(virtual_factory_industrial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(virtual_factory_industrial
    PUBLIC
        virtual_factory_equipment
    PRIVATE
        open62541::open62541
)

# Static library consumers must link open62541 for the OPC UA adapter objects.
target_link_libraries(virtual_factory_industrial
    INTERFACE
        open62541::open62541
)

set_target_properties(virtual_factory_industrial PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
