cmake_minimum_required(VERSION 3.20)
project(scylla LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX)

add_library(scylla_core STATIC
    src/scylla/path.cpp
    src/scylla/acl.cpp
    src/scylla/appcontainer.cpp
    src/scylla/process_scan.cpp
    src/scylla/session.cpp
    src/scylla/discover.cpp
    src/scylla/sandbox_probe.cpp
    src/scylla/identity.cpp
)
target_include_directories(scylla_core PUBLIC src/scylla)
target_link_libraries(scylla_core PUBLIC userenv advapi32 ole32 uuid shell32 shlwapi netapi32 psapi iphlpapi)

add_executable(scylla src/scylla/main.cpp)
target_link_libraries(scylla PRIVATE scylla_core)

add_executable(scylla-probe src/probe/main.cpp)
target_link_libraries(scylla-probe PRIVATE advapi32 winhttp)

add_executable(scylla-test-parent src/test_parent/main.cpp)

add_subdirectory(scyllagpt)

find_program(DOTNET_EXE dotnet)
if(DOTNET_EXE)
    add_custom_target(scylla-ui
        COMMAND ${DOTNET_EXE} build "${CMAKE_SOURCE_DIR}/src/ui/Scylla.UI.csproj" -c Release
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/src/ui"
        COMMENT "Build Scylla.UI (WPF)"
    )
endif()
