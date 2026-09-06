#pragma once

#include <string>
#include <vector>

namespace scyllagpt {

enum class ProviderId { OpenAI, Claude };

struct ProviderCapabilities {
    bool streaming = false;
    bool file_context = false;
    bool chat_send = false;  // Phase 1: only OpenAI/Codex
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

inline const char* provider_id_string(ProviderId id) {
    return id == ProviderId::Claude ? "claude" : "openai";
}

inline ProviderId provider_id_from_string(const std::string& s) {
    return s == "claude" ? ProviderId::Claude : ProviderId::OpenAI;
}

// Credential Manager — Anthropic API key only. Never logs the secret.
bool claude_api_key_save(const std::wstring& key);
bool claude_api_key_load(std::wstring* out);
bool claude_api_key_clear();
bool claude_api_key_present();

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
ProviderStatus claude_provider_status();

// True when Claude Code OAuth or CredMan API key is available.
bool claude_is_connected();

// Clear OAuth cache + sticky connected flag (Disconnect / sign-out paths).
void claude_clear_connection_state();

// One Claude Code model row (id + display name).
struct ClaudeModelRow {
    std::string id;
    std::string display;
};

// Query `claude models` (cached). Falls back to curated catalog without blocking when
// cache is cold. force=true runs the CLI. Results are clamped to the newest 3.
std::vector<ClaudeModelRow> claude_code_list_models(bool force = false);

// Curated fallback (Opus / Sonnet / Haiku) when CLI list is unavailable.
struct ClaudeModelInfo {
    const char* id;
    const char* display;
};
const ClaudeModelInfo* curated_claude_models(std::size_t* count);

// Run `claude -p` (print) with the given model. Uses Claude Code OAuth or ANTHROPIC_API_KEY
// from the environment/CredMan via the CLI — does not scrape token files.
// Blocks until complete or timeout_ms. Returns false on failure.
bool claude_code_print(const std::string& prompt_utf8, const std::string& model_id, const std::wstring& cwd,
                       std::string* result_text, std::wstring* error, unsigned timeout_ms = 300000);

// True when default provider can drive Send (OpenAI when ChatGPT signed in; Claude when connected).
bool provider_can_send(ProviderId id);

}  // namespace scyllagpt
