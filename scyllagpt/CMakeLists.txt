cmake_minimum_required(VERSION 3.20)
project(scyllagpt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(third_party)

set(SCYLLAGPT_SOURCES
    src/protocol/json.cpp
    src/platform/utf.cpp
    src/platform/paths.cpp
    src/storage/settings.cpp
    src/storage/store.cpp
    src/workspace/document.cpp
    src/runtime/runtime.cpp
    src/domain/session.cpp
    src/domain/providers.cpp
    src/ui/layout.cpp
    src/ui/theme.cpp
    src/ui/language.cpp
    src/ui/editor_host.cpp
    src/ui/commands.cpp
    src/ui/window.cpp
    src/app/main.cpp
    src/app/scyllagpt.rc
)

add_executable(scyllagpt WIN32 ${SCYLLAGPT_SOURCES})
target_include_directories(scyllagpt PRIVATE
    include
    third_party/scintilla/include
    third_party/lexilla/include
)
target_link_libraries(scyllagpt PRIVATE
    scintilla_static
    lexilla_static
    comctl32
    comdlg32
    shell32
    uxtheme
    dwmapi
    ole32
    uuid
    version
    user32
    gdi32
    imm32
    advapi32
    gdiplus
)
set_target_properties(scyllagpt PROPERTIES
    OUTPUT_NAME "scylla-workbench"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

add_executable(scyllagpt-tests
    tests/test_framing.cpp
    src/protocol/json.cpp
    src/platform/utf.cpp
    src/ui/layout.cpp
    src/ui/language.cpp
    src/storage/store.cpp
    src/workspace/document.cpp
    src/ui/theme.cpp
)
target_include_directories(scyllagpt-tests PRIVATE include)
target_link_libraries(scyllagpt-tests PRIVATE rpcrt4 uxtheme dwmapi)
set_target_properties(scyllagpt-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)
