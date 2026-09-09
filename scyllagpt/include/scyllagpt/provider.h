#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

// Four first-class providers (token buckets are separate for account vs API).
enum class ProviderId { OpenAI, OpenAiApi, Claude, ClaudeApi };

struct ProviderCapabilities {
    bool streaming = false;
    bool file_context = false;
    bool chat_send = false;
};

struct ProviderStatus {
    ProviderId id = ProviderId::OpenAI;
    std::string display_name;
    bool connected = false;
    std::string auth_label;   // e.g. "ChatGPT", "API key", "Claude Code", "Not connected"
    std::string detail;       // email or masked key hint
    ProviderCapabilities caps;
};

// Claude Code CLI session (OAuth) — discovered via `claude auth status`, not by reading tokens.
struct ClaudeCodeSession {
    bool logged_in = false;
    std::string email;
    std::string org_name;
    std::string auth_method;      // e.g. claude.ai
    std::string subscription;     // e.g. team / pro
};

inline bool is_api_provider(std::string_view provider) {
    return provider == "openai-api" || provider == "claude-api";
}

inline bool is_known_provider(std::string_view provider) {
    return provider == "openai" || provider == "openai-api" || provider == "claude" ||
           provider == "claude-api";
}

inline const char* provider_id_string(ProviderId id) {
    switch (id) {
        case ProviderId::OpenAiApi:
            return "openai-api";
        case ProviderId::Claude:
            return "claude";
        case ProviderId::ClaudeApi:
            return "claude-api";
        case ProviderId::OpenAI:
        default:
            return "openai";
    }
}

inline ProviderId provider_id_from_string(const std::string& s) {
    if (s == "openai-api") {
        return ProviderId::OpenAiApi;
    }
    if (s == "claude") {
        return ProviderId::Claude;
    }
    if (s == "claude-api") {
        return ProviderId::ClaudeApi;
    }
    return ProviderId::OpenAI;
}

inline const char* provider_display_name(std::string_view provider) {
    if (provider == "openai-api") {
        return "OpenAI API";
    }
    if (provider == "claude") {
        return "Claude Account";
    }
    if (provider == "claude-api") {
        return "Claude API";
    }
    return "OpenAI ChatGPT";
}

// Normalize persisted default_provider; unknown → openai.
inline std::string coerce_default_provider(std::string_view raw) {
    if (raw == "openai-api" || raw == "claude" || raw == "claude-api" || raw == "openai") {
        return std::string(raw);
    }
    return "openai";
}

// Credential Manager — Anthropic API key only. Never logs the secret.
bool claude_api_key_save(const std::wstring& key);
bool claude_api_key_load(std::wstring* out);
bool claude_api_key_clear();
bool claude_api_key_present();

// Credential Manager — OpenAI API key (BYOK). Never logs the secret.
bool openai_api_key_save(const std::wstring& key);
bool openai_api_key_load(std::wstring* out);
bool openai_api_key_clear();
bool openai_api_key_present();

// Discover claude.exe / claude.cmd on PATH (Claude Code CLI).
std::wstring discover_claude_cli();

// Launch `claude auth login`. Returns false if CLI missing.
bool claude_code_login_launch(std::wstring* error);

// Query `claude auth status` (JSON). force=true bypasses short TTL cache.
ClaudeCodeSession claude_code_session_status(bool force = false);

// Run `claude auth logout`. Does not touch CredMan API keys.
bool claude_code_logout(std::wstring* error);

ProviderStatus openai_provider_status(bool signed_in, const std::string& email, const std::string& plan,
                                      const std::string& type);
ProviderStatus openai_api_provider_status();
ProviderStatus claude_provider_status();       // Claude Code account (OAuth) only
ProviderStatus claude_api_provider_status();   // CredMan Anthropic key only

// True when Claude Code OAuth session is available (not API key).
bool claude_account_connected();
// True when Anthropic API key is in CredMan.
bool claude_api_connected();
// Alias for account (OAuth) — API key alone does not count.
bool claude_is_connected();

// Clear OAuth cache + sticky connected flag (Disconnect / sign-out paths).
void claude_clear_connection_state();

// One Claude Code model row (id + display name).
struct ClaudeModelRow {
    std::string id;
    std::string display;
};

// Query curated Claude Code aliases (CLI has no models subcommand).
std::vector<ClaudeModelRow> claude_code_list_models(bool force = false);

// Curated fallback (Opus / Sonnet / Haiku) when CLI list is unavailable.
struct ClaudeModelInfo {
    const char* id;
    const char* display;
};
const ClaudeModelInfo* curated_claude_models(std::size_t* count);

// Run `claude -p` (print) with the given model. Uses Claude Code OAuth session.
// Blocks until complete or timeout_ms. Returns false on failure.
bool claude_code_print(const std::string& prompt_utf8, const std::string& model_id, const std::wstring& cwd,
                       std::string* result_text, std::wstring* error, unsigned timeout_ms = 300000);

// True when default provider can drive Send (credentials present for that bucket).
bool provider_can_send(ProviderId id);

}  // namespace scyllagpt
