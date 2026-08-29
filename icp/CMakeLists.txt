cmake_minimum_required(VERSION 3.16)

project(virtual_factory_icp)

# ICP runtime foundation (ICP-1A). Links Phase 5–6 libraries.
# Does not depend on MES. Does not implement CIC/Designer.
add_library(virtual_factory_icp STATIC
    src/LiveStateCache.cc
    src/AdapterManager.cc
    src/PollScheduler.cc
    src/AdapterFactory.cc
)

target_include_directories(virtual_factory_icp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(virtual_factory_icp
    PUBLIC
        virtual_factory_industrial
)

set_target_properties(virtual_factory_icp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

find_package(Threads REQUIRED)
target_link_libraries(virtual_factory_icp PRIVATE Threads::Threads)
