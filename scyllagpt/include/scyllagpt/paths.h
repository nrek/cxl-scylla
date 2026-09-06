#pragma once

#include <string>
#include <vector>

namespace scyllagpt {

struct Paths {
    std::wstring appdata;       // %LOCALAPPDATA%\ScyllaGPT
    std::wstring codex_home;    // isolated CODEX_HOME
    std::wstring workspace;     // CreateProcess cwd (isolated); thread cwd follows project grant
    std::wstring settings_path;
    std::wstring store_path;
    std::wstring recovery_dir;
    std::wstring attachments_dir;  // pasted/attached images for composer
    std::wstring stderr_log;
    std::wstring runtime_pin;   // recorded runtime path/version
};

Paths make_paths();
bool ensure_dir(const std::wstring& path);
std::wstring file_version(const std::wstring& exe);
std::wstring discover_codex_exe();
bool is_unversioned_openai_codex(const std::wstring& path);
// Overwrites config.toml every launch / grant change. Never touches auth.json.
// workspace_write_grant: project dropdown has a folder → sandbox_mode workspace-write.
bool write_isolated_codex_config(const Paths& paths, bool workspace_write_grant = false);
std::wstring join_path(const std::wstring& a, const std::wstring& b);
bool file_exists(const std::wstring& path);

}  // namespace scyllagpt
