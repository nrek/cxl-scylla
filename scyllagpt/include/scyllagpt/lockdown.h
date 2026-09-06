#pragma once

// Managed Codex lockdown for ScyllaGPT. Regenerated into
// %LOCALAPPDATA%\ScyllaGPT\codex-home\config.toml on every launch
// and when the project grant changes.
// This is policy + Job Object + isolated home — not an AppContainer.

namespace scyllagpt {

// workspace_write_grant: true when the header project dropdown has a folder
// the agent may browse/edit (workspace-write + shell_tool for read/search).
// False → read-only, no shell.
inline const char* isolated_codex_config_toml(bool workspace_write_grant = false) {
    if (workspace_write_grant) {
        return
            "# ScyllaGPT managed Codex home. Regenerated on every launch / project grant change.\n"
            "# This is not %USERPROFILE%\\.codex. Do not edit — ScyllaGPT overwrites it.\n"
            "# Signing out here only affects this home, not ChatGPT desktop / Cursor.\n"
            "# Policy + job + isolated home; not an AppContainer / Isolated User cage.\n"
            "# Project grant: workspace-write + shell inside the selected folder (sandbox).\n"
            "forced_login_method = \"chatgpt\"\n"
            "approval_policy = \"on-request\"\n"
            "sandbox_mode = \"workspace-write\"\n"
            "notify = []\n"
            "web_search = \"disabled\"\n"
            "file_opener = \"none\"\n"
            "check_for_update_on_startup = false\n"
            "project_doc_max_bytes = 0\n"
            "project_doc_fallback_filenames = []\n"
            "agents.enabled = false\n"
            "\n"
            "[analytics]\n"
            "enabled = false\n"
            "\n"
            "[features]\n"
            "apps = false\n"
            "remote_plugin = false\n"
            "hooks = false\n"
            "memories = false\n"
            "multi_agent = false\n"
            "shell_tool = true\n"
            "skill_mcp_dependency_install = false\n"
            "goals = false\n"
            "shell_snapshot = false\n"
            "\n"
            "[apps._default]\n"
            "enabled = false\n"
            "open_world_enabled = false\n"
            "destructive_enabled = false\n"
            "\n"
            "[shell_environment_policy]\n"
            "inherit = \"none\"\n"
            "experimental_use_profile = false\n"
            "\n"
            "[computer_use.windows]\n"
            "always_allowed_app_ids = []\n";
    }
    return
        "# ScyllaGPT managed Codex home. Regenerated on every launch / project grant change.\n"
        "# This is not %USERPROFILE%\\.codex. Do not edit — ScyllaGPT overwrites it.\n"
        "# Signing out here only affects this home, not ChatGPT desktop / Cursor.\n"
        "# Policy + job + isolated home; not an AppContainer / Isolated User cage.\n"
        "# No project grant: read-only, shell_tool off.\n"
        "forced_login_method = \"chatgpt\"\n"
        "approval_policy = \"on-request\"\n"
        "sandbox_mode = \"read-only\"\n"
        "notify = []\n"
        "web_search = \"disabled\"\n"
        "file_opener = \"none\"\n"
        "check_for_update_on_startup = false\n"
        "project_doc_max_bytes = 0\n"
        "project_doc_fallback_filenames = []\n"
        "agents.enabled = false\n"
        "\n"
        "[analytics]\n"
        "enabled = false\n"
        "\n"
        "[features]\n"
        "apps = false\n"
        "remote_plugin = false\n"
        "hooks = false\n"
        "memories = false\n"
        "multi_agent = false\n"
        "shell_tool = false\n"
        "skill_mcp_dependency_install = false\n"
        "goals = false\n"
        "shell_snapshot = false\n"
        "\n"
        "[apps._default]\n"
        "enabled = false\n"
        "open_world_enabled = false\n"
        "destructive_enabled = false\n"
        "\n"
        "[shell_environment_policy]\n"
        "inherit = \"none\"\n"
        "experimental_use_profile = false\n"
        "\n"
        "[computer_use.windows]\n"
        "always_allowed_app_ids = []\n";
}

// allow_shell: omit --disable shell_tool so in-grant browse/search works.
inline const wchar_t* app_server_disable_args(bool allow_shell = false) {
    if (allow_shell) {
        return L"--disable apps --disable remote_plugin --disable hooks "
               L"--disable multi_agent --disable skill_mcp_dependency_install --disable goals "
               L"--disable memories";
    }
    return L"--disable apps --disable remote_plugin --disable hooks --disable shell_tool "
           L"--disable multi_agent --disable skill_mcp_dependency_install --disable goals "
           L"--disable memories";
}

inline const char* workspace_agents_md() {
    return "# ScyllaGPT workspace\n"
           "Stay inside this directory. Do not search or read files above it.\n";
}

}  // namespace scyllagpt
