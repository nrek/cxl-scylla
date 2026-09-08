#include "scyllagpt/session.h"
#include "scyllagpt/chat_history.h"

#include "scyllagpt/lockdown.h"
#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <thread>

namespace scyllagpt {
namespace {

std::string item_text(const Json& item) {
    if (item.has("text") && item.at("text").is_string()) {
        return item.at("text").as_string();
    }
    const Json& content = item.at("content");
    if (content.is_array()) {
        std::string acc;
        for (const auto& part : content.array_items()) {
            if (part.at("type").as_string() == "text" || part.has("text")) {
                if (!acc.empty()) {
                    acc += "\n";
                }
                acc += part.at("text").as_string();
            }
        }
        return acc;
    }
    return {};
}

std::string thread_label(const Json& t) {
    std::string name = t.at("name").as_string("");
    if (name.empty() || name == "null") {
        name = t.at("preview").as_string("");
    }
    if (name.empty()) {
        name = t.at("id").as_string("thread");
    }
    if (name.size() > 80) {
        name = name.substr(0, 77) + "...";
    }
    return name;
}

std::string normalized_item_type(std::string type) {
    std::string normalized;
    for (const unsigned char ch : type) {
        if (std::isalnum(ch)) normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    return normalized;
}

void save_local_history(WorkspaceStore& store, const std::wstring& store_path,
                        const std::string& thread_id,
                        const std::vector<Session::HistoryMessage>& messages) {
    auto* chat = store.by_thread(thread_id);
    if (!chat) return;
    chat->local_messages = Json::array();
    for (const auto& message : messages) {
        Json item = Json::object();
        item["user"] = Json::boolean(message.user);
        item["text"] = Json::string(message.text);
        chat->local_messages.push(std::move(item));
    }
    store.save(store_path);
}

struct McpRuntimeConfig {
    std::vector<CodexMcpServer> servers;
    std::vector<std::pair<std::wstring, std::wstring>> environment;
};

McpRuntimeConfig collect_mcp_runtime(const McpManager* manager, const std::string& project_id) {
    McpRuntimeConfig result;
    if (!manager) return result;
    for (const auto* connection : manager->list_for_project(project_id)) {
        if (!connection || !connection->enabled || connection->disconnected || connection->endpoint_or_cmd.empty()) continue;
        std::string name = connection->agent_alias.empty()
            ? connection->service_id + "-" + connection->id.substr(0, 8)
            : connection->agent_alias;
        for (auto& ch : name) if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) ch = '-';
        if (connection->transport_kind == McpTransportKind::Stdio) {
            CodexMcpServer server;
            server.name = std::move(name);
            server.stdio = true;
            server.command = connection->endpoint_or_cmd;
            server.arguments = connection->arguments;
            server.environment = connection->environment;
            result.servers.push_back(std::move(server));
            continue;
        }
        McpOAuthTokens tokens;
        if (!mcp_oauth_cred_load(connection->id, &tokens) || tokens.access_token.empty()) continue;
        std::wstring env = L"SCYLLA_MCP_TOKEN_";
        for (const unsigned char ch : connection->id) env.push_back(std::isalnum(ch) ? std::towupper(ch) : L'_');
        result.servers.push_back({name, connection->endpoint_or_cmd, env, false, {}, {}, {}});
        result.environment.emplace_back(std::move(env), utf16(tokens.access_token));
    }
    return result;
}

}  // namespace

const wchar_t* state_label(AppState s) {
    switch (s) {
        case AppState::Offline:
            return L"Offline";
        case AppState::Connecting:
            return L"Connecting";
        case AppState::SigningIn:
            return L"Signing in";
        case AppState::Ready:
            return L"Ready";
        case AppState::Generating:
            return L"Generating";
        case AppState::AwaitingAction:
            return L"Awaiting action";
        case AppState::Interrupted:
            return L"Interrupted";
        case AppState::Failed:
            return L"Failed";
    }
    return L"Unknown";
}

std::int64_t Session::send_req(const char* method, Json params) {
    const std::int64_t id = next_id_++;
    Json msg = Json::object();
    msg["method"] = Json::string(method);
    msg["id"] = Json::number(id);
    if (!params.is_null()) {
        msg["params"] = std::move(params);
    }
    if (!runtime_.write_line(msg.dump())) {
        last_error = "Runtime connection closed";
        set_state(AppState::Failed, L"Runtime connection closed");
        return -1;
    }
    return id;
}

void Session::send_notify(const char* method, Json params) {
    Json msg = Json::object();
    msg["method"] = Json::string(method);
    msg["params"] = params.is_null() ? Json::object() : std::move(params);
    runtime_.write_line(msg.dump());
}

void Session::send_result(const Json& id, Json result) {
    Json msg = Json::object();
    msg["id"] = id;
    msg["result"] = std::move(result);
    runtime_.write_line(msg.dump());
}

void Session::set_state(AppState s, const std::wstring& text) {
    state = s;
    status_text = text;
    if (activity.busy) {
        if (s == AppState::Ready) activity.finish("Completed");
        else if (s == AppState::Failed) activity.finish("Failed");
        else if (s == AppState::Offline) activity.finish("Disconnected");
        else if (s == AppState::AwaitingAction) activity.phase = "Waiting for approval";
        else if (s == AppState::Generating) activity.phase = "Working";
    }
}

bool Session::start_runtime(HWND hwnd, UINT line_msg, std::wstring* error) {
    hwnd_ = hwnd;
    line_msg_ = line_msg;
    paths = make_paths();
    store.load(paths.store_path);
    if (auto* p = store.active()) {
        project_root = p->root;
        settings.project_folder = p->root;
        settings.files_w = p->files_w;
        settings.agent_w = p->agent_w;
        settings.history_w = p->history_w;
    }
    const bool allow_shell = has_project_grant();
    const auto mcp_runtime = collect_mcp_runtime(mcp_manager_, store.active_project_id);
    if (!write_isolated_codex_config(paths, allow_shell, knowledge_accessible_paths_, mcp_runtime.servers)) {
        if (error) {
            *error = L"Failed to write isolated Codex lockdown config";
        }
        set_state(AppState::Failed, L"Lockdown config write failed");
        return false;
    }
    isolated_home_note = L"CODEX_HOME is isolated: " + paths.codex_home +
                         L"  CreateProcess cwd=" + paths.workspace +
                         L"  (sign-out here does not log out ChatGPT desktop / Cursor)";
    if (allow_shell) {
        isolated_home_note += L"  Grant browse/edit: " + conversation_cwd();
        if (!knowledge_accessible_paths_.empty()) {
            isolated_home_note +=
                L" + " + std::to_wstring(knowledge_accessible_paths_.size()) + L" knowledge root(s)";
        }
    } else {
        isolated_home_note += L"  No project grant (agent read-only)";
    }

    const std::wstring discovered = discover_codex_exe();
    if (!discovered.empty()) {
        if (settings.codex_path.empty() || !file_exists(settings.codex_path) ||
            is_unversioned_openai_codex(settings.codex_path)) {
            settings.codex_path = discovered;
        }
    }
    if (settings.codex_path.empty()) {
        if (error) {
            *error = L"Codex CLI not found. Install ChatGPT desktop or Codex CLI, or pick codex.exe in Settings.";
        }
        set_state(AppState::Failed, L"Codex CLI missing");
        return false;
    }
    runtime_path = settings.codex_path;
    runtime_version = file_version(settings.codex_path);
    set_state(AppState::Connecting, L"Starting Codex app-server");
    if (!runtime_.start(settings.codex_path, paths.codex_home, paths.workspace, paths.stderr_log, hwnd, line_msg,
                        allow_shell, mcp_runtime.environment, error)) {
        set_state(AppState::Failed, error ? *error : L"Runtime start failed");
        return false;
    }
    runtime_allow_shell_ = allow_shell;

    Json client = Json::object();
    client["name"] = Json::string("scylla_gpt");
    client["title"] = Json::string("Scylla");
    client["version"] = Json::string("0.1.0");
    Json params = Json::object();
    params["clientInfo"] = std::move(client);
    initialize_id_ = send_req("initialize", std::move(params));
    return true;
}

void Session::stop_runtime() {
    runtime_.stop();
    runtime_allow_shell_ = false;
    set_state(AppState::Offline, L"Offline");
}

void Session::after_ready() {
    account_read_id_ = send_req("account/read", Json::object());
    refresh_models();
    refresh_threads();
}

void Session::refresh_models() {
    request_models({});
}

void Session::sync_claude_models() {
    // Only authenticated providers contribute agent options.
    if (!account.signed_in) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id != "claude"; }),
                     models.end());
    }
    merge_claude_models();
    finalize_model_catalog();
}

void Session::refresh_claude_model_catalog(bool force_cli) {
    (void)force_cli;
    if (!claude_is_connected()) {
        return;
    }
    // Refresh curated Claude Code aliases into the agent catalog (CLI has no `models` cmd).
    claude_code_list_models(true);
    merge_claude_models();
    finalize_model_catalog();
}

void Session::set_default_provider(const std::string& provider) {
    settings.default_provider = (provider == "claude") ? "claude" : "openai";
    ensure_selected_model();
}

void Session::request_models(const std::string& cursor) {
    Json ml = Json::object();
    ml["limit"] = Json::number(100);
    ml["includeHidden"] = Json::boolean(true);
    if (!cursor.empty()) {
        ml["cursor"] = Json::string(cursor);
    } else {
        models_replace_next_ = true;
    }
    model_list_id_ = send_req("model/list", std::move(ml));
}

int Session::model_sort_rank(const std::string& id, const std::string& provider_id) {
    // Provider blocks: OpenAI first, then Claude. Within OpenAI, prefer newest family.
    if (provider_id == "claude") {
        if (id.find("opus") != std::string::npos) {
            return 100;
        }
        if (id.find("sonnet") != std::string::npos) {
            return 101;
        }
        return 110;
    }
    if (id.rfind("gpt-6", 0) == 0) {
        return 0;
    }
    if (id.rfind("gpt-5.6", 0) == 0) {
        return 1;
    }
    if (id.rfind("gpt-5.5", 0) == 0) {
        return 2;
    }
    if (id.rfind("gpt-5", 0) == 0) {
        return 3;
    }
    if (id.rfind("o3", 0) == 0 || id.rfind("o4", 0) == 0) {
        return 4;
    }
    return 10;
}

void Session::merge_claude_models() {
    models.erase(std::remove_if(models.begin(), models.end(),
                                [](const ModelChoice& m) { return m.provider_id == "claude"; }),
                 models.end());
    if (!claude_is_connected()) {
        return;
    }
    // Live `claude models` when cached; otherwise curated (never blocks UI).
    const auto rows = claude_code_list_models(false);
    for (std::size_t i = 0; i < rows.size() && i < 3; ++i) {
        ModelChoice c;
        c.id = rows[i].id;
        c.display = rows[i].display;
        c.provider_id = "claude";
        models.push_back(std::move(c));
    }
}

void Session::clamp_models_per_provider(std::size_t max_per) {
    std::vector<ModelChoice> openai;
    std::vector<ModelChoice> claude;
    openai.reserve(models.size());
    claude.reserve(models.size());
    for (const auto& m : models) {
        if (m.provider_id == "claude") {
            claude.push_back(m);
        } else {
            openai.push_back(m);
        }
    }
    if (openai.size() > max_per) {
        openai.resize(max_per);
    }
    if (claude.size() > max_per) {
        claude.resize(max_per);
    }
    models = std::move(openai);
    models.insert(models.end(), claude.begin(), claude.end());
}

void Session::finalize_model_catalog() {
    std::stable_sort(models.begin(), models.end(), [](const ModelChoice& a, const ModelChoice& b) {
        const int pa = a.provider_id == "claude" ? 1 : 0;
        const int pb = b.provider_id == "claude" ? 1 : 0;
        if (pa != pb) {
            return pa < pb;  // OpenAI block, then Claude Code
        }
        const int ra = model_sort_rank(a.id, a.provider_id);
        const int rb = model_sort_rank(b.id, b.provider_id);
        if (ra != rb) {
            return ra < rb;
        }
        return a.display < b.display;
    });
    clamp_models_per_provider(3);
    ensure_selected_model();

    // Avoid models_epoch thrash (UI blink) when pagination/sync yields the same clamped set.
    std::string sig;
    sig.reserve(models.size() * 40);
    for (const auto& m : models) {
        sig.append(m.provider_id);
        sig.push_back('\0');
        sig.append(m.id);
        sig.push_back('\0');
        sig.append(m.display);
        sig.push_back('\0');
    }
    sig.append(selected_model);
    sig.push_back('\0');
    sig.append(settings.default_provider);
    if (sig != models_catalog_sig_) {
        models_catalog_sig_ = std::move(sig);
        ++models_epoch;
    }
}

void Session::ensure_selected_model() {
    if (selected_model.empty() && !settings.selected_model.empty()) {
        selected_model = settings.selected_model;
    }
    if (settings.default_provider != "claude") {
        settings.default_provider = "openai";
    }

    // Claude disconnected while still selected → fall back to OpenAI only.
    if (settings.default_provider == "claude" && !claude_is_connected()) {
        settings.default_provider = "openai";
    }

    auto best_for = [&](const std::string& provider) -> const ModelChoice* {
        const ModelChoice* exact = nullptr;
        const ModelChoice* best = nullptr;
        int best_rank = 0x7fffffff;
        for (const auto& m : models) {
            if (m.provider_id != provider) {
                continue;
            }
            if (!selected_model.empty() && m.id == selected_model) {
                exact = &m;
            }
            const int rank = model_sort_rank(m.id, m.provider_id);
            if (!best || rank < best_rank) {
                best = &m;
                best_rank = rank;
            }
        }
        return exact ? exact : best;
    };

    // Respect default_provider — never steal provider from a cross-catalog id match.
    // (Bug: Claude rows arrive before Codex model/list; matching by id alone flipped
    //  default_provider to Claude and blocked OpenAI ↔ Claude toggle.)
    if (const ModelChoice* hit = best_for(settings.default_provider)) {
        selected_model = hit->id;
        settings.selected_model = hit->id;
        return;
    }

    // Preferred catalog empty (e.g. OpenAI list not in yet) — keep provider preference.
    // Leave selected_model as-is if it already belongs to that provider; else clear face id.
    bool keep = false;
    for (const auto& m : models) {
        if (m.provider_id == settings.default_provider && m.id == selected_model) {
            keep = true;
            break;
        }
    }
    if (!keep) {
        // Stale id from the other provider — clear until catalog fills.
        for (const auto& m : models) {
            if (m.id == selected_model && m.provider_id != settings.default_provider) {
                selected_model.clear();
                settings.selected_model.clear();
                break;
            }
        }
    }
}

void Session::ingest_models(const Json& result, bool replace) {
    if (replace) {
        // Replace OpenAI rows only — keep Claude catalog intact across pagination resets.
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id != "claude"; }),
                     models.end());
    }
    const Json& data = (result.has("data") && result.at("data").is_array())
                           ? result.at("data")
                           : ((result.has("models") && result.at("models").is_array()) ? result.at("models")
                                                                                        : Json::null());
    if (data.is_array()) {
        for (const auto& m : data.array_items()) {
            ModelChoice c;
            c.provider_id = "openai";
            c.id = m.at("id").as_string();
            if (c.id.empty()) {
                c.id = m.at("model").as_string();
            }
            c.display = m.at("displayName").as_string("");
            if (c.display.empty()) {
                c.display = m.at("name").as_string("");
            }
            if (c.display.empty()) {
                c.display = c.id;
            }
            if (c.id.empty()) {
                continue;
            }
            bool dup = false;
            for (const auto& existing : models) {
                if (existing.provider_id == c.provider_id && existing.id == c.id) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            models.push_back(std::move(c));
        }
    }
    merge_claude_models();
    // Drop OpenAI rows if ChatGPT is signed out (keep Claude when connected).
    if (!account.signed_in) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id != "claude"; }),
                     models.end());
    }
    finalize_model_catalog();
    const std::string next = result.at("nextCursor").as_string();
    if (next.empty()) {
        // Some payloads use camelCase alternate.
        const std::string alt = result.at("next_cursor").as_string();
        if (!alt.empty() && alt != "null") {
            request_models(alt);
        }
    } else if (next != "null") {
        request_models(next);
    }
}

void Session::login_chatgpt() {
    Json p = Json::object();
    p["type"] = Json::string("chatgpt");
    p["useHostedLoginSuccessPage"] = Json::boolean(true);
    p["appBrand"] = Json::string("chatgpt");
    login_id_req_ = send_req("account/login/start", std::move(p));
    set_state(AppState::SigningIn, L"Starting ChatGPT sign-in");
}

void Session::logout() {
    logout_id_ = send_req("account/logout", Json::object());
}

std::wstring Session::conversation_cwd() const {
    if (!active_thread_id.empty()) {
        if (auto* c = store.by_thread(active_thread_id)) {
            if (auto* p = store.by_id(c->project_id)) {
                if (!p->root.empty()) {
                    return p->root;
                }
            }
        }
    }
    if (!project_root.empty()) {
        return project_root;
    }
    return paths.workspace;
}

bool Session::has_project_grant() const {
    if (!active_thread_id.empty()) {
        if (auto* c = store.by_thread(active_thread_id)) {
            if (auto* p = store.by_id(c->project_id)) {
                return !p->root.empty();
            }
        }
    }
    return !project_root.empty();
}

std::wstring Session::grant_label() const {
    if (!has_project_grant()) {
        return L"No project — agent read-only";
    }
    std::wstring label = L"Grant: " + conversation_cwd() + L" (browse / edit)";
    if (!knowledge_accessible_paths_.empty()) {
        label += L" + " + std::to_wstring(knowledge_accessible_paths_.size()) + L" knowledge";
    }
    return label;
}

void Session::set_knowledge_accessible_paths(std::vector<std::wstring> paths) {
    knowledge_accessible_paths_ = std::move(paths);
}

std::string Session::knowledge_grant_preamble() const {
    if (knowledge_accessible_paths_.empty()) {
        return {};
    }
    std::ostringstream oss;
    oss << "Knowledge folders granted for this turn (absolute paths; not limited to the project cwd):\n";
    for (const auto& p : knowledge_accessible_paths_) {
        oss << "- " << utf8(p) << "\n";
    }
    oss << "Under a .md root, handoffs are in handoff/ (singular) and blueprints in blueprints/. "
           "workspace_index.sqlite is a binary SQLite index — do not treat it as empty markdown; "
           "read the .md files under those folders (or use STRATA if available).\n";
    return oss.str();
}

bool Session::sync_lockdown_config() {
    if (paths.codex_home.empty()) {
        paths = make_paths();
    }
    const bool allow_shell = has_project_grant();
    const auto mcp_runtime = collect_mcp_runtime(mcp_manager_, store.active_project_id);
    const bool ok = write_isolated_codex_config(paths, allow_shell, knowledge_accessible_paths_, mcp_runtime.servers);
    if (ok) {
        isolated_home_note = L"CODEX_HOME is isolated: " + paths.codex_home +
                             L"  CreateProcess cwd=" + paths.workspace;
        if (allow_shell) {
            isolated_home_note += L"  Grant browse/edit: " + conversation_cwd();
            if (!knowledge_accessible_paths_.empty()) {
                isolated_home_note += L" + knowledge roots: ";
                for (size_t i = 0; i < knowledge_accessible_paths_.size(); ++i) {
                    if (i != 0) {
                        isolated_home_note += L"; ";
                    }
                    isolated_home_note += knowledge_accessible_paths_[i];
                }
            }
        } else {
            isolated_home_note += L"  No project grant (agent read-only)";
        }
    }
    // Shell enablement is baked into CreateProcess args — restart when grant flips.
    if (ok && runtime_.running() && allow_shell != runtime_allow_shell_ && hwnd_ && line_msg_ &&
        !settings.codex_path.empty()) {
        runtime_.stop();
        runtime_allow_shell_ = false;
        set_state(AppState::Connecting, L"Restarting Codex for project grant…");
        std::wstring err;
        if (!runtime_.start(settings.codex_path, paths.codex_home, paths.workspace, paths.stderr_log, hwnd_, line_msg_,
                            allow_shell, mcp_runtime.environment, &err)) {
            set_state(AppState::Failed, err.empty() ? L"Runtime restart failed" : err);
            return false;
        }
        runtime_allow_shell_ = allow_shell;
        Json client = Json::object();
        client["name"] = Json::string("scylla_gpt");
        client["title"] = Json::string("Scylla");
        client["version"] = Json::string("0.1.0");
        Json params = Json::object();
        params["clientInfo"] = std::move(client);
        initialize_id_ = send_req("initialize", std::move(params));
    }
    return ok;
}

std::string Session::account_scope() const {
    if (account.signed_in && !account.email.empty()) {
        return "chatgpt:" + account.email;
    }
    return account.type.empty() ? std::string("none") : account.type;
}

void Session::new_conversation() {
    activity = {};
    active_thread_id.clear();
    active_turn_id.clear();
    stream_buffer.clear();
    history_messages.clear();
    if (account.signed_in) set_state(AppState::Ready, L"Ready");
    if (thread_start_id_ >= 0) return;
    Json p = Json::object();
    if (!selected_model.empty()) {
        p["model"] = Json::string(selected_model);
    }
    const std::wstring cwd = conversation_cwd();
    p["cwd"] = Json::string(utf8(cwd));
    p["approvalPolicy"] = Json::string("on-request");
    p["sandbox"] = Json::string(has_project_grant() ? "workspace-write" : "read-only");
    p["serviceName"] = Json::string("scylla_gpt");
    thread_start_id_ = send_req("thread/start", std::move(p));
}

void Session::send_user(const std::string& text) {
    if (settings.default_provider == "claude") {
        activity.begin();
        send_claude_user(text);
        return;
    }
    if (active_thread_id.empty()) {
        if (thread_start_id_ < 0) new_conversation();
        if (thread_start_id_ >= 0) {
            pending_thread_prompts_[thread_start_id_] = text;
            activity.begin();
            activity.phase = "Starting conversation";
        }
        return;
    }
    send_user_to_thread(text, active_thread_id, true);
}

void Session::send_user_to_thread(const std::string& text, const std::string& thread_id, bool foreground) {
    std::string payload = text;
    auto* naming_chat = store.by_thread(thread_id);
    if (naming_chat) {
        naming_chat->updated_at = std::time(nullptr);
        if (!naming_chat->title_manual && !naming_chat->title_generated &&
            (naming_chat->title.empty() || naming_chat->title == "New Chat"))
            naming_chat->title = short_chat_title(chat_title_seed);
        store.save(paths.store_path);
    }
    const std::string grant = knowledge_grant_preamble() + chat_title_request(naming_chat);
    const std::string ctx = snapshot_context(context_chips);
    if (!grant.empty() || !ctx.empty()) {
        std::string head;
        if (!grant.empty()) {
            head = grant;
        }
        if (!ctx.empty()) {
            if (!head.empty()) {
                head += "\n";
            }
            head += ctx;
        }
        payload = head + "\n---\n" + text;
    }
    Json input = Json::array();
    Json item = Json::object();
    item["type"] = Json::string("text");
    item["text"] = Json::string(payload);
    input.push(std::move(item));
    Json p = Json::object();
    p["threadId"] = Json::string(thread_id);
    p["input"] = std::move(input);
    const std::wstring cwd = conversation_cwd();
    p["cwd"] = Json::string(utf8(cwd));
    p["approvalPolicy"] = Json::string("on-request");
    Json sand = Json::object();
    if (has_project_grant()) {
        sand["type"] = Json::string("workspaceWrite");
        sand["networkAccess"] = Json::boolean(false);
        Json roots = Json::array();
        roots.push(Json::string(utf8(cwd)));
        // Knowledge sources (e.g. D:\projects\.md) sit outside the project folder;
        // without these roots the agent cannot list handoff/blueprints under the grant.
        for (const auto& kpath : knowledge_accessible_paths_) {
            if (kpath.empty()) {
                continue;
            }
            if (_wcsicmp(kpath.c_str(), cwd.c_str()) == 0) {
                continue;
            }
            roots.push(Json::string(utf8(kpath)));
        }
        sand["writableRoots"] = std::move(roots);
    } else {
        sand["type"] = Json::string("readOnly");
        sand["networkAccess"] = Json::boolean(false);
    }
    p["sandboxPolicy"] = std::move(sand);
    if (!selected_model.empty()) {
        p["model"] = Json::string(selected_model);
    }
    turn_start_id_ = send_req("turn/start", std::move(p));
    if (turn_start_id_ < 0) return;
    turn_start_threads_[turn_start_id_] = thread_id;
    auto& running = thread_runtime_[thread_id];
    running.activity.begin();
    running.stream.clear();
    if (naming_chat) {
        if (!naming_chat->local_messages.is_array()) naming_chat->local_messages = Json::array();
        Json message = Json::object();
        message["user"] = Json::boolean(true);
        message["text"] = Json::string(text);
        naming_chat->local_messages.push(std::move(message));
        store.save(paths.store_path);
    }
    if (foreground) {
        activity = running.activity;
        history_messages.push_back({true, text});
        stream_buffer.clear();
        set_state(AppState::Generating, L"Generating");
    }
}

void Session::ensure_claude_thread() {
    if (!active_thread_id.empty()) {
        if (auto* c = store.by_thread(active_thread_id)) {
            if (c->provider_id == "claude") {
                return;
            }
        }
    }
    const std::string tid = "claude-" + make_uuid();
    active_thread_id = tid;
    settings.last_thread_id = tid;
    if (auto* pr = store.active()) {
        pr->last_thread_id = tid;
    }
    auto* conv = store.upsert_thread(store.active_project_id, account_scope(), tid, "New Chat", "");
    if (conv) {
        conv->provider_id = "claude";
        conv->backend = "claude-code-print";
        conv->resumable = false;
    }
    store.save(paths.store_path);
}

void Session::send_claude_user(const std::string& text) {
    if (!claude_is_connected()) {
        last_error = "Claude is not connected";
        set_state(AppState::Failed, L"Claude not connected");
        return;
    }
    if (claude_busy_) {
        return;
    }
    ensure_claude_thread();
    std::string payload = text;
    auto* naming_chat = store.by_thread(active_thread_id);
    if (naming_chat) {
        naming_chat->updated_at = std::time(nullptr);
        if (!naming_chat->title_manual && !naming_chat->title_generated &&
            (naming_chat->title.empty() || naming_chat->title == "New Chat"))
            naming_chat->title = short_chat_title(chat_title_seed);
        store.save(paths.store_path);
    }
    const std::string grant = knowledge_grant_preamble() + chat_title_request(naming_chat);
    const std::string ctx = snapshot_context(context_chips);
    if (!grant.empty() || !ctx.empty()) {
        std::string head;
        if (!grant.empty()) {
            head = grant;
        }
        if (!ctx.empty()) {
            if (!head.empty()) {
                head += "\n";
            }
            head += ctx;
        }
        payload = head + "\n---\n" + text;
    }
    if (!history_messages.empty()) {
        std::ostringstream oss;
        oss << "Conversation so far:\n";
        const std::size_t start = history_messages.size() > 12 ? history_messages.size() - 12 : 0;
        for (std::size_t i = start; i < history_messages.size(); ++i) {
            oss << (history_messages[i].user ? "User: " : "Assistant: ") << history_messages[i].text << "\n";
        }
        oss << "\nUser: " << payload << "\nAssistant:";
        payload = oss.str();
    }
    history_messages.push_back({true, text});
    if (auto* chat = store.by_thread(active_thread_id)) {
        chat->local_messages = Json::array();
        for (const auto& message : history_messages) {
            Json item = Json::object();
            item["user"] = Json::boolean(message.user);
            item["text"] = Json::string(message.text);
            chat->local_messages.push(std::move(item));
        }
        store.save(paths.store_path);
    }
    stream_buffer.clear();
    claude_busy_ = true;
    claude_cancel_ = false;
    set_state(AppState::Generating, L"Claude generating…");

    const std::string model = selected_model.empty() ? "claude-sonnet-4-6" : selected_model;
    const std::wstring cwd = conversation_cwd();
    HWND hwnd = hwnd_;
    std::thread([this, payload, model, cwd, hwnd]() {
        std::string result;
        std::wstring err;
        const bool ok = !claude_cancel_ && claude_code_print(payload, model, cwd, &result, &err, 300000);
        auto* r = new ClaudePrintResult{};
        if (claude_cancel_) {
            r->ok = false;
            r->error = L"Cancelled";
        } else {
            r->ok = ok;
            r->text = std::move(result);
            r->error = std::move(err);
        }
        if (hwnd) {
            PostMessageW(hwnd, WM_SCYLLA_CLAUDE_DONE, 0, reinterpret_cast<LPARAM>(r));
        } else {
            delete r;
        }
    }).detach();
}

void Session::complete_claude_print(bool ok, const std::string& text, const std::wstring& error) {
    claude_busy_ = false;
    if (claude_cancel_) {
        activity.finish("Interrupted");
        set_state(AppState::Interrupted, L"Interrupted");
        return;
    }
    if (!ok) {
        last_error = error.empty() ? "Claude print failed" : utf8(error);
        set_state(AppState::Failed, error.empty() ? L"Claude failed" : error);
        return;
    }
    stream_buffer = text;
    history_messages.push_back({false, text});
    if (auto* conv = store.by_thread(active_thread_id)) {
        conv->local_messages = Json::array();
        for (const auto& message : history_messages) {
            Json item = Json::object();
            item["user"] = Json::boolean(message.user);
            item["text"] = Json::string(message.text);
            conv->local_messages.push(std::move(item));
        }
        conv->updated_at = std::time(nullptr);
        accept_chat_title(*conv, text);
        conv->preview = text.size() > 120 ? text.substr(0, 120) + "…" : text;
        store.save(paths.store_path);
    }
    set_state(AppState::Ready, L"Ready");
}

void Session::open_thread(const std::string& id) {
    active_thread_id = id;
    select_thread_runtime(id);
    settings.last_thread_id = id;
    if (auto* c = store.by_thread(id)) {
        if (auto* p = store.by_id(c->project_id)) {
            project_root = p->root;
            settings.project_folder = p->root;
        }
        history_messages.clear();
        if (c->local_messages.is_array()) for (const auto& message : c->local_messages.array_items())
            history_messages.push_back({message.at("user").as_bool(false), message.at("text").as_string()});
        transcript_replace = true;
    }
    if (auto* c = store.by_thread(id); c && c->provider_id == "claude") {
        settings.default_provider = "claude";
        history_messages.clear();
        if (c->local_messages.is_array()) for (const auto& message : c->local_messages.array_items())
            history_messages.push_back({message.at("user").as_bool(false), message.at("text").as_string()});
        stream_buffer.clear();
        transcript_replace = true;
        return;
    }
    settings.default_provider = "openai";
    sync_lockdown_config();
    Json p = Json::object();
    p["threadId"] = Json::string(id);
    thread_resume_id_ = send_req("thread/resume", p);
    Json r = Json::object();
    r["threadId"] = Json::string(id);
    r["includeTurns"] = Json::boolean(true);
    thread_read_id_ = send_req("thread/read", std::move(r));
    if (thread_read_id_ >= 0) thread_read_threads_[thread_read_id_] = id;
}

void Session::cancel_turn() {
    if (activity.busy) activity.phase = "Cancelling";
    if (claude_busy_) {
        claude_cancel_ = true;
        set_state(AppState::Interrupted, L"Cancelling Claude…");
        return;
    }
    const auto running = thread_runtime_.find(active_thread_id);
    if (active_thread_id.empty() || running == thread_runtime_.end() || running->second.turn_id.empty()) {
        return;
    }
    Json p = Json::object();
    p["threadId"] = Json::string(active_thread_id);
    p["turnId"] = Json::string(running->second.turn_id);
    turn_interrupt_id_ = send_req("turn/interrupt", std::move(p));
    set_state(AppState::Interrupted, L"Cancelling…");
}

bool Session::thread_busy(const std::string& thread_id) const {
    const auto found = thread_runtime_.find(thread_id);
    return found != thread_runtime_.end() && found->second.activity.busy;
}

void Session::select_thread_runtime(const std::string& thread_id) {
    const auto found = thread_runtime_.find(thread_id);
    if (found == thread_runtime_.end()) {
        activity = {};
        active_turn_id.clear();
        stream_buffer.clear();
        if (account.signed_in) set_state(AppState::Ready, L"Ready");
        return;
    }
    activity = found->second.activity;
    active_turn_id = found->second.turn_id;
    stream_buffer = found->second.stream;
    if (account.signed_in) set_state(activity.busy ? AppState::Generating : AppState::Ready,
                                     activity.busy ? L"Generating" : L"Ready");
}

void Session::refresh_threads() {
    Json p = Json::object();
    p["limit"] = Json::number(40);
    thread_list_id_ = send_req("thread/list", std::move(p));
}

std::string Session::draft_for(const std::string& thread_id) const {
    const std::string key = thread_id.empty() ? std::string("_new") : thread_id;
    return settings.drafts.at(key).as_string("");
}

void Session::set_draft(const std::string& thread_id, const std::string& text) {
    const std::string key = thread_id.empty() ? std::string("_new") : thread_id;
    settings.drafts[key] = Json::string(text);
}

void Session::apply_account(const Json& acc) {
    account = {};
    if (acc.is_null()) {
        return;
    }
    account.type = acc.at("type").as_string("");
    account.email = acc.at("email").as_string("");
    account.plan = acc.at("planType").as_string("");
    account.signed_in = account.type == "chatgpt";
    if (account.type == "apiKey") {
        last_error = "This Codex home is signed in with an API key. Scylla requires ChatGPT subscription sign-in. Use Sign in.";
    }
}

void Session::extract_history(const Json& thread, const std::string& thread_id) {
    if (thread_id != active_thread_id) return;
    history_messages.clear();
    const Json& turns = thread.at("turns");
    if (turns.is_array()) for (const auto& turn : turns.array_items()) {
        const Json& items = turn.at("items");
        const Json* list = &items;
        if (!items.is_array()) {
            continue;
        }
        for (const auto& item : list->array_items()) {
            const std::string type = normalized_item_type(item.at("type").as_string());
            const std::string text = item_text(item);
            if (type == "usermessage" || type == "user" || type == "inputmessage") {
                history_messages.push_back({true, text});
            } else if (type == "agentmessage" || type == "assistantmessage" || type == "assistant") {
                history_messages.push_back({false, text});
            }
        }
    }
    const bool provider_has_user = std::any_of(history_messages.begin(), history_messages.end(),
                                               [](const HistoryMessage& m) { return m.user; });
    if (!provider_has_user) {
        if (auto* chat = store.by_thread(active_thread_id); chat && chat->local_messages.is_array()) {
            std::vector<HistoryMessage> local;
            for (const auto& message : chat->local_messages.array_items()) {
                local.push_back({message.at("user").as_bool(false), message.at("text").as_string()});
            }
            const auto local_agents = std::count_if(local.begin(), local.end(),
                                                    [](const HistoryMessage& m) { return !m.user; });
            std::size_t seen_agents = 0;
            for (const auto& message : history_messages) {
                if (!message.user && seen_agents++ >= local_agents) local.push_back(message);
            }
            if (!local.empty()) history_messages = std::move(local);
        }
    }
    save_local_history(store, paths.store_path, active_thread_id, history_messages);
    if (const auto live = thread_runtime_.find(thread_id); live != thread_runtime_.end()) {
        stream_buffer = live->second.stream;
    } else {
        stream_buffer.clear();
    }
    transcript_replace = true;
}

void Session::handle_server_request(const Json& msg) {
    const std::string method = msg.at("method").as_string();
    const Json& id = msg.at("id");
    const std::string request_thread = msg.at("params").at("threadId").as_string();
    const bool foreground = request_thread.empty() || request_thread == active_thread_id;
    if (foreground) set_state(AppState::AwaitingAction, utf16("Approval: " + method));

    // Under a project grant: accept file/patch and sandboxed shell so the agent can
    // browse, search, and edit inside writableRoots. Apps/hooks/network stay off.
    if (method == "item/fileChange/requestApproval" || method == "applyPatchApproval" ||
        method == "item/commandExecution/requestApproval" || method == "execCommandApproval") {
        Json result = Json::object();
        if (has_project_grant()) {
            result["decision"] = Json::string("accept");
            send_result(id, std::move(result));
            if (foreground) {
                set_state(AppState::Generating, utf16("Approved: " + method));
                last_error.clear();
            }
        } else {
            result["decision"] = Json::string("decline");
            send_result(id, std::move(result));
            if (foreground) last_error = "Declined " + method + " (no project grant — agent is read-only).";
        }
        return;
    }
    if (method == "item/permissions/requestApproval") {
        Json result = Json::object();
        result["permissions"] = Json::array();
        send_result(id, std::move(result));
        last_error = "Denied extra permissions request.";
        return;
    }
    if (method == "mcpServer/elicitation/request" || method == "item/tool/requestUserInput") {
        Json result = Json::object();
        result["action"] = Json::string("decline");
        result["content"] = Json::null();
        send_result(id, std::move(result));
        return;
    }
    Json err = Json::object();
    err["code"] = Json::number(-32601);
    err["message"] = Json::string("Scylla does not handle " + method);
    Json msg_err = Json::object();
    msg_err["id"] = id;
    msg_err["error"] = std::move(err);
    runtime_.write_line(msg_err.dump());
}

void Session::handle_notification(const Json& msg) {
    const std::string method = msg.at("method").as_string();
    const Json& p = msg.at("params");
    if (method == "scylla/runtimeClosed") {
        if (p.at("pid").as_int() == runtime_.pid() && settings.default_provider != "claude")
            set_state(AppState::Failed, L"Runtime disconnected");
        return;
    }
    std::string thread_id = p.at("threadId").as_string();
    const std::string event_turn_id = p.at("turnId").as_string(p.at("turn").at("id").as_string().c_str());
    if (thread_id.empty() && !event_turn_id.empty()) {
        if (const auto found = turn_threads_.find(event_turn_id); found != turn_threads_.end()) thread_id = found->second;
    }
    ThreadRuntime* running = thread_id.empty() ? nullptr : &thread_runtime_[thread_id];
    if (running) {
        running->activity.codex(method, p);
        if (thread_id == active_thread_id) activity = running->activity;
    }
    if (method == "account/login/completed") {
        const bool ok = p.at("success").as_bool(false);
        if (ok) {
            account_read_id_ = send_req("account/read", Json::object());
            refresh_models();
            set_state(AppState::Ready, L"Signed in");
        } else {
            last_error = p.at("error").as_string("sign-in failed");
            set_state(AppState::Failed, L"Sign-in failed");
        }
        return;
    }
    if (method == "account/updated") {
        account.type = p.at("authMode").as_string("");
        account.plan = p.at("planType").as_string("");
        account.signed_in = account.type == "chatgpt";
        return;
    }
    if (method == "item/agentMessage/delta") {
        if (!running) return;
        running->stream += p.at("delta").as_string();
        if (thread_id == active_thread_id) stream_buffer = running->stream;
        return;
    }
    if (method == "turn/started") {
        if (thread_id.empty()) return;
        auto& turn = thread_runtime_[thread_id];
        turn.turn_id = p.at("turn").at("id").as_string();
        if (!turn.turn_id.empty()) turn_threads_[turn.turn_id] = thread_id;
        if (!turn.activity.busy) turn.activity.begin();
        if (thread_id == active_thread_id) {
            active_turn_id = turn.turn_id;
            activity = turn.activity;
            set_state(AppState::Generating, L"Generating");
        }
        return;
    }
    if (method == "turn/completed") {
        if (thread_id.empty()) return;
        auto& turn = thread_runtime_[thread_id];
        if (auto* chat = store.by_thread(thread_id)) {
            if (!turn.stream.empty()) {
                if (!chat->local_messages.is_array()) chat->local_messages = Json::array();
                Json message = Json::object();
                message["user"] = Json::boolean(false);
                message["text"] = Json::string(turn.stream);
                chat->local_messages.push(std::move(message));
            }
            chat->updated_at = std::time(nullptr);
            accept_chat_title(*chat, turn.stream);
            store.save(paths.store_path);
        }
        const std::string st = p.at("turn").at("status").as_string();
        turn.activity.finish(st == "interrupted" ? "Interrupted" : st == "failed" ? "Failed" : "Completed");
        if (!turn.turn_id.empty()) turn_threads_.erase(turn.turn_id);
        turn.turn_id.clear();
        ++chats_epoch;
        if (thread_id != active_thread_id) return;
        if (!turn.stream.empty() &&
            (history_messages.empty() || history_messages.back().user || history_messages.back().text != turn.stream))
            history_messages.push_back({false, turn.stream});
        stream_buffer = turn.stream;
        activity = turn.activity;
        active_turn_id.clear();
        if (st == "interrupted") {
            set_state(AppState::Interrupted, L"Interrupted — partial output kept");
        } else if (st == "failed") {
            last_error = p.at("turn").at("error").at("message").as_string("turn failed");
            set_state(AppState::Failed, L"Turn failed");
        } else {
            set_state(AppState::Ready, L"Ready");
        }
        return;
    }
    if (method == "error") {
        last_error = p.at("error").at("message").as_string("runtime error");
        const std::string info = p.at("error").at("codexErrorInfo").as_string("");
        if (info == "Unauthorized" || last_error.find("auth") != std::string::npos) {
            set_state(AppState::Failed, L"Login expired");
        } else if (p.at("willRetry").as_bool(false)) {
            if (activity.busy) activity.phase = "Retrying";
        } else {
            set_state(AppState::Failed, L"Turn failed");
        }
        return;
    }
}

void Session::handle_response(const Json& msg) {
    const std::int64_t id = msg.at("id").as_int(-1);
    if (msg.has("error")) {
        last_error = msg.at("error").at("message").as_string("request error");
        if (const auto pending = pending_thread_prompts_.find(id); pending != pending_thread_prompts_.end()) {
            pending_thread_prompts_.erase(pending);
            if (id == thread_start_id_) thread_start_id_ = -1;
        }
        if (const auto started = turn_start_threads_.find(id); started != turn_start_threads_.end()) {
            auto& running = thread_runtime_[started->second];
            running.activity.finish("Failed");
            if (started->second == active_thread_id) activity = running.activity;
            turn_start_threads_.erase(started);
        }
        if (id == initialize_id_) {
            set_state(AppState::Failed, L"Initialize failed");
        } else if (id == turn_start_id_ || id == thread_start_id_) {
            set_state(AppState::Failed, L"Turn could not start");
        } else if (id == turn_interrupt_id_) {
            set_state(AppState::Generating, L"Cancellation failed — still working");
        }
        return;
    }
    const Json& result = msg.at("result");
    if (id == initialize_id_) {
        send_notify("initialized", Json::object());
        isolated_home_note = L"Runtime CODEX_HOME: " + utf16(result.at("codexHome").as_string());
        if (isolated_home_note == L"Runtime CODEX_HOME: ") {
            isolated_home_note = L"Runtime CODEX_HOME: " + paths.codex_home;
        }
        set_state(AppState::Ready, L"Connected");
        after_ready();
        return;
    }
    if (id == account_read_id_) {
        apply_account(result.at("account"));
        if (!account.signed_in) {
            set_state(AppState::SigningIn, L"ChatGPT sign-in required");
        } else {
            set_state(AppState::Ready, utf16("Ready - " + account.email + " (" + account.plan + ")"));
            refresh_models();
        }
        return;
    }
    if (id == logout_id_) {
        account = {};
        set_state(AppState::SigningIn, L"Signed out");
        return;
    }
    if (id == login_id_req_) {
        login_id = result.at("loginId").as_string();
        const std::string url = result.at("authUrl").as_string();
        if (!url.empty()) {
            ShellExecuteW(nullptr, L"open", utf16(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            set_state(AppState::SigningIn, L"Complete sign-in in the browser, then wait for confirmation here");
        } else if (result.at("type").as_string() == "chatgptDeviceCode") {
            last_error = "Device code: " + result.at("userCode").as_string() + " at " + result.at("verificationUrl").as_string();
            ShellExecuteW(nullptr, L"open", utf16(result.at("verificationUrl").as_string()).c_str(), nullptr, nullptr,
                           SW_SHOWNORMAL);
        }
        return;
    }
    if (id == model_list_id_) {
        const bool replace = models_replace_next_;
        models_replace_next_ = false;
        ingest_models(result, replace);
        return;
    }
    if (id == thread_list_id_) {
        threads.clear();
        const Json& data = result.at("data");
        if (data.is_array()) {
            for (const auto& t : data.array_items()) {
                ThreadSummary s;
                s.id = t.at("id").as_string();
                s.name = thread_label(t);
                s.preview = t.at("preview").as_string("");
                if (auto* c = store.by_thread(s.id)) {
                    c->updated_at = (std::max)(c->updated_at, t.at("updatedAt").as_int(0));
                }
                if (!s.id.empty()) {
                    threads.push_back(s);
                }
            }
        }
        website_history_seen = false;  // Codex rollouts only unless a documented web id appears
        store.save(paths.store_path);
        ++chats_epoch;
        return;
    }
    if (id == thread_start_id_) {
        thread_start_id_ = -1;
        const std::string created_thread_id = result.at("thread").at("id").as_string();
        const bool foreground = active_thread_id.empty();
        if (foreground) {
            active_thread_id = created_thread_id;
            settings.last_thread_id = created_thread_id;
            if (auto* pr = store.active()) pr->last_thread_id = created_thread_id;
        }
        store.upsert_thread(store.active_project_id, account_scope(), created_thread_id, "New Chat", "");
        if (auto* conv = store.by_thread(created_thread_id)) {
            conv->provider_id = settings.default_provider.empty() ? "openai" : settings.default_provider;
        }
        store.save(paths.store_path);
        const auto pending_it = pending_thread_prompts_.find(id);
        const std::string pending = pending_it == pending_thread_prompts_.end() ? std::string{} : pending_it->second;
        if (pending_it != pending_thread_prompts_.end()) pending_thread_prompts_.erase(pending_it);
        if (!pending.empty()) {
            send_user_to_thread(pending, created_thread_id, foreground);
        }
        refresh_threads();
        return;
    }
    if (id == thread_resume_id_) {
        active_thread_id = result.at("thread").at("id").as_string();
        return;
    }
    if (const auto read = thread_read_threads_.find(id); read != thread_read_threads_.end()) {
        const std::string thread_id = read->second;
        thread_read_threads_.erase(read);
        extract_history(result.at("thread"), thread_id);
        return;
    }
    if (const auto started = turn_start_threads_.find(id); started != turn_start_threads_.end()) {
        const std::string thread_id = started->second;
        turn_start_threads_.erase(started);
        auto& running = thread_runtime_[thread_id];
        running.turn_id = result.at("turn").at("id").as_string();
        if (!running.turn_id.empty()) turn_threads_[running.turn_id] = thread_id;
        if (thread_id == active_thread_id) active_turn_id = running.turn_id;
        return;
    }
}

void Session::handle_line(const std::string& line) {
    std::string err;
    Json msg = Json::parse(line, &err);
    if (!err.empty() || !msg.is_object()) {
        last_error = "Malformed RPC: " + (err.empty() ? line.substr(0, 120) : err);
        return;
    }
    const bool has_id = msg.has("id") && !msg.at("id").is_null();
    const bool has_method = msg.has("method") && msg.at("method").is_string();
    const bool has_result = msg.has("result") || msg.has("error");
    if (has_id && has_method && !has_result) {
        handle_server_request(msg);
        return;
    }
    if (has_method && !has_id) {
        handle_notification(msg);
        return;
    }
    if (has_id && has_result) {
        handle_response(msg);
        return;
    }
}

}  // namespace scyllagpt
