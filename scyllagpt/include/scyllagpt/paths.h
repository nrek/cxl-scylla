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
    std::wstring knowledge_path;      // knowledge roots JSON (Phase 2)
    std::wstring strata_path;         // Strata client settings + bindings (Phase 3)
    std::wstring mcp_path;            // MCP connections JSON (Phase 4)
    std::wstring terminals_path;      // custom terminal profiles JSON
    std::wstring environments_path;   // project environments JSON (App Keyring Phase 2)
    std::wstring recovery_dir;
    std::wstring attachments_dir;  // pasted/attached images for composer
    std::wstring stderr_log;
    std::wstring runtime_pin;   // recorded runtime path/version
};

struct CodexMcpServer {
    std::string name;
    std::wstring url;
    std::wstring bearer_token_env_var;
    bool stdio = false;
    std::wstring command;
    std::vector<std::wstring> arguments;
    std::vector<std::pair<std::wstring, std::wstring>> environment;
};

Paths make_paths();
bool ensure_dir(const std::wstring& path);
std::wstring file_version(const std::wstring& exe);
std::wstring discover_codex_exe();
bool is_unversioned_openai_codex(const std::wstring& path);
// Overwrites config.toml every launch / grant change. Never touches auth.json.
// workspace_write_grant: project dropdown has a folder → sandbox_mode workspace-write.
// extra_writable_roots: Knowledge (and similar) absolute paths listed under
// [sandbox_workspace_write] writable_roots so the agent can browse outside cwd.
bool write_isolated_codex_config(const Paths& paths, bool workspace_write_grant = false,
                                 const std::vector<std::wstring>& extra_writable_roots = {},
                                 const std::vector<CodexMcpServer>& mcp_servers = {});
std::string render_isolated_codex_config(bool workspace_write_grant,
                                         const std::vector<std::wstring>& extra_writable_roots,
                                         const std::vector<CodexMcpServer>& mcp_servers);
std::wstring join_path(const std::wstring& a, const std::wstring& b);
bool file_exists(const std::wstring& path);

}  // namespace scyllagpt
