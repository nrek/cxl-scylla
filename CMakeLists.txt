cmake_minimum_required(VERSION 3.20)
project(scylla LANGUAGES C CXX)

# Product: scylla.exe = WinUI 3 Fluent shell under scylla-fluent/.
# Native runtime: scylla-core.dll plus the trusted scylla-broker.exe helper.
# Archived AppContainer CLI lived as scylla.exe historically under archive/scylla-cage/.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(scyllagpt)
