cmake_minimum_required(VERSION 3.20)
project(scylla LANGUAGES C CXX)

# Product is Scylla Workbench only. The AppContainer CLI (scylla.exe) and its
# probe/fixture helpers live under archive/scylla-cage/ and are not built.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(scyllagpt)
