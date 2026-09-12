#pragma once

#include "scyllagpt/agent_activity.h"

#include "scyllagpt/event_sink.h"
#include "scyllagpt/json.h"
#include "scyllagpt/mcp_manager.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/runtime.h"
#include "scyllagpt/settings.h"
#include "scyllagpt/store.h"

#include <deque>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

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
    std::string provider_id = "openai";  // openai | openai-api | claude | claude-api
    bool hidden = false;  // Hidden by the provider's default picker; still manageable in Settings.
    std::vector<std::string> reasoning_efforts;
    std::string default_reasoning_effort;
    std::string specialty;  // "language" | "code" | empty (general purpose/unspecified)
    bool is_default = false;
};

struct ThreadSummary {
    std::string id;
    std::string name;
    std::string preview;
    std::wstring cwd;
};

struct AccountInfo {
    bool signed_in = false;
    std::string type;  // chatgpt | apiKey | none
    std::string email;
    std::string plan;
};

class Session;

using UiFn = std::function<void()>;

class Session {
public:
    void log_payload(const std::string& direction, const std::string& payload);
    std::deque<std::string> payload_log;
    Paths paths;
    Settings settings;

    AppState state = AppState::Offline;
    AgentActivity activity;
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

    // on_line is invoked from the runtime reader thread; shells must marshal to UI.
    // on_claude_done is invoked from worker threads with a by-value result.
    bool start_runtime(LineSink on_line, ClaudeDoneSink on_claude_done, std::wstring* error);
    void stop_runtime();
    void handle_line(const std::string& line);

    void login_chatgpt();
    void logout();
    void new_conversation();
    void open_thread(const std::string& id);
    bool delete_thread(const std::string& id, std::string* error);
    std::string chat_title_seed;
    int chats_epoch = 0;
    void send_user(const std::string& text, const std::string& mode = "execute");
    std::string last_plan_path;
    bool account_loaded = false;
    // Called on UI thread when a local print/API worker finishes (ok + text or error).
    void complete_claude_print(bool ok, const std::string& text, const std::wstring& error);
    bool claude_generating() const { return claude_busy_; }
    std::wstring conversation_cwd() const;
    bool has_project_grant() const;
    std::wstring grant_label() const;
    bool sync_lockdown_config();
    // Knowledge source roots the agent may browse (enabled + agent_available).
    void set_knowledge_accessible_paths(std::vector<std::wstring> paths);
    void set_mcp_manager(const McpManager* manager) { mcp_manager_ = manager; }
    // Registers Scylla's own query-broker helper as an MCP stdio server for the agent runtime. An
    // empty `name` disables it, which is what keeps the tool absent until the broker is running.
    void set_broker_mcp_server(CodexMcpServer server) { broker_mcp_server_ = std::move(server); }
    bool broker_mcp_server_registered() const { return !broker_mcp_server_.name.empty(); }
    const std::vector<std::wstring>& knowledge_accessible_paths() const {
        return knowledge_accessible_paths_;
    }
    // Absolute-path preamble so the model looks under Knowledge roots, not only cwd.
    std::string knowledge_grant_preamble() const;
    std::string account_scope() const;
    void cancel_turn();
    void refresh_threads(const std::string& cursor = {});
    void refresh_models();
    // Merge/strip account + API catalogs based on auth; bump models_epoch.
    void sync_claude_models();
    // Switch active provider without losing the other catalog; picks a model for that provider.
    void set_default_provider(const std::string& provider);
    // Refresh Claude Code curated catalog (account).
    void refresh_claude_model_catalog(bool force_cli);
    // Refresh OpenAI/Claude API model lists when keys present.
    void refresh_openai_api_models();
    void refresh_claude_api_models();

    bool is_model_enabled(const std::string& provider_id, const std::string& model_id) const;
    void set_model_enabled(const std::string& provider_id, const std::string& model_id, bool enabled);
    std::vector<ModelChoice> models_for_provider(const std::string& provider_id, bool enabled_only) const;
    std::string provider_default_model_id(const std::string& provider_id) const;
    void set_provider_default_model(const std::string& provider_id, const std::string& model_id);
    void reset_reasoning_effort_for_selected_model();
    // Adds the selected effort to a Codex turn only when the active model advertises it.
    void apply_reasoning_effort(Json& turn_params) const;

    std::string draft_for(const std::string& thread_id) const;
    void set_draft(const std::string& thread_id, const std::string& text);

    bool runtime_live() const { return runtime_.running(); }
    bool thread_busy(const std::string& thread_id) const;
    const AgentActivity* thread_activity(const std::string& thread_id) const;
    bool active_thread_busy() const { return thread_busy(active_thread_id); }
    // Drop stuck busy flags when the shell is idle (no live turn / resume).
    void reconcile_idle_activity();
    bool workspace_change_pending() const {
        if (claude_busy_ || !pending_resumed_turns_.empty()) return true;
        // Idle New Chat precreates via thread/start while UI is Ready — only block when a send is queued on it.
        if (thread_start_id_ >= 0 &&
            pending_thread_prompts_.find(thread_start_id_) != pending_thread_prompts_.end()) {
            return true;
        }
        for (const auto& entry : thread_runtime_) {
            if (entry.second.activity.busy) return true;
        }
        if (activity.busy &&
            (state == AppState::Generating || state == AppState::AwaitingAction)) {
            return true;
        }
        return false;
    }

private:
    std::map<std::int64_t, std::string> pending_thread_modes_;
    std::map<std::string, std::string> thread_modes_;
    std::map<std::string, std::string> final_plan_text_;
    void save_knowledge_plan(const std::string& thread_id, const std::string& text);
    std::int64_t next_id_ = 1;
    std::int64_t initialize_id_ = 0;
    std::int64_t account_read_id_ = 0;
    std::int64_t login_id_req_ = 0;
    std::int64_t model_list_id_ = 0;
    bool models_replace_next_ = true;
    std::int64_t logout_id_ = 0;
    std::int64_t thread_list_id_ = 0;
    std::int64_t thread_start_id_ = -1;
    std::int64_t thread_resume_id_ = 0;
    std::int64_t thread_read_id_ = 0;
    std::int64_t turn_start_id_ = 0;
    std::int64_t turn_interrupt_id_ = 0;

    Runtime runtime_;
    LineSink on_line_;
    ClaudeDoneSink on_claude_done_;
    bool runtime_allow_shell_ = false;

    std::int64_t send_req(const char* method, Json params);
    void send_notify(const char* method, Json params);
    void send_result(const Json& id, Json result);
    void set_state(AppState s, const std::wstring& text);
    void after_ready();
    void request_models(const std::string& cursor);
    void ingest_models(const Json& result, bool replace);
    void merge_claude_models();
    void merge_http_provider_models(const std::string& provider_id,
                                    const std::vector<std::pair<std::string, std::string>>& rows);
    void clamp_models_per_provider(std::size_t max_per);
    void finalize_model_catalog();
    void ensure_selected_model();
    void ensure_claude_thread();
    void ensure_api_thread(const std::string& provider_id, const char* backend);
    void send_claude_user(const std::string& text);
    void send_api_user(const std::string& provider_id, const std::string& text);
    static int model_sort_rank(const std::string& id, const std::string& provider_id);
    void handle_response(const Json& msg);
    void handle_notification(const Json& msg);
    void handle_server_request(const Json& msg);
    void apply_account(const Json& account);
    void extract_history(const Json& thread, const std::string& thread_id);
    void select_thread_runtime(const std::string& thread_id);
    // Interrupt a Codex turn; used by Cancel and to clear orphaned inProgress turns after relaunch.
    void interrupt_thread_turn(const std::string& thread_id, const std::string& turn_id);
    // When thread/read shows inProgress but this process has no busy activity, the prior session
    // died mid-turn — interrupt so turn/start can succeed again.
    void clear_orphaned_in_progress_turn(const std::string& thread_id, const Json& thread);

    struct ThreadRuntime {
        AgentActivity activity;
        std::string stream;
        std::string turn_id;
    };
    enum class SendRepairStep {
        InitialResume = 0,
        AfterListLookup = 1,
        AfterPathResume = 2,
        FreshThread = 3,  // post thread/start — never start another chat on failure
    };
    struct PendingResumedPrompt {
        std::string text;
        std::string mode;
        std::string thread_id;
        bool foreground = true;
        SendRepairStep repair_step = SendRepairStep::InitialResume;
        bool recorded_local = false;
    };
    void advance_send_repair(PendingResumedPrompt prompt);
    void begin_fresh_send(PendingResumedPrompt prompt, const wchar_t* status);
    void fail_send_repair(PendingResumedPrompt prompt, const std::string& error);
    void send_user_to_thread(const std::string& text, const std::string& thread_id, bool foreground,
                             const std::string& mode, const std::string& resume_path, bool record_user,
                             SendRepairStep repair_step, bool skip_resume = false);
    std::unordered_map<std::string, ThreadRuntime> thread_runtime_;
    std::unordered_map<std::int64_t, std::string> turn_start_threads_;
    std::unordered_map<std::int64_t, Json> pending_resumed_turns_;
    std::unordered_map<std::int64_t, PendingResumedPrompt> pending_resumed_prompts_;
    std::unordered_map<std::int64_t, PendingResumedPrompt> pending_repair_prompts_;
    std::unordered_map<std::int64_t, std::string> pending_thread_prompts_;
    std::unordered_map<std::int64_t, std::string> thread_read_threads_;
    std::unordered_map<std::string, std::string> turn_threads_;
    std::int64_t repair_list_id_ = 0;
    bool send_repair_fresh_used_ = false;

    bool claude_busy_ = false;
    std::string print_thread_id_;
    std::atomic<bool> claude_cancel_{false};

    std::vector<std::wstring> knowledge_accessible_paths_;
    const McpManager* mcp_manager_ = nullptr;
    CodexMcpServer broker_mcp_server_;
};

const wchar_t* state_label(AppState s);

}  // namespace scyllagpt
