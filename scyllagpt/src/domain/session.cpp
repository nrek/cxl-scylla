#include "scyllagpt/session.h"
#include "scyllagpt/chat_history.h"
#include "scyllagpt/codex_thread_util.h"
#include "scyllagpt/history_merge.h"
#include "scyllagpt/chat_mode.h"
#include "scyllagpt/file_io.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/project_context.h"
#include <filesystem>

#include "scyllagpt/lockdown.h"
#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/provider_http.h"
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

bool is_thread_missing_error(std::string err) {
    for (char& ch : err) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return err.find("no rollout found") != std::string::npos ||
           err.find("rollout not found") != std::string::npos ||
           err.find("unknown thread") != std::string::npos ||
           err.find("thread not found") != std::string::npos ||
           err.find("no such thread") != std::string::npos ||
           err.find("failed to find rollout") != std::string::npos;
}

std::string thread_path_field(const Json& t) {
    for (const char* key : {"path", "filePath", "rolloutPath", "rollout_path"}) {
        if (t.has(key) && t.at(key).is_string()) {
            const std::string p = t.at(key).as_string();
            if (!p.empty()) return p;
        }
    }
    return {};
}

std::wstring find_rollout_file(const std::wstring& codex_home, const std::string& thread_id) {
    if (codex_home.empty() || thread_id.empty()) return {};
    const std::wstring needle = utf16(thread_id);
    const std::filesystem::path root = std::filesystem::path(codex_home) / L"sessions";
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return {};
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto name = it->path().filename().wstring();
        if (name.find(needle) == std::wstring::npos) continue;
        const auto ext = it->path().extension().wstring();
        if (_wcsicmp(ext.c_str(), L".jsonl") == 0 || _wcsicmp(ext.c_str(), L".json") == 0)
            return it->path().wstring();
    }
    return {};
}

bool is_supported_gpt_generation(const std::string& id) {
    // Codex remains authoritative for model availability. This only enforces the
    // Manage Models floor requested by Scylla: do not expose pre-GPT-4 entries.
    if (id.rfind("gpt-", 0) != 0) return true;
    std::size_t pos = 4;
    int major = 0;
    bool found_digit = false;
    while (pos < id.size() && std::isdigit(static_cast<unsigned char>(id[pos]))) {
        found_digit = true;
        major = (major * 10) + (id[pos] - '0');
        ++pos;
    }
    return !found_digit || major >= 4;
}

bool is_gpt4_generation(const std::string& id) {
    return id.rfind("gpt-4", 0) == 0;
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

void Session::log_payload(const std::string& direction, const std::string& payload) {
    // Called on the session/UI thread. Never write traffic to disk.
    auto value = Json::parse(payload);
    std::function<Json(const Json&)> redact = [&](const Json& v) -> Json {
        if (v.is_object()) {
            auto out = Json::object();
            for (const auto& [key, child] : v.object_items()) {
                std::string lower = key;
                for (auto& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                const bool secret = lower.find("token") != std::string::npos ||
                    lower.find("secret") != std::string::npos || lower.find("password") != std::string::npos ||
                    lower.find("apikey") != std::string::npos || lower.find("api_key") != std::string::npos ||
                    lower == "authorization" || lower == "headers" || lower == "env" ||
                    lower == "environment" || lower == "private_key";
                out[key] = secret ? Json::string("[redacted]") : redact(child);
            }
            return out;
        }
        if (v.is_array()) {
            auto out = Json::array();
            for (const auto& child : v.array_items()) out.push(redact(child));
            return out;
        }
        return v;
    };
    std::string preview = value.is_null() ? Json::string(payload).dump() : redact(value).dump();
    if (preview.size() > 2048) {
        size_t end = 2048;
        while (end && (static_cast<unsigned char>(preview[end]) & 0xc0) == 0x80) --end;
        preview.resize(end);
        preview += " ... [truncated]";
    }
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char stamp[32]{};
    snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03uZ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    payload_log.push_back(std::string(stamp) + " " + direction + " " + preview);
    while (payload_log.size() > 200) payload_log.pop_front();
}
std::int64_t Session::send_req(const char* method, Json params) {
    const std::int64_t id = next_id_++;
    Json msg = Json::object();
    msg["method"] = Json::string(method);
    msg["id"] = Json::number(id);
    if (!params.is_null()) {
        msg["params"] = std::move(params);
    }
    log_payload("OUT", msg.dump());
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
    log_payload("OUT", msg.dump());
    runtime_.write_line(msg.dump());
}

void Session::send_result(const Json& id, Json result) {
    Json msg = Json::object();
    msg["id"] = id;
    msg["result"] = std::move(result);
    log_payload("OUT", msg.dump());
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

bool Session::start_runtime(LineSink on_line, ClaudeDoneSink on_claude_done, std::wstring* error) {
    // Starting an already-live session must preserve its loaded threads and callbacks.
    // Runtime::start replaces the process, leaving active_thread_id stale.
    if (runtime_.running()) return true;
    account_loaded = false;
    on_line_ = std::move(on_line);
    on_claude_done_ = std::move(on_claude_done);
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
    auto writable_roots = knowledge_accessible_paths_;
    if (const auto* project = store.active()) for (const auto& root : project->roots)
        if (_wcsicmp(root.c_str(), project->root.c_str()) != 0) writable_roots.push_back(root);
    auto runtime_servers = mcp_runtime.servers;
    if (!broker_mcp_server_.name.empty()) runtime_servers.push_back(broker_mcp_server_);
    if (!write_isolated_codex_config(paths, allow_shell, writable_roots, runtime_servers)) {
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
    if (!runtime_.start(settings.codex_path, paths.codex_home, paths.workspace, paths.stderr_log, on_line_,
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
                                    [](const ModelChoice& m) { return m.provider_id == "openai"; }),
                     models.end());
    }
    merge_claude_models();
    if (!openai_api_key_present()) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id == "openai-api"; }),
                     models.end());
    }
    if (!claude_api_connected()) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id == "claude-api"; }),
                     models.end());
    }
    finalize_model_catalog();
}

void Session::refresh_claude_model_catalog(bool force_cli) {
    (void)force_cli;
    if (!claude_account_connected()) {
        return;
    }
    claude_code_list_models(true);
    merge_claude_models();
    finalize_model_catalog();
}

void Session::refresh_openai_api_models() {
    if (!openai_api_key_present()) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id == "openai-api"; }),
                     models.end());
        finalize_model_catalog();
        return;
    }
    std::vector<HttpModelRow> rows;
    std::wstring err;
    if (!openai_api_list_models(&rows, &err)) {
        last_error = err.empty() ? "OpenAI API model list failed" : utf8(err);
        return;
    }
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(rows.size());
    for (const auto& r : rows) {
        pairs.emplace_back(r.id, r.display);
    }
    merge_http_provider_models("openai-api", pairs);
    settings.verbose_agent_progress = load_settings(paths.settings_path).verbose_agent_progress;
    save_settings(paths.settings_path, settings);
    finalize_model_catalog();
}

void Session::refresh_claude_api_models() {
    if (!claude_api_connected()) {
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id == "claude-api"; }),
                     models.end());
        finalize_model_catalog();
        return;
    }
    std::vector<HttpModelRow> rows;
    std::wstring err;
    if (!claude_api_list_models(&rows, &err)) {
        last_error = err.empty() ? "Claude API model list failed" : utf8(err);
        return;
    }
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(rows.size());
    for (const auto& r : rows) {
        pairs.emplace_back(r.id, r.display);
    }
    merge_http_provider_models("claude-api", pairs);
    settings.verbose_agent_progress = load_settings(paths.settings_path).verbose_agent_progress;
    save_settings(paths.settings_path, settings);
    finalize_model_catalog();
}

void Session::set_default_provider(const std::string& provider) {
    settings.default_provider = coerce_default_provider(provider);
    ensure_selected_model();
    reset_reasoning_effort_for_selected_model();
}

bool Session::is_model_enabled(const std::string& provider_id, const std::string& model_id) const {
    return settings_model_enabled(settings, provider_id, model_id);
}

void Session::set_model_enabled(const std::string& provider_id, const std::string& model_id, bool enabled) {
    settings.model_enabled[model_enable_key(provider_id, model_id)] = enabled;
    if (!enabled && selected_model == model_id && settings.default_provider == provider_id) {
        ensure_selected_model();
    }
    if (!enabled) {
        const auto it = settings.provider_default_model.find(provider_id);
        if (it != settings.provider_default_model.end() && it->second == model_id) {
            settings.provider_default_model.erase(it);
            ensure_selected_model();
        }
    }
}

std::vector<ModelChoice> Session::models_for_provider(const std::string& provider_id,
                                                      bool enabled_only) const {
    std::vector<ModelChoice> out;
    out.reserve(models.size());
    for (const auto& m : models) {
        if (m.provider_id != provider_id) {
            continue;
        }
        if (enabled_only && !is_model_enabled(provider_id, m.id)) {
            continue;
        }
        out.push_back(m);
    }
    return out;
}

std::string Session::provider_default_model_id(const std::string& provider_id) const {
    const auto it = settings.provider_default_model.find(provider_id);
    if (it != settings.provider_default_model.end() && !it->second.empty() &&
        is_model_enabled(provider_id, it->second)) {
        return it->second;
    }
    for (const auto& m : models) {
        if (m.provider_id == provider_id && is_model_enabled(provider_id, m.id)) {
            return m.id;
        }
    }
    return {};
}

void Session::set_provider_default_model(const std::string& provider_id, const std::string& model_id) {
    if (model_id.empty()) {
        settings.provider_default_model.erase(provider_id);
    } else {
        settings.provider_default_model[provider_id] = model_id;
        set_model_enabled(provider_id, model_id, true);
    }
    if (settings.default_provider == provider_id && !model_id.empty()) {
        selected_model = model_id;
        settings.selected_model = model_id;
        reset_reasoning_effort_for_selected_model();
    }
}

void Session::reset_reasoning_effort_for_selected_model() {
    settings.reasoning_effort.clear();
    const auto choices = models_for_provider(settings.default_provider, false);
    const auto model = std::find_if(choices.begin(), choices.end(), [&](const auto& m) {
        return m.id == selected_model;
    });
    if (model == choices.end() || model->reasoning_efforts.empty()) return;
    const auto medium = std::find(model->reasoning_efforts.begin(), model->reasoning_efforts.end(), "medium");
    if (medium != model->reasoning_efforts.end()) settings.reasoning_effort = *medium;
    else if (std::find(model->reasoning_efforts.begin(), model->reasoning_efforts.end(),
        model->default_reasoning_effort) != model->reasoning_efforts.end())
        settings.reasoning_effort = model->default_reasoning_effort;
    else settings.reasoning_effort = model->reasoning_efforts[(model->reasoning_efforts.size() - 1) / 2];
}

void Session::apply_reasoning_effort(Json& turn_params) const {
    if (settings.reasoning_effort.empty()) return;
    const auto choices = models_for_provider(settings.default_provider, false);
    const auto model = std::find_if(choices.begin(), choices.end(), [&](const auto& candidate) {
        return candidate.id == selected_model;
    });
    if (model != choices.end() && std::find(model->reasoning_efforts.begin(),
        model->reasoning_efforts.end(), settings.reasoning_effort) != model->reasoning_efforts.end())
        turn_params["effort"] = Json::string(settings.reasoning_effort);
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
    // Provider blocks: openai, openai-api, claude, claude-api. Within OpenAI, prefer newest family.
    if (provider_id == "claude" || provider_id == "claude-api") {
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
    if (id.rfind("gpt-4", 0) == 0) {
        return 5;
    }
    return 10;
}

void Session::merge_claude_models() {
    models.erase(std::remove_if(models.begin(), models.end(),
                                [](const ModelChoice& m) { return m.provider_id == "claude"; }),
                 models.end());
    if (!claude_account_connected()) {
        return;
    }
    const auto rows = claude_code_list_models(false);
    for (const auto& row : rows) {
        ModelChoice c;
        c.id = row.id;
        c.display = row.display;
        c.provider_id = "claude";
        models.push_back(std::move(c));
    }
}

void Session::merge_http_provider_models(const std::string& provider_id,
                                         const std::vector<std::pair<std::string, std::string>>& rows) {
    models.erase(std::remove_if(models.begin(), models.end(),
                                [&](const ModelChoice& m) { return m.provider_id == provider_id; }),
                 models.end());
    const std::string prefix = provider_id + "/";
    const bool first_setup = std::none_of(settings.model_enabled.begin(), settings.model_enabled.end(),
        [&](const auto& entry) { return entry.first.starts_with(prefix); });
    std::size_t rank = 0;
    for (const auto& row : rows) {
        if (first_setup) settings.model_enabled[prefix + row.first] = rank < 3;
        ++rank;
        ModelChoice c;
        c.id = row.first;
        c.display = row.second.empty() ? row.first : row.second;
        c.provider_id = provider_id;
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
    auto provider_ord = [](const std::string& p) -> int {
        if (p == "openai") {
            return 0;
        }
        if (p == "openai-api") {
            return 1;
        }
        if (p == "claude") {
            return 2;
        }
        if (p == "claude-api") {
            return 3;
        }
        return 9;
    };
    std::stable_sort(models.begin(), models.end(), [&](const ModelChoice& a, const ModelChoice& b) {
        const int pa = provider_ord(a.provider_id);
        const int pb = provider_ord(b.provider_id);
        if (pa != pb) {
            return pa < pb;
        }
        if (is_api_provider(a.provider_id)) return false; // Preserve API discovery order (newest first).
        const int ra = model_sort_rank(a.id, a.provider_id);
        const int rb = model_sort_rank(b.id, b.provider_id);
        if (ra != rb) {
            return ra < rb;
        }
        return a.display < b.display;
    });
    ensure_selected_model();

    // Avoid models_epoch thrash (UI blink) when pagination/sync yields the same set.
    std::string sig;
    sig.reserve(models.size() * 40);
    for (const auto& m : models) {
        sig.append(m.provider_id);
        sig.push_back('\0');
        sig.append(m.id);
        sig.push_back('\0');
        sig.append(m.display);
        sig.push_back('\0');
        sig.append(m.default_reasoning_effort);
        sig.push_back('\0');
        sig.append(m.specialty);
        sig.push_back(m.is_default ? '\1' : '\0');
        for (const auto& effort : m.reasoning_efforts) { sig.append(effort); sig.push_back('\0'); }
        sig.push_back(m.hidden ? '\1' : '\0');
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
    settings.default_provider = coerce_default_provider(settings.default_provider);

    // Drop disconnected buckets back to OpenAI ChatGPT when possible.
    if (settings.default_provider == "claude" && !claude_account_connected()) {
        settings.default_provider = "openai";
    } else if (settings.default_provider == "openai-api" && !openai_api_key_present()) {
        settings.default_provider = "openai";
    } else if (settings.default_provider == "claude-api" && !claude_api_connected()) {
        settings.default_provider = "openai";
    }

    auto best_for = [&](const std::string& provider) -> const ModelChoice* {
        const ModelChoice* exact = nullptr;
        const ModelChoice* preferred = nullptr;
        const ModelChoice* catalog_default = nullptr;
        const ModelChoice* best = nullptr;
        int best_rank = 0x7fffffff;
        const auto pdm = settings.provider_default_model.find(provider);
        const std::string preferred_id =
            pdm != settings.provider_default_model.end() ? pdm->second : std::string{};
        for (const auto& m : models) {
            if (m.provider_id != provider) {
                continue;
            }
            if (!is_model_enabled(provider, m.id)) {
                continue;
            }
            if (!selected_model.empty() && m.id == selected_model) {
                exact = &m;
            }
            if (!preferred_id.empty() && m.id == preferred_id) {
                preferred = &m;
            }
            if (m.is_default && !catalog_default) {
                catalog_default = &m;
            }
            const int rank = model_sort_rank(m.id, m.provider_id);
            if (!best || rank < best_rank) {
                best = &m;
                best_rank = rank;
            }
        }
        if (exact) {
            return exact;
        }
        if (preferred) {
            return preferred;
        }
        if (catalog_default) {
            return catalog_default;
        }
        return best;
    };

    // Respect default_provider — never steal provider from a cross-catalog id match.
    if (const ModelChoice* hit = best_for(settings.default_provider)) {
        selected_model = hit->id;
        settings.selected_model = hit->id;
        return;
    }
    // API providers may legitimately have zero enabled models.
    if (is_api_provider(settings.default_provider)) {
        selected_model.clear();
        settings.selected_model.clear();
        return;
    }
    selected_model.clear();
    settings.selected_model.clear();
}

void Session::ingest_models(const Json& result, bool replace) {
    if (replace) {
        // Replace ChatGPT/Codex rows only — keep Claude + API catalogs intact.
        models.erase(std::remove_if(models.begin(), models.end(),
                                    [](const ModelChoice& m) { return m.provider_id == "openai"; }),
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
            if (!is_supported_gpt_generation(c.id)) {
                continue;
            }
            c.hidden = m.at("hidden").as_bool(false);
            c.specialty = m.at("modelSpecialty").as_string("");
            c.is_default = m.at("isDefault").as_bool(false);
            c.default_reasoning_effort = m.at("defaultReasoningEffort").as_string("");
            const Json& efforts = m.at("supportedReasoningEfforts");
            if (efforts.is_array()) {
                for (const auto& effort : efforts.array_items()) {
                    const std::string value = effort.is_string()
                        ? effort.as_string("") : effort.at("reasoningEffort").as_string("");
                    if (!value.empty() && std::find(c.reasoning_efforts.begin(), c.reasoning_efforts.end(), value) == c.reasoning_efforts.end())
                        c.reasoning_efforts.push_back(value);
                }
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
            // Hidden models are intentionally visible in Manage Models because
            // model/list is requested with includeHidden=true. Keep those and
            // the explicitly supported GPT-4 legacy floor opt-in unless the
            // user has already saved a preference.
            const std::string enabled_key = model_enable_key(c.provider_id, c.id);
            if ((c.hidden || is_gpt4_generation(c.id)) &&
                settings.model_enabled.find(enabled_key) == settings.model_enabled.end()) {
                settings.model_enabled[enabled_key] = false;
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
    if (const auto* project = store.active(); project && project->roots.size() > 1)
        label += L" + " + std::to_wstring(project->roots.size() - 1) + L" repositories";
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
        return scoped_knowledge_context(store, {});
    }
    std::ostringstream oss;
    oss << "Knowledge folders granted for this turn (absolute paths; not limited to the project cwd):\n";
    for (const auto& p : knowledge_accessible_paths_) {
        oss << "- " << utf8(p) << "\n";
    }
    oss << "Under a .md root, handoffs are in handoff/ (singular) and blueprints in blueprints/. "
           "workspace_index.sqlite is a binary SQLite index — do not treat it as empty markdown; "
           "read the .md files under those folders (or use STRATA if available).\n";
    return oss.str() + scoped_knowledge_context(store, knowledge_accessible_paths_);
}

bool Session::sync_lockdown_config() {
    if (paths.codex_home.empty()) {
        paths = make_paths();
    }
    const bool allow_shell = has_project_grant();
    const auto mcp_runtime = collect_mcp_runtime(mcp_manager_, store.active_project_id);
    auto writable_roots = knowledge_accessible_paths_;
    if (const auto* project = store.active()) for (const auto& root : project->roots)
        if (_wcsicmp(root.c_str(), project->root.c_str()) != 0) writable_roots.push_back(root);
    auto runtime_servers = mcp_runtime.servers;
    if (!broker_mcp_server_.name.empty()) runtime_servers.push_back(broker_mcp_server_);
    const bool ok = write_isolated_codex_config(paths, allow_shell, writable_roots, runtime_servers);
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
    if (ok && runtime_.running() && allow_shell != runtime_allow_shell_ && on_line_ &&
        !settings.codex_path.empty()) {
        runtime_.stop();
        runtime_allow_shell_ = false;
        set_state(AppState::Connecting, L"Restarting Codex for project grant…");
        std::wstring err;
        if (!runtime_.start(settings.codex_path, paths.codex_home, paths.workspace, paths.stderr_log, on_line_,
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
    send_repair_fresh_used_ = false;
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

void Session::send_user(const std::string& text, const std::string& mode) {
    log_payload("USER " + settings.default_provider + " " + mode, text);
    if (settings.default_provider == "claude") {
        if (claude_busy_) return;
        activity.begin();
        send_claude_user(text);
        return;
    }
    if (settings.default_provider == "openai-api" || settings.default_provider == "claude-api") {
        if (claude_busy_) return;
        activity.begin();
        send_api_user(settings.default_provider, text);
        return;
    }
    // Stale ChatGPT threads (rollout deleted / different CODEX_HOME) must not resume.
    if (!active_thread_id.empty()) {
        if (auto* c = store.by_thread(active_thread_id);
            c && (!c->resumable || (c->provider_id != "openai" && !c->provider_id.empty() &&
                                    c->provider_id != "chatgpt"))) {
            active_thread_id.clear();
        }
    }
    if (active_thread_id.empty()) {
        if (thread_start_id_ < 0) new_conversation();
        if (thread_start_id_ >= 0) {
            pending_thread_prompts_[thread_start_id_] = text;
            pending_thread_modes_[thread_start_id_] = mode;
            activity.begin();
            activity.phase = "Starting conversation";
        }
        return;
    }
    send_user_to_thread(text, active_thread_id, true, mode, {}, true, SendRepairStep::InitialResume);
}

void Session::fail_send_repair(PendingResumedPrompt prompt, const std::string& error) {
    last_error = error.empty() ? std::string("Turn could not start") : error;
    auto& running = thread_runtime_[prompt.thread_id];
    running.activity.finish(AgentActivity::label("Failed: " + last_error));
    if (prompt.foreground || prompt.thread_id == active_thread_id) {
        activity = running.activity;
        set_state(AppState::Failed, utf16("Turn could not start: " + last_error));
    }
}

void Session::begin_fresh_send(PendingResumedPrompt prompt, const wchar_t* status) {
    if (send_repair_fresh_used_) {
        fail_send_repair(std::move(prompt),
                         last_error.empty() ? "Chat repair already tried a new conversation" : last_error);
        return;
    }
    send_repair_fresh_used_ = true;
    if (auto* c = store.by_thread(prompt.thread_id)) {
        c->resumable = false;
        store.save(paths.store_path);
    }
    if (active_thread_id == prompt.thread_id) active_thread_id.clear();
    if (thread_start_id_ >= 0) {
        // A start is already in flight — queue onto it (still only one fresh attempt).
        pending_thread_prompts_[thread_start_id_] = prompt.text;
        pending_thread_modes_[thread_start_id_] = prompt.mode;
        activity.begin();
        activity.phase = "Starting conversation";
        last_error.clear();
        set_state(AppState::Generating, status ? status : L"Starting a new conversation…");
        return;
    }
    // Do not clear send_repair_fresh_used_ here — new_conversation() would reset it.
    activity = {};
    active_thread_id.clear();
    active_turn_id.clear();
    stream_buffer.clear();
    history_messages.clear();
    if (account.signed_in) set_state(AppState::Ready, L"Ready");
    Json p = Json::object();
    if (!selected_model.empty()) p["model"] = Json::string(selected_model);
    const std::wstring cwd = conversation_cwd();
    p["cwd"] = Json::string(utf8(cwd));
    p["approvalPolicy"] = Json::string("on-request");
    p["sandbox"] = Json::string(has_project_grant() ? "workspace-write" : "read-only");
    p["serviceName"] = Json::string("scylla_gpt");
    thread_start_id_ = send_req("thread/start", std::move(p));
    if (thread_start_id_ < 0) {
        fail_send_repair(std::move(prompt), "Could not start a replacement conversation");
        return;
    }
    pending_thread_prompts_[thread_start_id_] = prompt.text;
    pending_thread_modes_[thread_start_id_] = prompt.mode;
    activity.begin();
    activity.phase = "Starting conversation";
    last_error.clear();
    set_state(AppState::Generating, status ? status : L"Prior chat unavailable — starting a new one…");
}

void Session::advance_send_repair(PendingResumedPrompt prompt) {
    if (prompt.repair_step == SendRepairStep::FreshThread) {
        fail_send_repair(std::move(prompt), last_error);
        return;
    }
    last_error.clear();
    if (prompt.repair_step == SendRepairStep::InitialResume) {
        prompt.repair_step = SendRepairStep::AfterListLookup;
        activity.phase = "Looking up conversation";
        set_state(AppState::Generating, L"Looking up conversation…");
        Json p = Json::object();
        p["limit"] = Json::number(100);
        repair_list_id_ = send_req("thread/list", std::move(p));
        if (repair_list_id_ < 0) {
            prompt.repair_step = SendRepairStep::AfterListLookup;
        } else {
            pending_repair_prompts_[repair_list_id_] = std::move(prompt);
            return;
        }
    }

    if (prompt.repair_step == SendRepairStep::AfterListLookup) {
        const std::wstring found = find_rollout_file(paths.codex_home, prompt.thread_id);
        if (!found.empty()) {
            activity.phase = "Reloading conversation";
            set_state(AppState::Generating, L"Reloading conversation from disk…");
            send_user_to_thread(prompt.text, prompt.thread_id, prompt.foreground, prompt.mode, utf8(found),
                               !prompt.recorded_local, SendRepairStep::AfterPathResume, false);
            return;
        }
        begin_fresh_send(std::move(prompt), L"Conversation not in Codex — starting a new one…");
        return;
    }

    // AfterPathResume (or unknown): one fresh start max, then stop.
    begin_fresh_send(std::move(prompt), L"Prior chat unavailable — starting a new one…");
}

void Session::send_user_to_thread(const std::string& text, const std::string& thread_id, bool foreground,
                                  const std::string& mode, const std::string& resume_path, bool record_user,
                                  SendRepairStep repair_step, bool skip_resume) {
    thread_modes_[thread_id] = mode;
    final_plan_text_.erase(thread_id);
    std::string payload = text;
    auto* naming_chat = store.by_thread(thread_id);
    if (naming_chat) {
        naming_chat->updated_at = std::time(nullptr);
        if (!naming_chat->title_manual && !naming_chat->title_generated &&
            (naming_chat->title.empty() || naming_chat->title == "New Chat")) {
            const std::string seed = chat_title_seed.empty() ? text : chat_title_seed;
            naming_chat->title = short_chat_title(seed);
        }
        store.save(paths.store_path);
    }
    const std::string grant = knowledge_grant_preamble() + chat_review_request() + chat_title_request(naming_chat);
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
    payload = std::string(chat_mode_instructions(mode)) + "\n" + payload;
    Json input = Json::array();
    Json item = Json::object();
    item["type"] = Json::string("text");
    item["text"] = Json::string(payload);
    input.push(std::move(item));
    for (const auto& attachment : parse_user_display(text).attachments) {
        if (!attachment.image || attachment.path.empty()) continue;
        Json image = Json::object();
        image["type"] = Json::string("localImage");
        image["path"] = Json::string(attachment.path);
        input.push(std::move(image));
    }
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
    apply_chat_mode_policy(p, mode);
    if (!selected_model.empty()) {
        p["model"] = Json::string(selected_model);
    }
    apply_reasoning_effort(p);

    PendingResumedPrompt pending{text, mode, thread_id, foreground, repair_step, true};
    auto& running = thread_runtime_[thread_id];
    running.activity.begin();
    running.stream.clear();

    if (skip_resume || repair_step == SendRepairStep::FreshThread) {
        // thread/start already loaded this thread in Codex — resume would race/fail and loop.
        pending.repair_step = SendRepairStep::FreshThread;
        running.activity.phase = "Generating";
        turn_start_id_ = send_req("turn/start", std::move(p));
        if (turn_start_id_ < 0) {
            fail_send_repair(std::move(pending), "Runtime connection closed");
            return;
        }
        turn_start_threads_[turn_start_id_] = thread_id;
        pending_resumed_prompts_[turn_start_id_] = pending;
    } else {
        Json resume = Json::object();
        resume["threadId"] = Json::string(thread_id);
        if (!resume_path.empty()) resume["path"] = Json::string(resume_path);
        turn_start_id_ = send_req("thread/resume", std::move(resume));
        if (turn_start_id_ < 0) return;
        pending_resumed_turns_[turn_start_id_] = std::move(p);
        pending_resumed_prompts_[turn_start_id_] = pending;
        turn_start_threads_[turn_start_id_] = thread_id;
        running.activity.phase = resume_path.empty() ? "Resolving conversation" : "Reloading conversation";
    }

    if (record_user && naming_chat) {
        if (!naming_chat->local_messages.is_array()) naming_chat->local_messages = Json::array();
        Json message = Json::object();
        message["user"] = Json::boolean(true);
        message["text"] = Json::string(text);
        naming_chat->local_messages.push(std::move(message));
        store.save(paths.store_path);
    }
    if (foreground) {
        activity = running.activity;
        if (record_user) history_messages.push_back({true, text});
        stream_buffer.clear();
        set_state(AppState::Generating,
                  skip_resume || repair_step == SendRepairStep::FreshThread
                      ? L"Generating"
                      : (resume_path.empty() ? L"Resolving conversation…" : L"Reloading conversation…"));
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

void Session::ensure_api_thread(const std::string& provider_id, const char* backend) {
    if (!active_thread_id.empty()) {
        if (auto* c = store.by_thread(active_thread_id)) {
            if (c->provider_id == provider_id) {
                return;
            }
        }
    }
    const std::string tid = provider_id + "-" + make_uuid();
    active_thread_id = tid;
    settings.last_thread_id = tid;
    if (auto* pr = store.active()) {
        pr->last_thread_id = tid;
    }
    auto* conv = store.upsert_thread(store.active_project_id, account_scope(), tid, "New Chat", "");
    if (conv) {
        conv->provider_id = provider_id;
        conv->backend = backend ? backend : "http-chat";
        conv->resumable = false;
    }
    store.save(paths.store_path);
}

void Session::send_claude_user(const std::string& text) {
    if (!claude_account_connected()) {
        last_error = "Claude Account is not connected";
        set_state(AppState::Failed, L"Claude Account not connected");
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
    const std::string grant = knowledge_grant_preamble() + chat_review_request() + chat_title_request(naming_chat);
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
            const auto& message = history_messages[i];
            oss << (message.user ? "User: " : "Assistant: ")
                << (message.user ? visible_user_text(message.text) : visible_chat_text(message.text)) << "\n";
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
    print_thread_id_ = active_thread_id;
    thread_runtime_[print_thread_id_].activity = activity;

    const std::string model = selected_model.empty() ? "claude-sonnet-4-6" : selected_model;
    const std::wstring cwd = conversation_cwd();
    auto done = on_claude_done_;
    std::thread([this, payload, model, cwd, done]() {
        std::string result;
        std::wstring err;
        const bool ok = !claude_cancel_ && claude_code_print(payload, model, cwd, &result, &err, 300000);
        ClaudePrintResult r{};
        if (claude_cancel_) {
            r.ok = false;
            r.error = L"Cancelled";
        } else {
            r.ok = ok;
            r.text = std::move(result);
            r.error = std::move(err);
        }
        if (done) {
            done(std::move(r));
        }
    }).detach();
}

void Session::send_api_user(const std::string& provider_id, const std::string& text) {
    const bool openai = provider_id == "openai-api";
    if (openai && !openai_api_key_present()) {
        last_error = "OpenAI API key is not configured";
        set_state(AppState::Failed, L"OpenAI API not connected");
        return;
    }
    if (!openai && !claude_api_connected()) {
        last_error = "Claude API key is not configured";
        set_state(AppState::Failed, L"Claude API not connected");
        return;
    }
    if (selected_model.empty() || !is_model_enabled(provider_id, selected_model)) {
        last_error = "Enable at least one model for this API provider";
        set_state(AppState::Failed, L"No API model enabled");
        return;
    }
    if (claude_busy_) {
        return;
    }
    ensure_api_thread(provider_id, openai ? "openai-api-http" : "claude-api-http");

    auto* naming_chat = store.by_thread(active_thread_id);
    if (naming_chat) {
        naming_chat->updated_at = std::time(nullptr);
        if (!naming_chat->title_manual && !naming_chat->title_generated &&
            (naming_chat->title.empty() || naming_chat->title == "New Chat"))
            naming_chat->title = short_chat_title(chat_title_seed);
        store.save(paths.store_path);
    }
    std::string payload = text;
    const std::string grant = knowledge_grant_preamble() + chat_review_request() + chat_title_request(naming_chat);
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

    std::vector<HttpChatMessage> msgs;
    const std::size_t start = history_messages.size() > 12 ? history_messages.size() - 12 : 0;
    for (std::size_t i = start; i < history_messages.size(); ++i) {
        const auto& message = history_messages[i];
        msgs.push_back({message.user, message.user ? visible_user_text(message.text) : visible_chat_text(message.text)});
    }
    msgs.push_back({true, payload});

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
    set_state(AppState::Generating, openai ? L"OpenAI API generating…" : L"Claude API generating…");
    print_thread_id_ = active_thread_id;
    thread_runtime_[print_thread_id_].activity = activity;

    const std::string model = selected_model;
    auto done = on_claude_done_;
    std::thread([this, openai, msgs, model, done]() {
        std::string result;
        std::wstring err;
        bool ok = false;
        if (claude_cancel_) {
            err = L"Cancelled";
        } else if (openai) {
            ok = openai_api_chat(model, msgs, &result, &err, 300000);
        } else {
            ok = claude_api_chat(model, msgs, &result, &err, 300000);
        }
        ClaudePrintResult r{};
        if (claude_cancel_) {
            r.ok = false;
            r.error = L"Cancelled";
        } else {
            r.ok = ok;
            r.text = std::move(result);
            r.error = std::move(err);
        }
        if (done) {
            done(std::move(r));
        }
    }).detach();
}

void Session::complete_claude_print(bool ok, const std::string& text, const std::wstring& error) {
    log_payload(ok ? "REPLY" : "ERROR", ok ? text : utf8(error));
    const auto completed_id = print_thread_id_;
    print_thread_id_.clear();
    claude_busy_ = false;
    const std::string phase = claude_cancel_ ? "Interrupted" : ok ? "Completed" : "Failed";
    auto& completed = thread_runtime_[completed_id];
    completed.activity.finish(phase);
    if (ok && !claude_cancel_) completed.stream = text;
    if (completed_id != active_thread_id) {
        if (ok && !claude_cancel_) {
            completed.stream = text;
            if (auto* conv = store.by_thread(completed_id)) {
                Json item = Json::object();
                item["user"] = Json::boolean(false);
                item["text"] = Json::string(text);
                conv->local_messages.push(std::move(item));
                conv->updated_at = std::time(nullptr);
                accept_chat_title(*conv, text);
                conv->preview = text.substr(0, 120);
                store.save(paths.store_path);
            }
        }
        return;
    }
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

bool Session::delete_thread(const std::string& id, std::string* error) {
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (id.empty()) return fail("No chat selected.");
    if (thread_busy(id) || (id == active_thread_id && (claude_busy_ || state == AppState::Generating ||
        state == AppState::AwaitingAction))) return fail("Stop this chat before deleting it.");
    auto next = store;
    auto* row = next.by_thread(id);
    if (!row) {
        const auto remote = std::find_if(threads.begin(), threads.end(), [&](const auto& t) { return t.id == id; });
        if (remote == threads.end()) return fail("Chat no longer exists.");
        row = next.upsert_thread(next.active_project_id, account_scope(), id, "", "");
    }
    row->deleted = true;
    row->title.clear();
    row->preview.clear();
    row->local_messages = Json::array();
    row->resumable = false;
    for (auto& project : next.projects) if (project.last_thread_id == id) project.last_thread_id.clear();
    if (!next.save(paths.store_path)) return fail("Could not save chat deletion.");
    store = std::move(next);
    threads.erase(std::remove_if(threads.begin(), threads.end(), [&](const auto& t) { return t.id == id; }), threads.end());
    thread_runtime_.erase(id);
    if (settings.last_thread_id == id) settings.last_thread_id.clear();
    set_draft(id, "");
    if (active_thread_id == id) {
        active_thread_id.clear(); active_turn_id.clear(); history_messages.clear(); stream_buffer.clear();
        activity = {}; transcript_replace = true; last_plan_path.clear();
        set_state(AppState::Ready, L"Ready");
    }
    ++chats_epoch;
    return true;
}
void Session::open_thread(const std::string& id) {
    if (const auto* row = store.by_thread(id); row && row->deleted) return;
    active_thread_id = id;
    select_thread_runtime(id);
    settings.last_thread_id = id;
    if (auto* c = store.by_thread(id)) {
        // Resuming a peer-root chat must not replace the open workspace.
        // conversation_cwd() already resolves this conversation's own root.
        history_messages.clear();
        if (c->local_messages.is_array()) for (const auto& message : c->local_messages.array_items())
            history_messages.push_back({message.at("user").as_bool(false), message.at("text").as_string()});
        transcript_replace = true;
    }
    if (auto* c = store.by_thread(id); c && (c->provider_id == "claude" || c->provider_id == "claude-api" ||
                                             c->provider_id == "openai-api")) {
        settings.default_provider = coerce_default_provider(c->provider_id);
        history_messages.clear();
        if (c->local_messages.is_array()) for (const auto& message : c->local_messages.array_items())
            history_messages.push_back({message.at("user").as_bool(false), message.at("text").as_string()});
        stream_buffer.clear();
        transcript_replace = true;
        return;
    }
    // Local-only ChatGPT rows (missing Codex rollout) stay viewable without resume.
    if (auto* c = store.by_thread(id); c && !c->resumable) {
        settings.default_provider = "openai";
        stream_buffer.clear();
        set_state(AppState::Ready, L"Ready — prior Codex session unavailable");
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

void Session::interrupt_thread_turn(const std::string& thread_id, const std::string& turn_id) {
    if (thread_id.empty() || turn_id.empty()) {
        return;
    }
    Json p = Json::object();
    p["threadId"] = Json::string(thread_id);
    p["turnId"] = Json::string(turn_id);
    turn_interrupt_id_ = send_req("turn/interrupt", std::move(p));
    auto& running = thread_runtime_[thread_id];
    running.turn_id = turn_id;
    turn_threads_[turn_id] = thread_id;
    if (thread_id == active_thread_id) {
        active_turn_id = turn_id;
        if (activity.busy) {
            activity.phase = "Cancelling";
        }
        set_state(AppState::Interrupted, L"Cancelling stuck turn…");
    }
}

void Session::clear_orphaned_in_progress_turn(const std::string& thread_id, const Json& thread) {
    const std::string orphan = latest_in_progress_turn_id(thread);
    if (orphan.empty()) {
        return;
    }
    auto& running = thread_runtime_[thread_id];
    running.turn_id = orphan;
    turn_threads_[orphan] = thread_id;
    // Live turns keep activity.busy true from send_user_to_thread. After a Scylla relaunch the
    // map is empty so busy is false while Codex still lists inProgress — that blocks turn/start.
    if (running.activity.busy) {
        if (thread_id == active_thread_id) {
            active_turn_id = orphan;
            activity = running.activity;
            set_state(AppState::Generating, L"Generating");
        }
        return;
    }
    last_error =
        "Cleared a stuck prior turn left by a previous Scylla session. Send your message again.";
    interrupt_thread_turn(thread_id, orphan);
}

void Session::cancel_turn() {
    for (auto pending = pending_resumed_turns_.begin(); pending != pending_resumed_turns_.end(); ++pending) {
        if (pending->second.at("threadId").as_string() != active_thread_id) continue;
        turn_start_threads_.erase(pending->first);
        pending_resumed_prompts_.erase(pending->first);
        pending_resumed_turns_.erase(pending);
        auto& waiting = thread_runtime_[active_thread_id];
        waiting.activity.finish("Interrupted");
        activity = waiting.activity;
        set_state(AppState::Interrupted, L"Interrupted");
        return;
    }
    if (activity.busy) activity.phase = "Cancelling";
    if (claude_busy_ && print_thread_id_ == active_thread_id) {
        claude_cancel_ = true;
        set_state(AppState::Interrupted, L"Cancelling Claude…");
        return;
    }
    const auto running = thread_runtime_.find(active_thread_id);
    std::string turn_id;
    if (running != thread_runtime_.end()) {
        turn_id = running->second.turn_id;
    }
    if (turn_id.empty()) {
        turn_id = active_turn_id;
    }
    if (active_thread_id.empty() || turn_id.empty()) {
        last_error = "No active turn to cancel";
        set_state(AppState::Ready, L"Nothing to cancel");
        return;
    }
    interrupt_thread_turn(active_thread_id, turn_id);
}

bool Session::thread_busy(const std::string& thread_id) const {
    const auto found = thread_runtime_.find(thread_id);
    return found != thread_runtime_.end() && found->second.activity.busy;
}

const AgentActivity* Session::thread_activity(const std::string& thread_id) const {
    if (thread_id == active_thread_id && activity.visible) return &activity;
    const auto found = thread_runtime_.find(thread_id);
    return found == thread_runtime_.end() ? nullptr : &found->second.activity;
}

void Session::reconcile_idle_activity() {
    if (state == AppState::Generating || state == AppState::AwaitingAction || claude_busy_) return;
    if (!pending_resumed_turns_.empty()) return;
    auto waiting_turn_start = [&](const std::string& tid) {
        for (const auto& [req, waiting] : turn_start_threads_) {
            if (waiting == tid) return true;
        }
        return false;
    };
    for (auto& [tid, rt] : thread_runtime_) {
        if (!rt.activity.busy) continue;
        if (!rt.turn_id.empty() || waiting_turn_start(tid)) continue;
        rt.activity.finish("Completed");
    }
    if (activity.busy && active_turn_id.empty()) activity.finish("Completed");
}

void Session::select_thread_runtime(const std::string& thread_id) {
    const auto* chat = store.by_thread(thread_id);
    const bool local_provider = chat && (chat->provider_id == "claude" || chat->provider_id == "claude-api"
        || chat->provider_id == "openai-api");
    const auto found = thread_runtime_.find(thread_id);
    if (found == thread_runtime_.end()) {
        activity = {};
        active_turn_id.clear();
        stream_buffer.clear();
        if (account.signed_in || local_provider) set_state(AppState::Ready, L"Ready");
        return;
    }
    activity = found->second.activity;
    active_turn_id = found->second.turn_id;
    stream_buffer = found->second.stream;
    if (account.signed_in || local_provider) set_state(activity.busy ? AppState::Generating : AppState::Ready,
                                     activity.busy ? L"Generating" : L"Ready");
}

void Session::refresh_threads(const std::string& cursor) {
    if (cursor.empty()) threads.clear();
    Json p = Json::object();
    p["limit"] = Json::number(100);
    if (!cursor.empty()) p["cursor"] = Json::string(cursor);
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
            if (!local.empty()) {
                history_messages = merge_provider_agent_messages(local, history_messages);
            }
        }
    }
    save_local_history(store, paths.store_path, active_thread_id, history_messages);
    if (const auto live = thread_runtime_.find(thread_id); live != thread_runtime_.end()) {
        stream_buffer = live->second.stream;
    } else {
        stream_buffer.clear();
    }
    transcript_replace = true;
    clear_orphaned_in_progress_turn(thread_id, thread);
}

void Session::handle_server_request(const Json& msg) {
    const std::string method = msg.at("method").as_string();
    const Json& id = msg.at("id");
    const std::string request_thread = msg.at("params").at("threadId").as_string();
    const bool foreground = request_thread.empty() || request_thread == active_thread_id;
    const auto mode = thread_modes_.find(request_thread.empty() ? active_thread_id : request_thread);
    const bool read_only_mode = mode != thread_modes_.end() && mode->second != "execute";
    if (foreground) set_state(AppState::AwaitingAction, utf16("Approval: " + method));

    // Under a project grant: accept file/patch and sandboxed shell so the agent can
    // browse, search, and edit inside writableRoots. Apps/hooks/network stay off.
    if (method == "item/fileChange/requestApproval" || method == "applyPatchApproval" ||
        method == "item/commandExecution/requestApproval" || method == "execCommandApproval") {
        Json result = Json::object();
        if (has_project_grant() && !read_only_mode) {
            result["decision"] = Json::string("accept");
            send_result(id, std::move(result));
            if (foreground) {
                set_state(AppState::Generating, utf16("Approved: " + method));
                last_error.clear();
            }
        } else {
            result["decision"] = Json::string("decline");
            send_result(id, std::move(result));
            if (foreground) last_error = "Declined " + method + " (agent is read-only).";
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
    if (method == "mcpServer/elicitation/request") {
        const std::string server_name = msg.at("params").at("serverName").as_string();
        Json result = Json::object();
        // The query MCP is an in-process Scylla capability backed by a per-session authenticated
        // named pipe. Let Codex invoke it; ConnectionBroker remains the authority for SQL class,
        // saved-alias availability, Keyring authorization, and any write approval. Never extend
        // this acceptance to an arbitrary MCP server.
        const bool internal_query_broker = !read_only_mode && broker_mcp_server_registered() &&
                                           server_name == broker_mcp_server_.name &&
                                           server_name == "scylla-query";
        result["action"] = Json::string(internal_query_broker ? "accept" : "decline");
        result["content"] = Json::null();
        send_result(id, std::move(result));
        if (foreground && internal_query_broker) {
            set_state(AppState::Generating, L"Running Scylla broker query");
            last_error.clear();
        }
        return;
    }
    if (method == "item/tool/requestUserInput") {
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
    log_payload("OUT", msg_err.dump());
    runtime_.write_line(msg_err.dump());
}

void Session::save_knowledge_plan(const std::string& thread_id, const std::string& text) {
    const auto* chat = store.by_thread(thread_id);
    if (!chat) { last_error = "Cannot save plan: conversation has no project."; return; }
    try {
        const auto project_key = std::to_wstring(fnv1a64(chat->project_id.data(), chat->project_id.size()));
        const auto folder = join_path(join_path(paths.appdata, L"knowledge-plans"), project_key);
        std::filesystem::create_directories(folder);
        const auto file = join_path(folder, L"plan-" + std::to_wstring(std::time(nullptr)) + L"-" + std::to_wstring(GetTickCount64()) + L".md");
        if (!write_file_bytes_atomic(file, visible_chat_text(text))) {
            last_error = "Could not save Knowledge plan."; return;
        }
        KnowledgeStore knowledge;
        knowledge.load(paths.knowledge_path);
        if (!knowledge.source_for_path(file, chat->project_id)) {
            if (!knowledge.add_source(L"Project plans", folder, SourceType::Knowledge, AccessMode::ReadOnly,
                                      true, {chat->project_id}) || !knowledge.save(paths.knowledge_path)) {
                last_error = "Plan saved, but Knowledge registration failed: " + utf8(file); return;
            }
        }
        last_plan_path = utf8(file);
    } catch (const std::exception& error) { last_error = "Could not save Knowledge plan: " + std::string(error.what()); }
}

void Session::handle_notification(const Json& msg) {
    const std::string method = msg.at("method").as_string();
    const Json& p = msg.at("params");
    if (method == "scylla/runtimeClosed") {
        if (p.at("pid").as_int() == runtime_.pid()) {
            for (auto& [id, turn] : thread_runtime_) {
                if (id != print_thread_id_ && turn.activity.busy) {
                    turn.activity.finish("Disconnected");
                    turn.turn_id.clear();
                }
            }
            if (settings.default_provider != "claude" && active_thread_id != print_thread_id_)
                set_state(AppState::Failed, L"Runtime disconnected");
        }
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
        account_loaded = true;
        account.type = p.at("authMode").as_string("");
        account.plan = p.at("planType").as_string("");
        account.signed_in = account.type == "chatgpt";
        return;
    }
    if (method == "item/completed" && p.at("item").at("type").as_string() == "agentMessage") {
        if (!thread_id.empty() && thread_modes_[thread_id] == "plan"
            && p.at("item").at("phase").as_string() != "commentary")
            final_plan_text_[thread_id] = item_text(p.at("item"));
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
        if (st == "completed" && thread_modes_[thread_id] == "plan") {
            const auto final = final_plan_text_.find(thread_id);
            if (final != final_plan_text_.end() && !final->second.empty())
                save_knowledge_plan(thread_id, final->second);
            else last_error = "The plan has no final response to save. Ask for a complete final plan.";
        }
        final_plan_text_.erase(thread_id);
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
        PendingResumedPrompt recovered_prompt;
        const bool had_pending_prompt = [&] {
            const auto it = pending_resumed_prompts_.find(id);
            if (it == pending_resumed_prompts_.end()) return false;
            recovered_prompt = it->second;
            pending_resumed_prompts_.erase(it);
            return true;
        }();
        pending_resumed_turns_.erase(id);
        last_error = msg.at("error").at("message").as_string("request error");
        const bool missing_thread = is_thread_missing_error(last_error);

        if (missing_thread && had_pending_prompt) {
            turn_start_threads_.erase(id);
            if (id == turn_start_id_) turn_start_id_ = 0;
            if (recovered_prompt.repair_step == SendRepairStep::FreshThread) {
                fail_send_repair(std::move(recovered_prompt), last_error);
                return;
            }
            advance_send_repair(std::move(recovered_prompt));
            return;
        }
        if (had_pending_prompt && recovered_prompt.repair_step == SendRepairStep::FreshThread) {
            turn_start_threads_.erase(id);
            if (id == turn_start_id_) turn_start_id_ = 0;
            fail_send_repair(std::move(recovered_prompt), last_error);
            return;
        }

        if (const auto repair = pending_repair_prompts_.find(id); repair != pending_repair_prompts_.end()) {
            PendingResumedPrompt prompt = repair->second;
            pending_repair_prompts_.erase(repair);
            if (id == repair_list_id_) repair_list_id_ = 0;
            // List failed — try disk path, then fresh chat.
            prompt.repair_step = SendRepairStep::AfterListLookup;
            advance_send_repair(std::move(prompt));
            return;
        }

        if (const auto pending = pending_thread_prompts_.find(id); pending != pending_thread_prompts_.end()) {
            pending_thread_prompts_.erase(pending);
            if (id == thread_start_id_) thread_start_id_ = -1;
        }
        if (const auto started = turn_start_threads_.find(id); started != turn_start_threads_.end()) {
            const std::string thread_id = started->second;
            auto& running = thread_runtime_[thread_id];
            const std::string fail_label =
                last_error.empty() ? std::string("Failed") : AgentActivity::label("Failed: " + last_error);
            running.activity.finish(fail_label);
            if (thread_id == active_thread_id) activity = running.activity;
            turn_start_threads_.erase(started);
            // Common after relaunch: Codex still holds an inProgress turn we no longer track.
            if (!running.turn_id.empty()) {
                interrupt_thread_turn(thread_id, running.turn_id);
            }
        }
        if (missing_thread && id == thread_resume_id_) {
            if (auto* c = store.by_thread(active_thread_id)) {
                c->resumable = false;
                store.save(paths.store_path);
            }
            active_thread_id.clear();
            last_error = "Prior Codex session is gone. Send again to start a fresh conversation.";
            set_state(AppState::Ready, L"Ready — prior chat unavailable");
            return;
        }
        if (id == initialize_id_) {
            set_state(AppState::Failed, L"Initialize failed");
        } else if (id == turn_start_id_ || id == thread_start_id_) {
            set_state(AppState::Failed,
                      last_error.empty() ? L"Turn could not start" : utf16("Turn could not start: " + last_error));
        } else if (id == turn_interrupt_id_) {
            set_state(AppState::Generating, L"Cancellation failed — still working");
        }
        return;
    }
    const Json& result = msg.at("result");
    if (auto pending = pending_resumed_turns_.find(id); pending != pending_resumed_turns_.end()) {
        Json params = std::move(pending->second);
        pending_resumed_turns_.erase(pending);
        pending_resumed_prompts_.erase(id);
        const std::string thread_id = params.at("threadId").as_string();
        turn_start_threads_.erase(id);
        const auto request = send_req("turn/start", std::move(params));
        if (request >= 0) {
            turn_start_threads_[request] = thread_id;
            if (thread_id == active_thread_id) turn_start_id_ = request;
            auto& running = thread_runtime_[thread_id];
            running.activity.phase = "Generating";
            if (thread_id == active_thread_id) {
                activity = running.activity;
                set_state(AppState::Generating, L"Generating");
            }
        } else {
            auto& running = thread_runtime_[thread_id];
            running.activity.finish("Failed: runtime connection closed");
            if (thread_id == active_thread_id) {
                activity = running.activity;
                set_state(AppState::Failed, L"Runtime connection closed");
            }
        }
        return;
    }
    if (id == turn_interrupt_id_) {
        turn_interrupt_id_ = 0;
        if (!active_thread_id.empty()) {
            auto& running = thread_runtime_[active_thread_id];
            if (!running.turn_id.empty()) {
                turn_threads_.erase(running.turn_id);
                running.turn_id.clear();
            }
            running.activity.finish("Interrupted");
            activity = running.activity;
        }
        active_turn_id.clear();
        last_error.clear();
        set_state(AppState::Ready, L"Ready — stuck turn cleared");
        return;
    }
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
        account_loaded = true;
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
    if (id == thread_list_id_ || id == repair_list_id_) {
        const bool repair_list = id == repair_list_id_;
        if (id == thread_list_id_) thread_list_id_ = 0;
        if (id == repair_list_id_) repair_list_id_ = 0;

        const Json& data = result.at("data");
        std::string listed_path;
        bool listed_match = false;
        if (data.is_array()) {
            for (const auto& t : data.array_items()) {
                ThreadSummary s;
                s.id = t.at("id").as_string();
                s.name = thread_label(t);
                s.preview = t.at("preview").as_string("");
                s.cwd = utf16(t.at("cwd").as_string(""));
                const auto* stored = store.by_thread(s.id);
                const bool in_scope = stored ? store.contains_open_project(stored->project_id) : store.contains_open_path(s.cwd);
                if (auto* c = store.by_thread(s.id); c && in_scope) {
                    c->updated_at = (std::max)(c->updated_at, t.at("updatedAt").as_int(0));
                }
                if (!repair_list && !s.id.empty() && in_scope) {
                    threads.push_back(s);
                }
                if (repair_list) {
                    const auto pit = pending_repair_prompts_.find(id);
                    if (pit != pending_repair_prompts_.end() && s.id == pit->second.thread_id) {
                        listed_match = true;
                        listed_path = thread_path_field(t);
                    }
                }
            }
        }
        website_history_seen = false;  // Codex rollouts only unless a documented web id appears
        store.save(paths.store_path);
        ++chats_epoch;
        if (!repair_list) {
            const auto next = result.at("nextCursor").as_string("");
            if (!next.empty() && next != "null") refresh_threads(next);
        }

        if (repair_list) {
            const auto pit = pending_repair_prompts_.find(id);
            if (pit == pending_repair_prompts_.end()) return;
            PendingResumedPrompt prompt = pit->second;
            pending_repair_prompts_.erase(pit);
            if (listed_match) {
                activity.phase = "Reloading conversation";
                set_state(AppState::Generating, L"Found conversation — reloading…");
                send_user_to_thread(prompt.text, prompt.thread_id, prompt.foreground, prompt.mode, listed_path,
                                   !prompt.recorded_local, SendRepairStep::AfterListLookup, false);
                return;
            }
            // Not in Codex list — try local rollout file, else start fresh.
            prompt.repair_step = SendRepairStep::AfterListLookup;
            advance_send_repair(std::move(prompt));
        }
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
        const auto pending_mode = pending_thread_modes_.find(id);
        const std::string mode = pending_mode == pending_thread_modes_.end() ? "execute" : pending_mode->second;
        pending_thread_modes_.erase(id);
        if (!pending.empty()) {
            // Fresh Codex thread is already loaded — do not resume (that caused the chat-spam loop).
            send_user_to_thread(pending, created_thread_id, foreground, mode, {}, true,
                               SendRepairStep::FreshThread, true);
        }
        refresh_threads();
        return;
    }
    if (id == thread_resume_id_) {
        const Json& thread = result.at("thread");
        const std::string resumed_id = thread.at("id").as_string();
        if (resumed_id == active_thread_id)
            clear_orphaned_in_progress_turn(resumed_id, thread);
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
        pending_resumed_prompts_.erase(id);
        auto& running = thread_runtime_[thread_id];
        running.turn_id = result.at("turn").at("id").as_string();
        if (!running.turn_id.empty()) turn_threads_[running.turn_id] = thread_id;
        if (thread_id == active_thread_id) active_turn_id = running.turn_id;
        send_repair_fresh_used_ = false;
        return;
    }
}

void Session::handle_line(const std::string& line) {
    log_payload("IN", line);
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
