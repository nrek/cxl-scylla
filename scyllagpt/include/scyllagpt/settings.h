#pragma once

#include "scyllagpt/json.h"
#include "scyllagpt/provider.h"

#include <map>
#include <string>
#include <string_view>

namespace scyllagpt {

// How a protected (Keyring-backed) value may reach a consumer.
enum class PolicyMode {
    Allow = 0,
    Ask,
    Block,
};

// Execution Policy for protected values (Settings → Security → Execution Policy).
// Strict is the shipped default: humans may use protected values, agent terminals never do,
// and approved recipes confirm each time.
struct ExecutionPolicy {
    PolicyMode human_terminals = PolicyMode::Allow;
    PolicyMode agent_terminals = PolicyMode::Block;
    PolicyMode approved_recipes = PolicyMode::Ask;
};

PolicyMode parse_policy_mode(std::string_view raw, PolicyMode fallback);
const char* policy_mode_string(PolicyMode mode);
const wchar_t* policy_mode_label(PolicyMode mode);
ExecutionPolicy strict_execution_policy();
bool execution_policy_is_strict(const ExecutionPolicy& policy);

struct WindowPlacement {
    int x = 80;
    int y = 80;
    int w = 1440;
    int h = 860;
    bool maximized = false;
};

struct Settings {
    std::wstring codex_path;
    WindowPlacement window;
    bool enter_sends = true;
    std::string last_thread_id;
    bool restore_chat_on_start = true;
    Json drafts;  // object: threadId -> draft text
    std::wstring project_folder;
    int files_w = 220;
    int knowledge_h = 0;  // DIPs; zero keeps the original automatic height until dragged.
    int agent_w = 400;
    int history_w = 232;
    int files_mode = 0;    // 0 auto, 1 on, 2 off
    int history_mode = 0;
    bool focus_editor = false;
    // "openai" | "openai-api" | "claude" | "claude-api"
    std::string default_provider = "openai";
    std::string selected_model;  // last model id for active provider
    // Per-model enable: key = "provider/model_id".
    // Account providers (openai, claude): missing key ⇒ enabled (first-run parity).
    // API providers (openai-api, claude-api): missing key ⇒ disabled (0 selected by default).
    std::map<std::string, bool> model_enabled;
    // Per-provider default model id. Empty ⇒ use catalog best among enabled.
    std::map<std::string, std::string> provider_default_model;
    bool word_wrap = false;
    bool show_whitespace = false;
    int terminal_h = 220;
    bool terminal_visible = false;
    // Human terminal is always free; agent-driven shell uses this policy.
    // "ask" | "allow" | "block" — default ask.
    std::string agent_terminal_policy = "ask";
    std::string default_terminal_profile_id;
    std::string panel_surface = "terminal";  // terminal|problems|output|ports
    // Protected-value rules surfaced by Settings → Security → Execution Policy.
    ExecutionPolicy execution_policy;
    // Profile id → enabled override (missing = leave discovered default).
    std::map<std::string, bool> terminal_profile_enabled;
    // Knowledge / Strata / MCP / custom terminals persist under Paths::*_path
    // (%LOCALAPPDATA%\ScyllaGPT\*.json) — not embedded in settings.json.
};

Settings load_settings(const std::wstring& path);
bool save_settings(const std::wstring& path, const Settings& s);

inline std::string model_enable_key(std::string_view provider, std::string_view model_id) {
    std::string k;
    k.reserve(provider.size() + model_id.size() + 1);
    k.append(provider);
    k.push_back('/');
    k.append(model_id);
    return k;
}

// Account providers: missing ⇒ enabled. API providers: missing ⇒ disabled.
inline bool settings_model_enabled(const Settings& s, std::string_view provider, std::string_view model_id) {
    const auto it = s.model_enabled.find(model_enable_key(provider, model_id));
    if (it == s.model_enabled.end()) {
        return !is_api_provider(provider);
    }
    return it->second;
}

}  // namespace scyllagpt
