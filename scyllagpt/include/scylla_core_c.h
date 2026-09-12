#pragma once

// Flat C ABI for scylla-core. Consumed by scylla.exe (C# P/Invoke) and by
// scylla.exe consumes this ABI from scylla-core.dll. Keep C89-compatible.

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SCYLLA_CORE_EXPORTS
#define SCYLLA_CORE_API __declspec(dllexport)
#else
#define SCYLLA_CORE_API __declspec(dllimport)
#endif

#ifdef SCYLLA_CORE_STATIC
#undef SCYLLA_CORE_API
#define SCYLLA_CORE_API
#endif

typedef struct scylla_session scylla_session;
typedef struct scylla_terminal scylla_terminal;
SCYLLA_CORE_API int scylla_session_send_user_mode(scylla_session* session, const char* text_utf8, const char* mode, char* err_utf8, int err_len);

SCYLLA_CORE_API const char* scylla_core_version(void);

SCYLLA_CORE_API int scylla_core_try_acquire_instance(void);
SCYLLA_CORE_API void scylla_core_release_instance(void);
SCYLLA_CORE_API int scylla_core_activate_existing(void);

// UTF-8 %LOCALAPPDATA%\ScyllaGPT. Returns bytes written excluding NUL, or -1.
SCYLLA_CORE_API int scylla_core_data_dir_utf8(char* buf, int buf_len);

// Settings snapshot JSON. Returns bytes written or -1.
SCYLLA_CORE_API int scylla_core_settings_json(char* buf, int buf_len);

// Merge known keys from JSON into settings.json and save. Returns 1 on ok.
SCYLLA_CORE_API int scylla_core_patch_settings(const char* json_utf8, char* err_utf8, int err_len);

// Set project folder (UTF-8 path). Persists settings + opens/creates workspace store. Returns 1 on ok.
SCYLLA_CORE_API int scylla_core_set_project_folder(const char* path_utf8, char* err_utf8, int err_len);

// Project roots for Files tree / Access. JSON:
// {project_id, primary, roots:[{path,name}]}
SCYLLA_CORE_API int scylla_core_project_roots_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_pick_folders(void* owner, char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_add_project_root(const char* path_utf8, char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_core_remove_project_root(const char* path_utf8, char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_core_access_summary_json(char* buf, int buf_len);

// Knowledge sources CRUD (knowledge.json).
SCYLLA_CORE_API int scylla_core_knowledge_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_knowledge_add(const char* label_utf8, const char* path_utf8,
                                               char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_core_knowledge_remove(const char* id_utf8, char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_core_knowledge_set_flags(const char* id_utf8, int enabled, int agent_available,
                                                     char* err_utf8, int err_len);

// Markdown → structured runs JSON (no WebView).
SCYLLA_CORE_API int scylla_core_markdown_runs_json(const char* text_utf8, char* buf, int buf_len);

// MCP connections (mcp.json). action: add|update|remove|set_enabled|disconnect
SCYLLA_CORE_API int scylla_core_mcp_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_mcp_catalog_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_mcp_scopes_json(const char* endpoint, char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_file_mentions(const char* query, char* buf, int buf_len);
// Human settings only. Snapshot contains metadata; no secret read/export operation exists.
SCYLLA_CORE_API int scylla_core_security_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_security_action(const char* action, const char* metadata,
    const char* value, char* error, int error_len);
SCYLLA_CORE_API int scylla_core_mcp_action(const char* action, const char* json_utf8, char* err_utf8,
                                            int err_len);

// Strata client settings + bridge ops. action: save|test|search|status
SCYLLA_CORE_API int scylla_core_strata_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_strata_action(const char* action, const char* json_utf8, char* buf,
                                               int buf_len, char* err_utf8, int err_len);

// Terminal profile prefs (discovered + custom).
SCYLLA_CORE_API int scylla_core_terminal_profiles_json(char* buf, int buf_len);
SCYLLA_CORE_API int scylla_core_terminal_profiles_save(const char* json_utf8, char* err_utf8, int err_len);

// List directory children as JSON array [{name,path,is_dir}]. Returns bytes or -1.
SCYLLA_CORE_API int scylla_core_list_dir(const char* path_utf8, char* buf, int buf_len);

// Read text file as UTF-8 (LF normalized in payload). Returns bytes or -1.
SCYLLA_CORE_API int scylla_core_read_text_file(const char* path_utf8, char* buf, int buf_len);

// Write UTF-8 text file (LF→CRLF if previous was CRLF when known). Returns 1 on ok.
SCYLLA_CORE_API int scylla_core_write_text_file(const char* path_utf8, const char* text_utf8,
                                                 char* err_utf8, int err_len);

// Session lifecycle.
SCYLLA_CORE_API scylla_session* scylla_session_create(void);
// Replace the open workspace with a JSON array of absolute folder paths (primary first).
SCYLLA_CORE_API int scylla_session_set_project_roots(scylla_session* session, const char* roots_json,
                                                    char* err_utf8, int err_len);
SCYLLA_CORE_API void scylla_session_destroy(scylla_session* session);
SCYLLA_CORE_API int scylla_session_start(scylla_session* session, char* err_utf8, int err_len);
SCYLLA_CORE_API void scylla_session_stop(scylla_session* session);
SCYLLA_CORE_API int scylla_session_send_user(scylla_session* session, const char* text_utf8,
                                              char* err_utf8, int err_len);
SCYLLA_CORE_API void scylla_session_cancel(scylla_session* session);
SCYLLA_CORE_API int scylla_session_new_chat(scylla_session* session, char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_session_delete_thread(scylla_session* session, const char* thread_id_utf8, char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_session_open_thread(scylla_session* session, const char* thread_id_utf8,
                                                char* err_utf8, int err_len);
SCYLLA_CORE_API int scylla_session_poll(scylla_session* session, char* buf, int buf_len);

// Provider auth status for Settings → Providers. JSON array of
// {id, display_name, connected, auth_label, detail, product, claude_cli_present}.
// Returns bytes written or -1.
SCYLLA_CORE_API int scylla_session_providers_json(scylla_session* session, char* buf, int buf_len);

// Provider auth action. provider_id: openai | openai-api | claude | claude-api.
// action: sign_in | sign_out | connect_key | disconnect | login_cli.
// secret_utf8: required for connect_key (API key); ignored otherwise (may be null).
// Returns 1 on ok. Domain stays in core — shells must not fork credential policy.
SCYLLA_CORE_API int scylla_session_provider_action(scylla_session* session, const char* provider_id,
                                                     const char* action, const char* secret_utf8,
                                                     char* err_utf8, int err_len);

// Scintilla editor HWND helpers (parent is a Win32 HWND; typically XAML island or window).
SCYLLA_CORE_API void* scylla_editor_create(void* parent_hwnd, int control_id);
SCYLLA_CORE_API void scylla_editor_destroy(void* editor_hwnd);
SCYLLA_CORE_API void scylla_editor_move(void* editor_hwnd, int x, int y, int w, int h);
SCYLLA_CORE_API void scylla_editor_set_text_utf8(void* editor_hwnd, const char* text_utf8);
SCYLLA_CORE_API int scylla_editor_get_text_utf8(void* editor_hwnd, char* buf, int buf_len);
SCYLLA_CORE_API int scylla_editor_load_path(void* editor_hwnd, const char* path_utf8, char* err_utf8,
                                            int err_len);
SCYLLA_CORE_API int scylla_editor_save_path(void* editor_hwnd, const char* path_utf8, char* err_utf8,
                                            int err_len);
SCYLLA_CORE_API void scylla_editor_apply_chrome(void* editor_hwnd, int dpi);
SCYLLA_CORE_API void scylla_editor_set_word_wrap(void* editor_hwnd, int wrap);
SCYLLA_CORE_API void scylla_editor_set_whitespace(void* editor_hwnd, int show);
SCYLLA_CORE_API void* scylla_editor_create_minimap(void* parent_hwnd, void* editor_hwnd, int control_id,
                                                    int dpi);
SCYLLA_CORE_API void scylla_editor_sync_minimap(void* editor_hwnd, void* minimap_hwnd);

// ConPTY terminal host (Phase C).
SCYLLA_CORE_API scylla_terminal* scylla_terminal_create(void* parent_hwnd, void* notify_hwnd,
                                                         int control_id, const char* cwd_utf8,
                                                         char* err_utf8, int err_len);
// Empty profile id resolves the configured default; explicit ids must be enabled.
SCYLLA_CORE_API scylla_terminal* scylla_terminal_create_profile(void* parent_hwnd, void* notify_hwnd,
    int control_id, const char* cwd_utf8, const char* profile_id_utf8, char* err_utf8, int err_len);
SCYLLA_CORE_API void scylla_terminal_destroy(scylla_terminal* term);
SCYLLA_CORE_API void scylla_terminal_move(scylla_terminal* term, int x, int y, int w, int h);
SCYLLA_CORE_API void scylla_terminal_set_visible(scylla_terminal* term, int visible);
SCYLLA_CORE_API int scylla_terminal_write_utf8(scylla_terminal* term, const char* text_utf8);
SCYLLA_CORE_API int scylla_terminal_poll(scylla_terminal* term, char* buf, int buf_len);
SCYLLA_CORE_API int scylla_terminal_resize_pixels(scylla_terminal* term, int w, int h);
SCYLLA_CORE_API int scylla_terminal_running(scylla_terminal* term);
SCYLLA_CORE_API void scylla_terminal_set_mouse_behavior(scylla_terminal* term, int mode);
SCYLLA_CORE_API int scylla_terminal_take_new_request(scylla_terminal* term);

#ifdef __cplusplus
}
#endif
