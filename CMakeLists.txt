cmake_minimum_required(VERSION 3.20)
project(scylla LANGUAGES C CXX)

# Primary product (future): scylla.exe = WinUI 3 Fluent shell under scylla-fluent/.
# Optional Win32: scylla-workbench.exe (CMake target scyllagpt) for runas / limited-user.
# Shared domain: scylla-core (static today; DLL export path via scylla_core_c.h).
# Archived AppContainer CLI lived as scylla.exe historically under archive/scylla-cage/.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(scyllagpt)
