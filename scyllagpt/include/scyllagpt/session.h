#pragma once

#include "scyllagpt/json.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/runtime.h"
#include "scyllagpt/settings.h"
#include "scyllagpt/store.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace scyllagpt {

enum class AppState {
    Offline,
    Connecting,
    SigningIn,
    Ready,
    Generating,
    AwaitingAction,
    Interrupted,
    Failed,
};

struct ModelChoice {
    std::string id;
    std::string display;
    std::string provider_id = "openai";  // openai | claude
};

struct ThreadSummary {
    std::string id;
    std::string name;
    std::string preview;
};

struct AccountInfo {
    bool signed_in = false;
    std::string type;  // chatgpt | apiKey | none
    std::string email;
    std::string plan;
};

class Session;

using UiFn = std::function<void()>;

// Posted to the main window when a Claude -p worker finishes (lParam = ClaudePrintResult*).
constexpr UINT WM_SCYLLA_CLAUDE_DONE = WM_APP + 43;
// Posted when a background `claude models` refresh finishes.
constexpr UINT WM_SCYLLA_CLAUDE_MODELS = WM_APP + 44;

struct ClaudePrintResult {
    bool ok = false;
    std::string text;
    std::wstring error;
};

class Session {
public:
    Paths paths;
    Settings settings;

    AppState state = AppState::Offline;
    std::wstring status_text = L"Offline";
    AccountInfo account;
    std::vector<ModelChoice> models;
    int models_epoch = 0;
    std::string models_catalog_sig_;
    std::string selected_model;
    std::vector<ThreadSummary> threads;
    std::string active_thread_id;
    std::string active_turn_id;
    std::string login_id;
    std::wstring runtime_version;
    std::wstring runtime_path;
    std::wstring isolated_home_note;
    std::string last_error;
    std::string stream_buffer;  // live assistant text or reconstructed history
    struct HistoryMessage { bool user; std::string text; };
    std::vector<HistoryMessage> history_messages;
    bool transcript_replace = false;
    bool following_tail = true;
    bool website_history_seen = false;

    WorkspaceStore store;
    std::wstring project_root;
    std::vector<ContextChip> context_chips;

    bool start_runtime(HWND hwnd, UINT line_msg, std::wstring* error);
    void stop_runtime();
    void handle_line(const std::string& line);

    void login_chatgpt();
    void logout();
    void new_conversation();
    void open_thread(const std::string& id);
    void send_user(const std::string& text);
    // Called on UI thread when a Claude -p worker finishes (ok + text or error).
    void complete_claude_print(bool ok, const std::string& text, const std::wstring& error);
    bool claude_generating() const { return claude_busy_; }
    std::wstring conversation_cwd() const;
    bool has_project_grant() const;
    std::wstring grant_label() const;
    bool sync_lockdown_config();
    std::string account_scope() const;
    void cancel_turn();
    void refresh_threads();
    void refresh_models();
    // Merge/strip curated Claude models based on auth; bump models_epoch.
    void sync_claude_models();
    // Switch active provider without losing the other catalog; picks a model for that provider.
    void set_default_provider(const std::string& provider);
    // Refresh Claude model list from CLI (background-safe to call with force on timer).
    void refresh_claude_model_catalog(bool force_cli);

    std::string draft_for(const std::string& thread_id) const;
    void set_draft(const std::string& thread_id, const std::string& text);

    bool runtime_live() const { return runtime_.running(); }

private:
    std::int64_t next_id_ = 1;
    std::int64_t initialize_id_ = 0;
    std::int64_t account_read_id_ = 0;
    std::int64_t login_id_req_ = 0;
    std::int64_t model_list_id_ = 0;
    bool models_replace_next_ = true;
    std::int64_t logout_id_ = 0;
    std::int64_t thread_list_id_ = 0;
    std::int64_t thread_start_id_ = 0;
    std::int64_t thread_resume_id_ = 0;
    std::int64_t thread_read_id_ = 0;
    std::int64_t turn_start_id_ = 0;
    std::int64_t turn_interrupt_id_ = 0;

    Runtime runtime_;
    HWND hwnd_ = nullptr;
    UINT line_msg_ = 0;
    bool runtime_allow_shell_ = false;

    std::int64_t send_req(const char* method, Json params);
    void send_notify(const char* method, Json params);
    void send_result(const Json& id, Json result);
    void set_state(AppState s, const std::wstring& text);
    void after_ready();
    void request_models(const std::string& cursor);
    void ingest_models(const Json& result, bool replace);
    void merge_claude_models();
    void clamp_models_per_provider(std::size_t max_per);
    void finalize_model_catalog();
    void ensure_selected_model();
    void ensure_claude_thread();
    void send_claude_user(const std::string& text);
    static int model_sort_rank(const std::string& id, const std::string& provider_id);
    void handle_response(const Json& msg);
    void handle_notification(const Json& msg);
    void handle_server_request(const Json& msg);
    void apply_account(const Json& account);
    void extract_history(const Json& thread);

    bool claude_busy_ = false;
    std::atomic<bool> claude_cancel_{false};
};

const wchar_t* state_label(AppState s);

}  // namespace scyllagpt
