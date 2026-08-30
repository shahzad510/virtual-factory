cmake_minimum_required(VERSION 3.16)

project(virtual_factory_icp)

# ICP-1A runtime + ICP-1B persistent configuration (JSON) + Application API/GUI host.
# Does not depend on MES. Does not implement CIC/Designer.
# nlohmann is header-only; Hilscher is not required to parse or save configuration.
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
    PATHS
        /usr/include
        "${CMAKE_SOURCE_DIR}/.deps/nlohmann-json/usr/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/nlohmann-json/usr/include"
)
if (NOT NLOHMANN_JSON_INCLUDE_DIR)
  message(FATAL_ERROR
      "nlohmann/json.hpp not found. Install nlohmann-json3-dev or extract it to .deps/nlohmann-json")
endif()

add_library(virtual_factory_icp STATIC
    src/LiveStateCache.cc
    src/AdapterManager.cc
    src/PollScheduler.cc
    src/AdapterFactory.cc
    src/config/ConfigurationValidator.cc
    src/config/JsonFileConfigurationRepository.cc
    src/config/ConfigurationCatalog.cc
    src/config/NativeFieldbusConfigMapper.cc
    src/app/ApplicationService.cc
    src/app/HttpApiServer.cc
)

target_include_directories(virtual_factory_icp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${NLOHMANN_JSON_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
        ${CMAKE_SOURCE_DIR}/industrial/src
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

add_executable(icp_server
    src/app/icp_server_main.cc
)
target_link_libraries(icp_server PRIVATE virtual_factory_icp Threads::Threads)
target_compile_definitions(icp_server
    PRIVATE
        VF_ICP_GUI_ROOT="${CMAKE_SOURCE_DIR}/icp/gui"
)
set_target_properties(icp_server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
