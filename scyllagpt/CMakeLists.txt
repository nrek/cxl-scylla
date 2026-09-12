cmake_minimum_required(VERSION 3.20)
project(scyllagpt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(scyllagpt-markdown-tests tests/test_markdown_ui.cpp src/ui/ui_kit.cpp src/ui/ui_space.cpp src/ui/theme.cpp)
target_include_directories(scyllagpt-markdown-tests PRIVATE include)
target_link_libraries(scyllagpt-markdown-tests PRIVATE comctl32 user32 gdi32 uxtheme dwmapi)
set_target_properties(scyllagpt-markdown-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(third_party)

# Native STRATA extension uses the user's installed STRATA package/configuration.
foreach(config Release Debug RelWithDebInfo MinSizeRel)
    configure_file(runtime/strata_workbench_bridge.py
        "${CMAKE_BINARY_DIR}/${config}/strata_workbench_bridge.py" COPYONLY)
endforeach()

add_executable(scyllagpt-strata-tests
    tests/test_strata_ui.cpp src/ui/strata_settings_ui.cpp src/ui/ui_kit.cpp
    src/ui/ui_space.cpp src/ui/theme.cpp src/platform/utf.cpp src/platform/paths.cpp
    src/platform/file_io.cpp src/protocol/json.cpp src/domain/strata_bridge.cpp src/domain/strata_client.cpp
    src/domain/knowledge.cpp src/storage/store.cpp)
target_include_directories(scyllagpt-strata-tests PRIVATE include)
target_link_libraries(scyllagpt-strata-tests PRIVATE comctl32 user32 gdi32 shell32
    uxtheme dwmapi version advapi32 rpcrt4 winhttp)
set_target_properties(scyllagpt-strata-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")

set(SCYLLA_CORE_SOURCES
    src/protocol/acp_client.cpp
    src/protocol/json.cpp
    src/platform/utf.cpp
    src/platform/paths.cpp
    src/platform/file_io.cpp
    src/platform/single_instance.cpp
    src/storage/settings.cpp
    src/storage/store.cpp
    src/workspace/document.cpp
    src/runtime/runtime.cpp
    src/domain/session.cpp
    src/domain/project_context.cpp
    src/domain/providers.cpp
    src/domain/provider_http.cpp
    src/domain/keyring.cpp
    src/domain/project_environment.cpp
    src/domain/project_connection.cpp
    src/domain/connection_policy.cpp
    src/domain/connection_broker.cpp
    src/domain/ssh_query_executor.cpp
    src/domain/broker_service.cpp
    src/domain/broker_mcp.cpp
    src/domain/knowledge.cpp
    src/domain/strata_client.cpp
    src/domain/strata_bridge.cpp
    src/domain/mcp_manager.cpp
    src/domain/mcp_oauth.cpp
    src/domain/terminal_profiles.cpp
    src/core/scylla_core_c.cpp
)

set(SCYLLAGPT_UI_SOURCES
    src/ui/layout.cpp
    src/ui/theme.cpp
    src/ui/language.cpp
    src/ui/editor_host.cpp
    src/ui/terminal_host.cpp
    src/ui/terminal_session.cpp
    src/ui/workbench_panel.cpp
    src/ui/ui_space.cpp
    src/ui/ui_kit.cpp
    src/ui/ui_gallery.cpp
    src/ui/mcp_settings_ui.cpp
    src/ui/providers_settings_ui.cpp
    src/ui/keyring_ui.cpp
    src/ui/environment_settings_ui.cpp
    src/ui/connections_settings_ui.cpp
    src/ui/security_overview_ui.cpp
    src/ui/security_policy_ui.cpp
    src/ui/knowledge_settings_ui.cpp
    src/ui/strata_settings_ui.cpp
    src/ui/terminal_settings_ui.cpp
    src/ui/chat_log_view.cpp
    src/ui/commands.cpp
    src/ui/window.cpp
    src/app/main.cpp
    src/app/scyllagpt.rc
)

add_library(scylla-core STATIC ${SCYLLA_CORE_SOURCES})
target_compile_definitions(scylla-core PUBLIC SCYLLA_CORE_STATIC)
add_executable(scylla-broker src/app/broker_helper.cpp)
target_link_libraries(scylla-broker PRIVATE scylla-core)
set_target_properties(scylla-broker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
target_include_directories(scylla-core PUBLIC include third_party/argon2)
target_link_libraries(scylla-core PUBLIC
    argon2_static
    version
    advapi32
    bcrypt
    winhttp
    ws2_32
    rpcrt4
    shell32
)

# Shared export for scylla.exe (C# LibraryImport). Domain + Scintilla + ConPTY helpers.
add_library(scylla-core-shared SHARED
    ${SCYLLA_CORE_SOURCES}
    src/ui/editor_host.cpp
    src/ui/language.cpp
    src/ui/theme.cpp
    src/ui/terminal_host.cpp
)
target_compile_definitions(scylla-core-shared PRIVATE SCYLLA_CORE_EXPORTS)
add_dependencies(scylla-core-shared scylla-broker)
target_include_directories(scylla-core-shared PUBLIC
    include
    third_party/argon2
    third_party/scintilla/include
    third_party/lexilla/include
)
target_link_libraries(scylla-core-shared PUBLIC
    argon2_static
    scintilla_static
    lexilla_static
    version
    advapi32
    bcrypt
    winhttp
    ws2_32
    rpcrt4
    shell32
    user32
    gdi32
    comctl32
    imm32
    ole32
    oleaut32
    uuid
    dwmapi
    uxtheme
)
set_target_properties(scylla-core-shared PROPERTIES
    OUTPUT_NAME "scylla-core"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

add_executable(scyllagpt-agent-tests tests/test_agent_activity.cpp
    src/protocol/json.cpp src/protocol/acp_client.cpp)
add_executable(scyllagpt-editor-handle-tests tests/test_editor_handle.cpp)
target_link_libraries(scyllagpt-editor-handle-tests PRIVATE user32)
add_dependencies(scyllagpt-editor-handle-tests scylla-core-shared)
set_target_properties(scyllagpt-editor-handle-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
target_include_directories(scyllagpt-agent-tests PRIVATE include)
set_target_properties(scyllagpt-agent-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_executable(scyllagpt WIN32 ${SCYLLAGPT_UI_SOURCES})
target_include_directories(scyllagpt PRIVATE
    include
    third_party/scintilla/include
    third_party/lexilla/include
    third_party/argon2
)
target_link_libraries(scyllagpt PRIVATE
    scylla-core
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
    bcrypt
    winhttp
    ws2_32
    rpcrt4
)
set_target_properties(scyllagpt PROPERTIES
    OUTPUT_NAME "scylla-workbench"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

add_executable(scyllagpt-tests
    tests/test_framing.cpp
    tests/test_keyring.cpp
    tests/test_project_environment.cpp
    tests/test_project_connection.cpp
    tests/test_connection_policy.cpp
    tests/test_connection_broker.cpp
    tests/test_ssh_query_executor.cpp
    tests/test_broker_transport.cpp
    tests/test_terminal_screen.cpp
    src/ui/terminal_host.cpp
    src/ui/terminal_session.cpp
    tests/test_terminal_profiles.cpp
    tests/test_workbench_domain.cpp
    tests/test_settings_policy.cpp
    tests/test_mcp_oauth.cpp
    src/ui/layout.cpp
    src/ui/language.cpp
    src/ui/theme.cpp
    src/ui/ui_space.cpp
)
add_executable(scylla-mcp-tests tests/test_mcp_settings.cpp tests/test_mcp_oauth.cpp)
target_link_libraries(scylla-mcp-tests PRIVATE scylla-core user32)
set_target_properties(scylla-mcp-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
target_include_directories(scyllagpt-tests PRIVATE include third_party/argon2)
target_link_libraries(scyllagpt-tests PRIVATE
    scylla-core
    comctl32
    uxtheme
    dwmapi
    version
    shell32
    advapi32
    winhttp
    ws2_32
    rpcrt4
    user32
    gdi32
)
set_target_properties(scyllagpt-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

add_executable(scylla-payload-tests tests/test_payload_log.cpp)
target_link_libraries(scylla-payload-tests PRIVATE scylla-core user32)
set_target_properties(scylla-payload-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
