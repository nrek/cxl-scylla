#include "scylla_core_c.h"

#include "scyllagpt/chat_history.h"
#include "scyllagpt/chat_projects.h"
#include "scyllagpt/chat_mode.h"
#include "scyllagpt/document.h"
#include "scyllagpt/editor_host.h"
#include "scyllagpt/event_sink.h"
#include "scyllagpt/json.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/project_connection.h"
#include "scyllagpt/broker_service.h"
#include "scyllagpt/ssh_query_executor.h"
#include "scyllagpt/markdown.h"
#include "scyllagpt/mcp_manager.h"
#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/agent_files.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/session.h"
#include "scyllagpt/settings.h"
#include "scyllagpt/single_instance.h"
#include "scyllagpt/store.h"
#include "scyllagpt/project_context.h"
#include "scyllagpt/strata_bridge.h"
#include "scyllagpt/strata_client.h"
#include "scyllagpt/terminal_host.h"
#include "scyllagpt/terminal_profiles.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <cstring>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

scyllagpt::InstanceLock g_instance{};
bool g_editor_registered = false;
std::mutex g_security_mutex;
scyllagpt::Keyring g_app_keyring;

int write_utf8(char* buf, int buf_len, const std::string& s) {
    if (!buf || buf_len <= 0) return -1;
    if (static_cast<int>(s.size()) + 1 > buf_len) return -1;
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    return static_cast<int>(s.size());
}

const char* app_state_name(scyllagpt::AppState s) {
    using scyllagpt::AppState;
    switch (s) {
        case AppState::Offline: return "offline";
        case AppState::Connecting: return "connecting";
        case AppState::SigningIn: return "signing_in";
        case AppState::Ready: return "ready";
        case AppState::Generating: return "generating";
        case AppState::AwaitingAction: return "awaiting_action";
        case AppState::Interrupted: return "interrupted";
        case AppState::Failed: return "failed";
        default: return "unknown";
    }
}

scyllagpt::Json settings_to_json(const scyllagpt::Settings& s, const scyllagpt::Paths& paths) {
    scyllagpt::Json j = scyllagpt::Json::object();
    j["codex_path"] = scyllagpt::Json::string(scyllagpt::utf8(s.codex_path));
    j["default_provider"] = scyllagpt::Json::string(s.default_provider);
    j["selected_model"] = scyllagpt::Json::string(s.selected_model);
    j["reasoning_effort"] = scyllagpt::Json::string(s.reasoning_effort);
    j["enter_sends"] = scyllagpt::Json::boolean(s.enter_sends);
    j["verbose_agent_progress"] = scyllagpt::Json::boolean(s.verbose_agent_progress);
    j["restore_chat_on_start"] = scyllagpt::Json::boolean(s.restore_chat_on_start);
    j["project_folder"] = scyllagpt::Json::string(scyllagpt::utf8(s.project_folder));
    j["last_thread_id"] = scyllagpt::Json::string(s.last_thread_id);
    j["files_w"] = scyllagpt::Json::number(s.files_w);
    j["agent_w"] = scyllagpt::Json::number(s.agent_w);
    j["history_w"] = scyllagpt::Json::number(s.history_w);
    j["files_mode"] = scyllagpt::Json::number(s.files_mode);
    j["history_mode"] = scyllagpt::Json::number(s.history_mode);
    j["agent_mode"] = scyllagpt::Json::number(s.agent_mode);
    j["focus_editor"] = scyllagpt::Json::boolean(s.focus_editor);
    j["terminal_h"] = scyllagpt::Json::number(s.terminal_h);
    j["terminal_visible"] = scyllagpt::Json::boolean(s.terminal_visible);
    j["knowledge_h"] = scyllagpt::Json::number(s.knowledge_h);
    j["word_wrap"] = scyllagpt::Json::boolean(s.word_wrap);
    j["show_minimap"] = scyllagpt::Json::boolean(s.show_minimap);
    j["show_whitespace"] = scyllagpt::Json::boolean(s.show_whitespace);
    j["agent_terminal_policy"] = scyllagpt::Json::string(s.agent_terminal_policy);
    j["default_terminal_profile_id"] = scyllagpt::Json::string(s.default_terminal_profile_id);
    j["panel_surface"] = scyllagpt::Json::string(s.panel_surface);
    j["data_dir"] = scyllagpt::Json::string(scyllagpt::utf8(paths.appdata));
    return j;
}

void apply_settings_patch(scyllagpt::Settings& s, const scyllagpt::Json& patch) {
    if (patch.has("codex_path") && patch.at("codex_path").is_string())
        s.codex_path = scyllagpt::utf16(patch.at("codex_path").as_string());
    if (patch.has("default_provider") && patch.at("default_provider").is_string())
        s.default_provider = patch.at("default_provider").as_string();
    if (patch.has("selected_model") && patch.at("selected_model").is_string())
        s.selected_model = patch.at("selected_model").as_string();
    if (patch.has("reasoning_effort") && patch.at("reasoning_effort").is_string())
        s.reasoning_effort = patch.at("reasoning_effort").as_string();
    if (patch.has("enter_sends") && patch.at("enter_sends").is_bool())
        s.enter_sends = patch.at("enter_sends").as_bool();
    if (patch.has("restore_chat_on_start") && patch.at("restore_chat_on_start").is_bool())
        s.restore_chat_on_start = patch.at("restore_chat_on_start").as_bool();
    if (patch.has("project_folder") && patch.at("project_folder").is_string())
        s.project_folder = scyllagpt::utf16(patch.at("project_folder").as_string());
    if (patch.has("last_thread_id") && patch.at("last_thread_id").is_string())
        s.last_thread_id = patch.at("last_thread_id").as_string();
    if (patch.has("files_w") && patch.at("files_w").is_number())
        s.files_w = static_cast<int>(patch.at("files_w").as_int(s.files_w));
    if (patch.has("agent_w") && patch.at("agent_w").is_number())
        s.agent_w = static_cast<int>(patch.at("agent_w").as_int(s.agent_w));
    if (patch.has("history_w") && patch.at("history_w").is_number())
        s.history_w = static_cast<int>(patch.at("history_w").as_int(s.history_w));
    if (patch.has("files_mode") && patch.at("files_mode").is_number())
        s.files_mode = static_cast<int>(patch.at("files_mode").as_int(s.files_mode));
    if (patch.has("history_mode") && patch.at("history_mode").is_number())
        s.history_mode = static_cast<int>(patch.at("history_mode").as_int(s.history_mode));
    if (patch.has("agent_mode") && patch.at("agent_mode").is_number())
        s.agent_mode = static_cast<int>(patch.at("agent_mode").as_int(s.agent_mode));
    if (patch.has("focus_editor") && patch.at("focus_editor").is_bool())
        s.focus_editor = patch.at("focus_editor").as_bool();
    if (patch.has("terminal_h") && patch.at("terminal_h").is_number())
        s.terminal_h = static_cast<int>(patch.at("terminal_h").as_int(s.terminal_h));
    if (patch.has("terminal_visible") && patch.at("terminal_visible").is_bool())
        s.terminal_visible = patch.at("terminal_visible").as_bool();
    if (patch.has("knowledge_h") && patch.at("knowledge_h").is_number())
        s.knowledge_h = static_cast<int>(patch.at("knowledge_h").as_int(s.knowledge_h));
    if (patch.has("word_wrap") && patch.at("word_wrap").is_bool())
        s.word_wrap = patch.at("word_wrap").as_bool();
    if (patch.has("show_minimap") && patch.at("show_minimap").is_bool())
        s.show_minimap = patch.at("show_minimap").as_bool();
    if (patch.has("verbose_agent_progress") && patch.at("verbose_agent_progress").is_bool())
        s.verbose_agent_progress = patch.at("verbose_agent_progress").as_bool();
    if (patch.has("show_whitespace") && patch.at("show_whitespace").is_bool())
        s.show_whitespace = patch.at("show_whitespace").as_bool();
    if (patch.has("agent_terminal_policy") && patch.at("agent_terminal_policy").is_string()) {
        s.agent_terminal_policy = patch.at("agent_terminal_policy").as_string();
        if (s.agent_terminal_policy != "allow" && s.agent_terminal_policy != "block")
            s.agent_terminal_policy = "ask";
    }
    if (patch.has("default_terminal_profile_id") && patch.at("default_terminal_profile_id").is_string())
        s.default_terminal_profile_id = patch.at("default_terminal_profile_id").as_string();
    if (patch.has("panel_surface") && patch.at("panel_surface").is_string()) {
        s.panel_surface = patch.at("panel_surface").as_string();
        if (s.panel_surface != "payload" && s.panel_surface != "problems" && s.panel_surface != "output" && s.panel_surface != "ports")
            s.panel_surface = "terminal";
    }
}

scyllagpt::WorkspaceStore load_store(const scyllagpt::Paths& paths) {
    scyllagpt::WorkspaceStore store;
    store.load(paths.store_path);
    return store;
}

bool save_store(const scyllagpt::Paths& paths, const scyllagpt::WorkspaceStore& store) {
    return store.save(paths.store_path);
}

scyllagpt::Project* ensure_active_project(scyllagpt::WorkspaceStore& store, scyllagpt::Settings& settings) {
    if (!settings.project_folder.empty()) {
        auto* p = store.open_or_create(settings.project_folder);
        if (p) return p;
    }
    return store.active();
}

}  // namespace

struct scylla_session {
    scyllagpt::Session session;
    std::mutex mu;
    std::deque<std::string> pending_lines;
    std::deque<scyllagpt::ClaudePrintResult> pending_claude;
    bool started = false;
    std::mutex broker_context_mutex;
    std::string broker_project;
    bool broker_execute_mode = false;
    scyllagpt::BrokerPipeService broker_service;
};

static void start_session_broker(scylla_session* handle) {
    if (handle->broker_service.running()) return;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto helper = std::filesystem::path(executable).parent_path() / L"scylla-broker.exe";
    if (!scyllagpt::file_exists(helper.wstring())) return;
    std::string error;
    if (!handle->broker_service.start([handle](const scyllagpt::QueryToolCall& call) {
        std::string project;
        bool execute_mode;
        { std::lock_guard lock(handle->broker_context_mutex);
          project = handle->broker_project; execute_mode = handle->broker_execute_mode; }
        const auto paths = scyllagpt::make_paths();
        scyllagpt::ProjectConnectionManager connections;
        scyllagpt::SshQueryExecutor executor(scyllagpt::join_path(paths.appdata, L"broker-run"));
        scyllagpt::ConnectionBroker broker(connections, g_app_keyring, executor);
        scyllagpt::PreparedOperation prepared;
        {
            std::lock_guard lock(g_security_mutex);
            if (!connections.load(paths.connections_path)) {
                scyllagpt::BrokerResponse response;
                response.safe_message = "Could not load saved connections.";
                return scyllagpt::encode_broker_response(response);
            }
            // Name-only legacy credentials are ambiguous across scopes. Refuse rather than
            // letting Keyring's first-match lookup select another project's value.
            const auto all = g_app_keyring.list_refs();
            const auto visible = g_app_keyring.list_refs_for_ui(project);
            for (const auto& ref : all) {
                if (std::count_if(all.begin(), all.end(), [&](const auto& r) { return r.name == ref.name; }) > 1) {
                    scyllagpt::BrokerResponse response;
                    response.safe_message = "Duplicate keyring reference names must be resolved before broker use.";
                    return scyllagpt::encode_broker_response(response);
                }
            }
            if (auto* connection = const_cast<scyllagpt::ProjectConnection*>(connections.resolve_alias(project, call.connection_alias))) {
                for (const auto* ref : { &connection->ssh.host_ref, &connection->ssh.port_ref, &connection->ssh.username_ref,
                    &connection->ssh.auth_ref, &connection->ssh.private_key_ref, &connection->ssh.key_passphrase_ref,
                    &connection->database.host_ref, &connection->database.port_ref, &connection->database.username_ref,
                    &connection->database.password_ref, &connection->database.tls_ca_ref }) {
                    if (!ref->empty() && std::none_of(visible.begin(), visible.end(), [&](const auto& r) { return r.name == *ref; })) {
                        scyllagpt::BrokerResponse response;
                        response.safe_message = "A credential reference is unavailable in this project.";
                        return scyllagpt::encode_broker_response(response);
                    }
                }
                if (!execute_mode) {
                    connection->command_authority = scyllagpt::ConnectionAuthority::Block;
                    connection->query_policy.data_modification = scyllagpt::ConnectionAuthority::Block;
                    connection->query_policy.schema_modification = scyllagpt::ConnectionAuthority::Block;
                    connection->query_policy.administrative = scyllagpt::ConnectionAuthority::Block;
                    connection->query_policy.unrestricted = false;
                }
                if (connection->ssh_only) {
                    const auto settings = scyllagpt::load_settings(paths.settings_path);
                    const auto policy = scyllagpt::terminal_agent_policy(settings.terminal_profile_policy,
                        connection->terminal_profile_id, settings.agent_terminal_policy);
                    if (policy == "block") connection->command_authority = scyllagpt::ConnectionAuthority::Block;
                    else if (policy == "ask" && connection->command_authority != scyllagpt::ConnectionAuthority::Block)
                        connection->command_authority = scyllagpt::ConnectionAuthority::Ask;
                }
            }
            prepared = broker.prepare({scyllagpt::BrokerPipeService::make_secret_token(), project,
                call.connection_alias, call.sql, false, call.ssh_command});
        }
        return scyllagpt::encode_broker_response(prepared.ready ? broker.finish(std::move(prepared)) : prepared.response);
    }, &error)) return;
    scyllagpt::CodexMcpServer server;
    server.name = "scylla-query";
    server.stdio = true;
    server.command = helper.wstring();
    server.environment = {{scyllagpt::utf16(scyllagpt::kBrokerTokenEnvVar), scyllagpt::utf16(handle->broker_service.token())}};
    handle->session.set_broker_mcp_server(std::move(server));
}

extern "C" {

const char* scylla_core_version(void) {
    return "0.6.0-core";
}

int scylla_core_try_acquire_instance(void) {
    if (g_instance.owned) return 1;
    g_instance = scyllagpt::try_acquire_instance();
    return g_instance.owned ? 1 : 0;
}

void scylla_core_release_instance(void) {
    { std::lock_guard lock(g_security_mutex); g_app_keyring.lock(); }
    scyllagpt::release_instance(&g_instance);
}

int scylla_core_activate_existing(void) {
    return scyllagpt::activate_existing_instance() ? 1 : 0;
}

int scylla_core_data_dir_utf8(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    return write_utf8(buf, buf_len, scyllagpt::utf8(paths.appdata));
}

int scylla_core_settings_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    const auto s = scyllagpt::load_settings(paths.settings_path);
    return write_utf8(buf, buf_len, settings_to_json(s, paths).dump());
}

int scylla_core_patch_settings(const char* json_utf8, char* err_utf8, int err_len) {
    if (!json_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null json");
        return 0;
    }
    std::string err;
    auto patch = scyllagpt::Json::parse(json_utf8, &err);
    if (!err.empty() || !patch.is_object()) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, err.empty() ? "invalid json" : err);
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    auto s = scyllagpt::load_settings(paths.settings_path);
    apply_settings_patch(s, patch);
    if (!scyllagpt::save_settings(paths.settings_path, s)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save_settings failed");
        return 0;
    }
    return 1;
}

int scylla_core_set_project_folder(const char* path_utf8, char* err_utf8, int err_len) {
    if (!path_utf8 || !*path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty path");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    auto s = scyllagpt::load_settings(paths.settings_path);
    s.project_folder = scyllagpt::utf16(path_utf8);
    auto store = load_store(paths);
    if (!store.open_or_create(s.project_folder)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "open_or_create failed");
        return 0;
    }
    if (!scyllagpt::save_settings(paths.settings_path, s) || !save_store(paths, store)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_list_dir(const char* path_utf8, char* buf, int buf_len) {
    if (!path_utf8 || !*path_utf8) return -1;
    const std::wstring root = scyllagpt::utf16(path_utf8);
    const std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    scyllagpt::Json arr = scyllagpt::Json::array();
    if (h == INVALID_HANDLE_VALUE) {
        return write_utf8(buf, buf_len, arr.dump());
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::wstring full = root + L"\\" + fd.cFileName;
        scyllagpt::Json row = scyllagpt::Json::object();
        row["name"] = scyllagpt::Json::string(scyllagpt::utf8(fd.cFileName));
        row["path"] = scyllagpt::Json::string(scyllagpt::utf8(full));
        row["is_dir"] = scyllagpt::Json::boolean(is_dir);
        arr.push(std::move(row));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return write_utf8(buf, buf_len, arr.dump());
}

int scylla_core_read_text_file(const char* path_utf8, char* buf, int buf_len) {
    if (!path_utf8 || !*path_utf8) return -1;
    auto loaded = scyllagpt::load_text_file(scyllagpt::utf16(path_utf8));
    if (!loaded.error.empty() || loaded.binary || loaded.too_large) return -1;
    return write_utf8(buf, buf_len, scyllagpt::utf8(loaded.text));
}

int scylla_core_write_text_file(const char* path_utf8, const char* text_utf8, char* err_utf8, int err_len) {
    if (!path_utf8 || !text_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null args");
        return 0;
    }
    const std::wstring path = scyllagpt::utf16(path_utf8);
    auto existing = scyllagpt::load_text_file(path);
    const bool crlf = existing.error.empty() ? existing.crlf : true;
    const auto enc = existing.error.empty() ? existing.enc : scyllagpt::TextEnc::Utf8;
    const auto hash = existing.error.empty() ? existing.hash : 0;
    auto result = scyllagpt::save_text_file(path, scyllagpt::utf16(text_utf8), enc, crlf, hash);
    if (!result.ok) {
        if (err_utf8 && err_len > 0)
            write_utf8(err_utf8, err_len, result.error.empty() ? "save failed" : scyllagpt::utf8(result.error));
        return 0;
    }
    return 1;
}

scylla_session* scylla_session_create(void) {
    auto* s = new (std::nothrow) scylla_session{};
    if (!s) return nullptr;
    s->session.paths = scyllagpt::make_paths();
    s->session.settings = scyllagpt::load_settings(s->session.paths.settings_path);
    s->session.selected_model = s->session.settings.selected_model;
    s->session.project_root = s->session.settings.project_folder;
    s->session.store.load(s->session.paths.store_path);
    // The runtime is started lazily. Saved chats are available from the store
    // immediately; after_ready() requests the remote list once connected.
    return s;
}

int scylla_session_set_project_roots(scylla_session* session, const char* roots_json, char* err_utf8, int err_len) {
    if (!session || !roots_json) return 0;
    auto& sess = session->session;
    auto fail = [&](const char* message) { write_utf8(err_utf8, err_len, message); return 0; };
    std::string error;
    const auto json = scyllagpt::Json::parse(roots_json, &error);
    if (!error.empty() || !json.is_array()) return fail("Expected project folder array.");
    std::vector<std::wstring> roots;
    for (const auto& item : json.array_items()) {
        if (!item.is_string() || item.as_string().empty()) return fail("Invalid project folder.");
        const auto path = scyllagpt::canonicalize_path(scyllagpt::utf16(item.as_string()));
        const auto attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return fail("A selected project folder is unavailable.");
        if (std::none_of(roots.begin(), roots.end(), [&](const auto& r) { return _wcsicmp(r.c_str(), path.c_str()) == 0; }))
            roots.push_back(path);
    }
    if (roots == sess.store.open_roots()) return 1;
    sess.reconcile_idle_activity();
    if (sess.workspace_change_pending()) return fail("Finish or stop running chats before changing project folders.");
    const auto old_store = sess.store;
    const auto old_settings = sess.settings;
    if (roots.empty()) sess.store.active_project_id.clear();
    else {
        auto* project = sess.store.open_or_create(roots.front());
        if (!project) return fail("Could not open project.");
        project->roots = roots;
    }
    sess.settings = scyllagpt::load_settings(sess.paths.settings_path);
    sess.settings.project_folder = roots.empty() ? L"" : roots.front();
    sess.settings.last_thread_id.clear();
    if (!sess.store.save(sess.paths.store_path) || !scyllagpt::save_settings(sess.paths.settings_path, sess.settings)) {
        sess.store = old_store;
        sess.settings = old_settings;
        sess.store.save(sess.paths.store_path);
        return fail("Could not save project folders.");
    }
    sess.project_root = sess.settings.project_folder;
    sess.active_thread_id.clear(); sess.active_turn_id.clear();
    sess.history_messages.clear(); sess.stream_buffer.clear(); sess.activity = {};
    sess.last_plan_path.clear(); sess.transcript_replace = true;
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(sess.paths.knowledge_path);
    sess.set_knowledge_accessible_paths(scyllagpt::scoped_knowledge_paths(sess.store, knowledge));
    return 1;
}

void scylla_session_destroy(scylla_session* session) {
    if (!session) return;
    session->session.stop_runtime();
    delete session;
}

int scylla_session_start(scylla_session* session, char* err_utf8, int err_len) {
    if (!session) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null session");
        return 0;
    }
    start_session_broker(session);
    scyllagpt::LineSink on_line = [session](std::string line) {
        std::lock_guard<std::mutex> lock(session->mu);
        session->pending_lines.push_back(std::move(line));
    };
    scyllagpt::ClaudeDoneSink on_done = [session](scyllagpt::ClaudePrintResult result) {
        std::lock_guard<std::mutex> lock(session->mu);
        session->pending_claude.push_back(std::move(result));
    };
    std::wstring err;
    if (!session->session.start_runtime(std::move(on_line), std::move(on_done), &err)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, scyllagpt::utf8(err));
        return 0;
    }
    session->started = true;
    return 1;
}

void scylla_session_stop(scylla_session* session) {
    if (!session) return;
    session->session.stop_runtime();
    session->started = false;
}

int scylla_session_send_user(scylla_session* session, const char* text_utf8, char* err_utf8, int err_len) {
    return scylla_session_send_user_mode(session, text_utf8, "execute", err_utf8, err_len);
}

int scylla_session_send_user_mode(scylla_session* session, const char* text_utf8, const char* mode, char* err_utf8, int err_len) {
    if (!session) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null session");
        return 0;
    }
    if (!text_utf8 || !*text_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty message");
        return 0;
    }
    try {
        if (!mode || !scyllagpt::valid_chat_mode(mode)) {
            write_utf8(err_utf8, err_len, "Unknown chat mode."); return 0;
        }
        if (std::string(mode) != "execute" && session->session.settings.default_provider != "openai") {
            write_utf8(err_utf8, err_len, "Plan and Ask currently require the ChatGPT provider for enforced read-only access."); return 0;
        }
        const auto display = scyllagpt::parse_user_display(text_utf8);
        scyllagpt::KnowledgeStore knowledge;
        knowledge.load(session->session.paths.knowledge_path);
        session->session.set_knowledge_accessible_paths(
            scyllagpt::scoped_knowledge_paths(session->session.store, knowledge));
        if (session->session.settings.default_provider != "openai") {
            for (const auto& attachment : display.attachments) if (attachment.image) {
                if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len,
                    "Image attachments currently require the ChatGPT/Codex provider.");
                return 0;
            }
        }
        session->session.chat_title_seed = display.text.empty() ? "Image discussion" : display.text;
        {
            std::lock_guard lock(session->broker_context_mutex);
            session->broker_project.clear();
            const auto cwd = session->session.conversation_cwd();
            for (const auto& project : session->session.store.projects)
                if (scyllagpt::project_key(project.root) == scyllagpt::project_key(cwd))
                    session->broker_project = project.id;
            session->broker_execute_mode = std::string(mode) == "execute";
        }
        session->session.send_user(text_utf8, mode);
        return 1;
    } catch (...) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "send_user failed");
        return 0;
    }
}

void scylla_session_cancel(scylla_session* session) {
    if (!session) return;
    session->session.cancel_turn();
}

int scylla_session_new_chat(scylla_session* session, char* err_utf8, int err_len) {
    if (!session) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null session");
        return 0;
    }
    try {
        session->session.new_conversation();
        return 1;
    } catch (...) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "new_chat failed");
        return 0;
    }
}

int scylla_session_delete_thread(scylla_session* session, const char* thread_id_utf8, char* err_utf8, int err_len) {
    try {
        std::string error;
        if (session && thread_id_utf8 && session->session.delete_thread(thread_id_utf8, &error)) return 1;
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, error.empty() ? "Invalid chat" : error);
    } catch (...) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "Chat deletion failed");
    }
    return 0;
}

int scylla_session_open_thread(scylla_session* session, const char* thread_id_utf8, char* err_utf8, int err_len) {
    if (!session || !thread_id_utf8 || !*thread_id_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "bad args");
        return 0;
    }
    try {
        const auto* stored = session->session.store.by_thread(thread_id_utf8);
        const auto& sess = session->session;
        const bool visible = stored ? (!stored->deleted && sess.store.contains_open_project(stored->project_id))
            : std::any_of(sess.threads.begin(), sess.threads.end(), [&](const auto& t) {
                return t.id == thread_id_utf8 && sess.store.contains_open_path(t.cwd);
            });
        if (!visible) { write_utf8(err_utf8, err_len, "Chat is outside the open projects."); return 0; }
        if (!stored) {
            auto& workspace = session->session.store;
            const auto summary = std::find_if(sess.threads.begin(), sess.threads.end(), [&](const auto& t) { return t.id == thread_id_utf8; });
            const auto active = workspace.active_project_id;
            std::wstring owner;
            for (const auto& root : workspace.open_roots()) {
                const auto cwd = scyllagpt::project_key(scyllagpt::canonicalize_path(summary->cwd));
                const auto key = scyllagpt::project_key(scyllagpt::canonicalize_path(root));
                if ((cwd == key || cwd.starts_with(key + L"\\")) && root.size() > owner.size()) owner = root;
            }
            auto* project = workspace.open_or_create(owner);
            const auto project_id = project->id;
            workspace.active_project_id = active;
            workspace.upsert_thread(project_id, sess.account_scope(), summary->id, summary->name, summary->preview);
            workspace.save(sess.paths.store_path);
        }
        session->session.open_thread(thread_id_utf8);
        return 1;
    } catch (...) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "open_thread failed");
        return 0;
    }
}

int scylla_session_poll(scylla_session* session, char* buf, int buf_len) {
    if (!session) return -1;

    std::deque<scyllagpt::ClaudePrintResult> dones;
    {
        std::lock_guard<std::mutex> lock(session->mu);
        dones.swap(session->pending_claude);
    }
    // A burst of tool/token events must not monopolize the window message thread.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    for (int count = 0; count < 64; ++count) {
        std::string line;
        {
            std::lock_guard<std::mutex> lock(session->mu);
            if (session->pending_lines.empty()) break;
            line = std::move(session->pending_lines.front());
            session->pending_lines.pop_front();
        }
        session->session.handle_line(line);
        if (std::chrono::steady_clock::now() >= deadline) break;
    }
    for (auto& done : dones) {
        session->session.complete_claude_print(done.ok, done.text, done.error);
    }

    auto& sess = session->session;
    scyllagpt::Json root = scyllagpt::Json::object();
    std::string payload_text;
    for (const auto& line : sess.payload_log) { payload_text += line; payload_text += '\n'; }
    root["payload_log"] = scyllagpt::Json::string(std::move(payload_text));
    root["state"] = scyllagpt::Json::string(app_state_name(sess.state));
    root["status"] = scyllagpt::Json::string(scyllagpt::utf8(sess.status_text));
    root["runtime_live"] = scyllagpt::Json::boolean(sess.runtime_live());
    root["activity"] = scyllagpt::Json::string(sess.activity.summary());
    root["stream"] = scyllagpt::Json::string(scyllagpt::visible_chat_text(sess.stream_buffer));
    root["active_thread_id"] = scyllagpt::Json::string(sess.active_thread_id);
    root["selected_model"] = scyllagpt::Json::string(sess.selected_model);
    root["reasoning_effort"] = scyllagpt::Json::string(sess.settings.reasoning_effort);
    root["default_provider"] = scyllagpt::Json::string(sess.settings.default_provider);
    root["last_error"] = scyllagpt::Json::string(sess.last_error);
    root["last_plan_path"] = scyllagpt::Json::string(sess.last_plan_path);
    root["account_email"] = scyllagpt::Json::string(sess.account.email);
    root["account_signed_in"] = scyllagpt::Json::boolean(sess.account.signed_in);
    root["account_loaded"] = scyllagpt::Json::boolean(sess.account_loaded);
    root["project_folder"] = scyllagpt::Json::string(scyllagpt::utf8(sess.settings.project_folder));
    root["grant_label"] = scyllagpt::Json::string(scyllagpt::utf8(sess.grant_label()));

    scyllagpt::Json history = scyllagpt::Json::array();
    for (const auto& m : sess.history_messages) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["user"] = scyllagpt::Json::boolean(m.user);
        row["text"] = scyllagpt::Json::string(m.user
            ? scyllagpt::visible_user_text(m.text)
            : scyllagpt::visible_chat_text(m.text));
        row["attachments"] = scyllagpt::Json::array();
        if (m.user) for (const auto& attachment : scyllagpt::parse_user_display(m.text).attachments) {
            scyllagpt::Json entry = scyllagpt::Json::object();
            entry["path"] = scyllagpt::Json::string(attachment.path);
            entry["label"] = scyllagpt::Json::string(attachment.label);
            entry["image"] = scyllagpt::Json::boolean(attachment.image);
            row["attachments"].push(std::move(entry));
        }
        history.push(std::move(row));
    }
    root["history"] = std::move(history);

    scyllagpt::Json threads = scyllagpt::Json::array();
    root["project_roots"] = scyllagpt::Json::array();
    for (const auto& path : sess.store.open_roots())
        root["project_roots"].push(scyllagpt::Json::string(scyllagpt::utf8(path)));
    auto add_activity = [&](scyllagpt::Json& row, const std::string& id) {
        if (const auto* activity = sess.thread_activity(id); activity && activity->visible) {
            row["busy"] = scyllagpt::Json::boolean(activity->busy);
            row["activity_phase"] = scyllagpt::Json::string(activity->phase);
            row["active_agents"] = scyllagpt::Json::number(static_cast<int>(activity->agents.size()));
        }
    };
    const auto scope = sess.account.type.empty() ? std::string{} : sess.account_scope();
    for (const auto* conv : sess.store.list_visible(scope, L"")) {
        auto projects = scyllagpt::chat_project_paths(sess.store, conv, L"", conv->title, conv->preview);
        if (projects.array_items().empty()) continue;
        scyllagpt::Json row = scyllagpt::Json::object();
        row["project_paths"] = std::move(projects);
        add_activity(row, conv->thread_id);
        row["id"] = scyllagpt::Json::string(conv->thread_id);
        row["name"] = scyllagpt::Json::string(conv->title.empty() ? "New Chat" : conv->title);
        row["preview"] = scyllagpt::Json::string(conv->preview);
        row["updated_at"] = scyllagpt::Json::number(conv->updated_at);
        threads.push(std::move(row));
    }
    for (const auto& t : sess.threads) {
        // Stored entries own their title, visibility and ordering.
        if (sess.store.by_thread(t.id)) continue;
        if (!sess.store.contains_open_path(t.cwd)) continue;
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(t.id);
        row["name"] = scyllagpt::Json::string(t.name);
        row["preview"] = scyllagpt::Json::string(t.preview);
        row["project_paths"] = scyllagpt::chat_project_paths(sess.store, nullptr, t.cwd, t.name, t.preview);
        add_activity(row, t.id);
        // Unix seconds; shells group the history list by day.
        std::int64_t updated = 0;
        if (const auto* conv = sess.store.by_thread(t.id)) {
            updated = conv->updated_at;
        }
        row["updated_at"] = scyllagpt::Json::number(updated);
        threads.push(std::move(row));
    }
    root["threads"] = std::move(threads);

    scyllagpt::Json models = scyllagpt::Json::array();
    for (const auto& m : sess.models_for_provider(sess.settings.default_provider, true)) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(m.id);
        row["label"] = scyllagpt::Json::string(m.display.empty() ? m.id : m.display);
        row["specialty"] = scyllagpt::Json::string(m.specialty);
        row["default_reasoning_effort"] = scyllagpt::Json::string(m.default_reasoning_effort);
        row["reasoning_efforts"] = scyllagpt::Json::array();
        for (const auto& effort : m.reasoning_efforts)
            row["reasoning_efforts"].push(scyllagpt::Json::string(effort));
        models.push(std::move(row));
    }
    root["models"] = std::move(models);

    return write_utf8(buf, buf_len, root.dump());
}

int scylla_session_providers_json(scylla_session* session, char* buf, int buf_len) {
    if (!session) return -1;
    auto& sess = session->session;
    const auto oa = scyllagpt::openai_provider_status(sess.account.signed_in, sess.account.email,
                                                       sess.account.plan, sess.account.type);
    const auto oa_api = scyllagpt::openai_api_provider_status();
    const auto cl = scyllagpt::claude_provider_status();
    const auto cl_api = scyllagpt::claude_api_provider_status();
    const scyllagpt::ClaudeCodeSession cl_sess = scyllagpt::claude_code_session_status(false);
    sess.sync_claude_models(); // Pick up a completed asynchronous authentication probe.
    const bool claude_cli = !scyllagpt::discover_claude_cli().empty();

    auto push_card = [&](scyllagpt::Json& arr, const scyllagpt::ProviderStatus& st, const char* product,
                         const std::string& detail_override) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(scyllagpt::provider_id_string(st.id));
        row["display_name"] = scyllagpt::Json::string(st.display_name);
        row["connected"] = scyllagpt::Json::boolean(st.connected);
        row["auth_label"] = scyllagpt::Json::string(st.auth_label);
        row["detail"] = scyllagpt::Json::string(detail_override.empty() ? st.detail : detail_override);
        row["product"] = scyllagpt::Json::string(product);
        row["claude_cli_present"] = scyllagpt::Json::boolean(claude_cli);
        scyllagpt::Json catalog = scyllagpt::Json::array();
        const std::string pid = scyllagpt::provider_id_string(st.id);
        for (const auto& m : sess.models_for_provider(pid, false)) {
            scyllagpt::Json model = scyllagpt::Json::object();
            model["id"] = scyllagpt::Json::string(m.id);
            model["label"] = scyllagpt::Json::string(m.display.empty() ? m.id : m.display);
            model["specialty"] = scyllagpt::Json::string(m.specialty);
            model["enabled"] = scyllagpt::Json::boolean(sess.is_model_enabled(pid, m.id));
            model["default_reasoning_effort"] = scyllagpt::Json::string(m.default_reasoning_effort);
            model["reasoning_efforts"] = scyllagpt::Json::array();
            for (const auto& effort : m.reasoning_efforts)
                model["reasoning_efforts"].push(scyllagpt::Json::string(effort));
            catalog.push(std::move(model));
        }
        row["models"] = std::move(catalog);
        arr.push(std::move(row));
    };

    scyllagpt::Json arr = scyllagpt::Json::array();
    push_card(arr, oa, "ChatGPT subscription", {});
    push_card(arr, oa_api, "OpenAI API (BYOK)", {});
    {
        std::string detail;
        if (cl_sess.logged_in && !cl_sess.email.empty()) {
            detail = cl_sess.email;
            if (!cl_sess.subscription.empty()) {
                detail += " · ";
                detail += cl_sess.subscription;
            }
        }
        push_card(arr, cl, "Claude Code", detail);
    }
    push_card(arr, cl_api, "Anthropic API (BYOK)", {});
    return write_utf8(buf, buf_len, arr.dump());
}

int scylla_session_provider_action(scylla_session* session, const char* provider_id, const char* action,
                                   const char* secret_utf8, char* err_utf8, int err_len) {
    if (!session || !provider_id || !*provider_id || !action || !*action) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "bad args");
        return 0;
    }
    auto& sess = session->session;
    const std::string pid = provider_id;
    const std::string act = action;
    // The Fluent settings pane can change this preference after session creation.
    sess.settings.verbose_agent_progress =
        scyllagpt::load_settings(sess.paths.settings_path).verbose_agent_progress;

    auto fail = [&](const char* msg) -> int {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, msg);
        return 0;
    };
    auto persist = [&]() {
        scyllagpt::save_settings(sess.paths.settings_path, sess.settings);
        sess.sync_claude_models();
    };

    try {
        if (act == "refresh_models") {
            if (pid == "openai") sess.refresh_models();
            else if (pid == "openai-api") sess.refresh_openai_api_models();
            else if (pid == "claude-api") sess.refresh_claude_api_models();
            else if (pid == "claude") sess.sync_claude_models();
            else return fail("Unknown provider");
            return 1;
        }
        if (act == "set_default" || act == "select_model" || act == "select_effort" ||
            act == "enable_model" || act == "disable_model") {
            if (pid != "openai" && pid != "openai-api" && pid != "claude" && pid != "claude-api")
                return fail("Unknown provider");
            sess.settings = scyllagpt::load_settings(sess.paths.settings_path);
            if (act == "set_default") sess.set_default_provider(pid);
            else if (act == "select_effort") {
                const std::string effort = secret_utf8 ? secret_utf8 : "";
                const auto choices = sess.models_for_provider(pid, false);
                const auto model = std::find_if(choices.begin(), choices.end(), [&](const auto& m) {
                    return m.id == sess.selected_model;
                });
                if (model == choices.end() || std::find(model->reasoning_efforts.begin(),
                    model->reasoning_efforts.end(), effort) == model->reasoning_efforts.end())
                    return fail("Reasoning effort is not supported by the selected model");
                sess.settings.reasoning_effort = effort;
            }
            else {
                const std::string mid = secret_utf8 ? secret_utf8 : "";
                bool found = false;
                for (const auto& m : sess.models_for_provider(pid, false))
                    if (m.id == mid) found = true;
                if (!found) return fail("Model is no longer available; refresh the model list");
                if (act == "select_model") {
                    sess.set_provider_default_model(pid, mid);
                }
                else {
                    if (act == "disable_model" && pid != "openai-api" && pid != "claude-api" &&
                        sess.is_model_enabled(pid, mid) && sess.models_for_provider(pid, true).size() <= 1)
                        return fail("Keep at least one model enabled for this provider");
                    sess.set_model_enabled(pid, mid, act == "enable_model");
                }
            }
            if (!scyllagpt::save_settings(sess.paths.settings_path, sess.settings))
                return fail("Could not save provider settings");
            return 1;
        }
        if (pid == "openai" && act == "sign_in") {
            if (!session->started || !sess.runtime_live()) {
                return fail("Start the runtime before signing in with ChatGPT");
            }
            sess.login_chatgpt();
            persist();
            return 1;
        }
        if (pid == "openai" && act == "sign_out") {
            if (!session->started || !sess.runtime_live()) {
                return fail("Runtime not live");
            }
            sess.logout();
            persist();
            return 1;
        }
        if (pid == "openai-api" && act == "connect_key") {
            if (!secret_utf8 || !*secret_utf8) return fail("API key required");
            if (!scyllagpt::openai_api_key_save(scyllagpt::utf16(secret_utf8))) {
                return fail("Could not save API key to Credential Manager");
            }
            sess.refresh_openai_api_models();
            persist();
            return 1;
        }
        if (pid == "openai-api" && act == "disconnect") {
            scyllagpt::openai_api_key_clear();
            if (sess.settings.default_provider == "openai-api") {
                sess.set_default_provider("openai");
            }
            persist();
            return 1;
        }
        if (pid == "claude" && act == "login_cli") {
            std::wstring err;
            if (!scyllagpt::claude_code_login_launch(&err)) {
                const std::string msg =
                    err.empty() ? "Could not launch Claude Code login" : scyllagpt::utf8(err);
                return fail(msg.c_str());
            }
            scyllagpt::claude_code_session_status(true);
            persist();
            return 1;
        }
        if (pid == "claude" && act == "disconnect") {
            std::wstring err;
            if (scyllagpt::claude_code_session_status(true).logged_in) {
                if (!scyllagpt::claude_code_logout(&err) && !err.empty()) {
                    const std::string msg = scyllagpt::utf8(err);
                    return fail(msg.c_str());
                }
            }
            scyllagpt::claude_clear_connection_state();
            if (sess.settings.default_provider == "claude") {
                sess.set_default_provider("openai");
            }
            persist();
            return 1;
        }
        if (pid == "claude-api" && act == "connect_key") {
            if (!secret_utf8 || !*secret_utf8) return fail("API key required");
            if (!scyllagpt::claude_api_key_save(scyllagpt::utf16(secret_utf8))) {
                return fail("Could not save API key to Credential Manager");
            }
            sess.refresh_claude_api_models();
            persist();
            return 1;
        }
        if (pid == "claude-api" && act == "disconnect") {
            scyllagpt::claude_api_key_clear();
            if (sess.settings.default_provider == "claude-api") {
                sess.set_default_provider("openai");
            }
            persist();
            return 1;
        }
        return fail("unknown provider or action");
    } catch (...) {
        return fail("provider_action failed");
    }
}

void* scylla_editor_create(void* parent_hwnd, int control_id) {
    HWND parent = reinterpret_cast<HWND>(parent_hwnd);
    if (!parent) return nullptr;
    // Register native controls under the module that implements them.
    HMODULE inst = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&scylla_editor_create), &inst)) return nullptr;
    if (!g_editor_registered) {
        scyllagpt::editor_register(inst);
        g_editor_registered = true;
    }
    HWND sci = scyllagpt::editor_create(parent, control_id, inst);
    if (sci) {
        scyllagpt::editor_apply_chrome(sci, nullptr, 10, 96);
    }
    return sci;
}

void scylla_editor_destroy(void* editor_hwnd) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    wchar_t name[32]{};
    // HWND values can be reused after a parent island destroys its children.
    // Never destroy a WinUI bridge (or any other window) through a stale editor handle.
    if (sci && GetClassNameW(sci, name, 32) && wcscmp(name, L"Scintilla") == 0) DestroyWindow(sci);
}

void scylla_editor_move(void* editor_hwnd, int x, int y, int w, int h) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    wchar_t name[32]{};
    if (!sci || !GetClassNameW(sci, name, 32) || wcscmp(name, L"Scintilla") != 0 || w <= 0 || h <= 0) return;
    SetWindowPos(sci, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void scylla_editor_set_text_utf8(void* editor_hwnd, const char* text_utf8) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci) return;
    scyllagpt::editor_set_text(sci, scyllagpt::utf16(text_utf8 ? text_utf8 : ""));
}

int scylla_editor_get_text_utf8(void* editor_hwnd, char* buf, int buf_len) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci) return -1;
    return write_utf8(buf, buf_len, scyllagpt::utf8(scyllagpt::editor_get_text(sci)));
}

int scylla_editor_load_path(void* editor_hwnd, const char* path_utf8, char* err_utf8, int err_len) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci || !path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "bad args");
        return 0;
    }
    const std::wstring path = scyllagpt::utf16(path_utf8);
    auto loaded = scyllagpt::load_text_file(path);
    if (!loaded.error.empty()) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, scyllagpt::utf8(loaded.error));
        return 0;
    }
    if (loaded.binary || loaded.too_large) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "unsupported file");
        return 0;
    }
    scyllagpt::editor_set_text(sci, loaded.text);
    scyllagpt::editor_clear_undo(sci);
    scyllagpt::editor_set_language(sci, path, loaded.too_large);
    return 1;
}

int scylla_editor_save_path(void* editor_hwnd, const char* path_utf8, char* err_utf8, int err_len) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci || !path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "bad args");
        return 0;
    }
    const std::wstring path = scyllagpt::utf16(path_utf8);
    auto existing = scyllagpt::load_text_file(path);
    const bool crlf = existing.error.empty() ? existing.crlf : true;
    const auto enc = existing.error.empty() ? existing.enc : scyllagpt::TextEnc::Utf8;
    const auto hash = existing.error.empty() ? existing.hash : 0;
    auto result =
        scyllagpt::save_text_file(path, scyllagpt::editor_get_text(sci), enc, crlf, hash);
    if (!result.ok) {
        if (err_utf8 && err_len > 0)
            write_utf8(err_utf8, err_len, result.error.empty() ? "save failed" : scyllagpt::utf8(result.error));
        return 0;
    }
    return 1;
}

void scylla_editor_apply_chrome(void* editor_hwnd, int dpi) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci) return;
    scyllagpt::editor_apply_chrome(sci, nullptr, 10, dpi > 0 ? dpi : 96);
}

void scylla_editor_set_word_wrap(void* editor_hwnd, int wrap) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci) return;
    scyllagpt::editor_set_word_wrap(sci, wrap != 0);
}

void scylla_editor_set_whitespace(void* editor_hwnd, int show) {
    HWND sci = reinterpret_cast<HWND>(editor_hwnd);
    if (!sci) return;
    scyllagpt::editor_set_whitespace(sci, show != 0);
}

void* scylla_editor_create_minimap(void* parent_hwnd, void* editor_hwnd, int control_id, int dpi) {
    HWND parent = reinterpret_cast<HWND>(parent_hwnd);
    HWND primary = reinterpret_cast<HWND>(editor_hwnd);
    if (!parent || !primary) return nullptr;
    HMODULE inst = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&scylla_editor_create_minimap), &inst)) return nullptr;
    return scyllagpt::editor_create_minimap(parent, primary, control_id, inst, dpi > 0 ? dpi : 96);
}

void scylla_editor_sync_minimap(void* editor_hwnd, void* minimap_hwnd) {
    scyllagpt::editor_sync_minimap(reinterpret_cast<HWND>(editor_hwnd),
                                  reinterpret_cast<HWND>(minimap_hwnd));
}

int scylla_core_pick_folders(void* owner, char* buf, int buf_len) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return -1;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    dialog->SetTitle(L"Add project folders");
    scyllagpt::Json paths = scyllagpt::Json::array();
    const HRESULT shown = dialog->Show(static_cast<HWND>(owner));
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(shown) && SUCCEEDED(dialog->GetResults(&items))) {
        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            if (FAILED(items->GetItemAt(i, &item))) continue;
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                paths.push(scyllagpt::Json::string(scyllagpt::utf8(path)));
                CoTaskMemFree(path);
            }
            item->Release();
        }
        items->Release();
    }
    dialog->Release();
    if (FAILED(shown) && shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) return -1;
    return write_utf8(buf, buf_len, paths.dump());
}

int scylla_core_project_roots_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto store = load_store(paths);
    auto* project = ensure_active_project(store, settings);
    scyllagpt::Json root = scyllagpt::Json::object();
    root["project_id"] = scyllagpt::Json::string(project ? project->id : "");
    root["primary"] = scyllagpt::Json::string(project ? scyllagpt::utf8(project->root) : scyllagpt::utf8(settings.project_folder));
    scyllagpt::Json arr = scyllagpt::Json::array();
    if (project) {
        auto push_root = [&](const std::wstring& path) {
            scyllagpt::Json row = scyllagpt::Json::object();
            row["path"] = scyllagpt::Json::string(scyllagpt::utf8(path));
            const auto slash = path.find_last_of(L"\\/");
            const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
            row["name"] = scyllagpt::Json::string(scyllagpt::utf8(name));
            arr.push(std::move(row));
        };
        if (!project->root.empty()) push_root(project->root);
        for (const auto& r : project->roots) {
            if (r.empty()) continue;
            if (!project->root.empty() && _wcsicmp(r.c_str(), project->root.c_str()) == 0) continue;
            push_root(r);
        }
    } else if (!settings.project_folder.empty()) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["path"] = scyllagpt::Json::string(scyllagpt::utf8(settings.project_folder));
        const auto& path = settings.project_folder;
        const auto slash = path.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        row["name"] = scyllagpt::Json::string(scyllagpt::utf8(name));
        arr.push(std::move(row));
    }
    root["roots"] = std::move(arr);
    return write_utf8(buf, buf_len, root.dump());
}

int scylla_core_add_project_root(const char* path_utf8, char* err_utf8, int err_len) {
    if (!path_utf8 || !*path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty path");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto store = load_store(paths);
    const std::wstring folder = scyllagpt::utf16(path_utf8);
    auto* project = ensure_active_project(store, settings);
    if (!project) {
        if (settings.project_folder.empty()) settings.project_folder = folder;
        project = store.open_or_create(settings.project_folder.empty() ? folder : settings.project_folder);
    }
    if (!project) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "no active project");
        return 0;
    }
    if (!store.add_root(project->id, folder)) {
        // already present is ok
    }
    if (settings.project_folder.empty()) settings.project_folder = project->root;
    if (!scyllagpt::save_settings(paths.settings_path, settings) || !save_store(paths, store)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_remove_project_root(const char* path_utf8, char* err_utf8, int err_len) {
    if (!path_utf8 || !*path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty path");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto store = load_store(paths);
    auto* project = ensure_active_project(store, settings);
    if (!project) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "no active project");
        return 0;
    }
    if (!store.remove_root(project->id, scyllagpt::utf16(path_utf8))) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "remove_root failed");
        return 0;
    }
    if (!save_store(paths, store)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_access_summary_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto store = load_store(paths);
    auto* project = ensure_active_project(store, settings);
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    int root_count = 0;
    std::string primary;
    if (project) {
        primary = scyllagpt::utf8(project->root);
        root_count = 1 + static_cast<int>(project->roots.size());
    } else {
        primary = scyllagpt::utf8(settings.project_folder);
        root_count = primary.empty() ? 0 : 1;
    }
    int knowledge_count = 0;
    for (const auto& s : knowledge.sources()) {
        if (s.enabled) ++knowledge_count;
    }
    scyllagpt::Json j = scyllagpt::Json::object();
    j["primary"] = scyllagpt::Json::string(primary);
    j["root_count"] = scyllagpt::Json::number(root_count);
    j["knowledge_count"] = scyllagpt::Json::number(knowledge_count);
    j["project_id"] = scyllagpt::Json::string(project ? project->id : "");
    j["label"] = scyllagpt::Json::string(root_count <= 1
                                             ? (primary.empty() ? "No project folder" : "1 authorized folder")
                                             : (std::to_string(root_count) + " authorized folders"));
    return write_utf8(buf, buf_len, j.dump());
}

int scylla_core_knowledge_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    scyllagpt::Json arr = scyllagpt::Json::array();
    for (const auto& s : knowledge.sources()) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(s.id);
        row["label"] = scyllagpt::Json::string(scyllagpt::utf8(s.label));
        row["path"] = scyllagpt::Json::string(scyllagpt::utf8(s.path));
        row["type"] = scyllagpt::Json::string(scyllagpt::source_type_to_string(s.type));
        row["access"] = scyllagpt::Json::string(scyllagpt::access_mode_to_string(s.access));
        row["enabled"] = scyllagpt::Json::boolean(s.enabled);
        row["agent_available"] = scyllagpt::Json::boolean(s.agent_available);
        arr.push(std::move(row));
    }
    return write_utf8(buf, buf_len, arr.dump());
}

int scylla_core_knowledge_add(const char* label_utf8, const char* path_utf8, char* err_utf8, int err_len) {
    if (!path_utf8 || !*path_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty path");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    const std::wstring folder = scyllagpt::utf16(path_utf8);
    std::wstring label = label_utf8 && *label_utf8 ? scyllagpt::utf16(label_utf8) : L"";
    if (label.empty()) {
        const auto slash = folder.find_last_of(L"\\/");
        label = slash == std::wstring::npos ? folder : folder.substr(slash + 1);
    }
    auto* src = knowledge.add_source(label, folder, scyllagpt::SourceType::Knowledge,
                                     scyllagpt::AccessMode::ReadOnly, true, {});
    if (!src) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "add_source failed");
        return 0;
    }
    if (!knowledge.save(paths.knowledge_path)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_knowledge_remove(const char* id_utf8, char* err_utf8, int err_len) {
    if (!id_utf8 || !*id_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty id");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    if (!knowledge.remove(id_utf8)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "not found");
        return 0;
    }
    if (!knowledge.save(paths.knowledge_path)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_knowledge_set_flags(const char* id_utf8, int enabled, int agent_available, char* err_utf8,
                                    int err_len) {
    if (!id_utf8 || !*id_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "empty id");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    auto* src = knowledge.by_id(id_utf8);
    if (!src) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "not found");
        return 0;
    }
    src->enabled = enabled != 0;
    src->agent_available = agent_available != 0;
    if (!knowledge.save(paths.knowledge_path)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_markdown_runs_json(const char* text_utf8, char* buf, int buf_len) {
    const std::wstring wide = scyllagpt::utf16(text_utf8 ? text_utf8 : "");
    const auto runs = scyllagpt::parse_markdown(wide);
    scyllagpt::Json arr = scyllagpt::Json::array();
    for (const auto& r : runs) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["text"] = scyllagpt::Json::string(scyllagpt::utf8(r.text));
        row["bold"] = scyllagpt::Json::boolean(r.bold);
        row["italic"] = scyllagpt::Json::boolean(r.italic);
        row["code"] = scyllagpt::Json::boolean(r.code);
        row["strike"] = scyllagpt::Json::boolean(r.strike);
        row["heading"] = scyllagpt::Json::number(r.heading);
        row["link"] = scyllagpt::Json::string(scyllagpt::utf8(r.link));
        arr.push(std::move(row));
    }
    return write_utf8(buf, buf_len, arr.dump());
}

int scylla_core_mcp_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    scyllagpt::McpManager mgr;
    mgr.load(paths.mcp_path);
    scyllagpt::Json arr = scyllagpt::Json::array();
    const auto store = load_store(paths);
    for (const auto* connection : mgr.list_for_project(store.active_project_id)) {
        const auto& c = *connection;
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(c.id);
        row["service_id"] = scyllagpt::Json::string(c.service_id);
        row["display_name"] = scyllagpt::Json::string(scyllagpt::utf8(c.display_name));
        row["connection_name"] = scyllagpt::Json::string(scyllagpt::utf8(c.connection_name));
        row["endpoint_or_cmd"] = scyllagpt::Json::string(scyllagpt::utf8(c.endpoint_or_cmd));
        row["transport"] = scyllagpt::Json::string(scyllagpt::mcp_transport_name(c.transport_kind));
        row["enabled"] = scyllagpt::Json::boolean(c.enabled);
        row["disconnected"] = scyllagpt::Json::boolean(c.disconnected);
        row["auth_state"] = scyllagpt::Json::string(scyllagpt::mcp_auth_state_name(c.auth_state));
        row["last_error"] = scyllagpt::Json::string(c.last_error);
        row["agent_alias"] = scyllagpt::Json::string(c.agent_alias);
        auto scopes = scyllagpt::Json::array();
        for (const auto& scope : c.oauth_scopes) scopes.push(scyllagpt::Json::string(scope));
        row["oauth_scopes"] = std::move(scopes);
        row["scopes_selected"] = scyllagpt::Json::boolean(c.scopes_selected);
        row["has_authenticated"] = scyllagpt::Json::boolean(c.has_authenticated);
        if (c.auth_state == scyllagpt::McpAuthState::Healthy) {
            scyllagpt::McpOAuthTokens tokens;
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (!scyllagpt::mcp_oauth_cred_load(c.id, &tokens)) row["auth_state"] = scyllagpt::Json::string("needs_reauth");
            else if (tokens.expires_at_unix > 0 && tokens.expires_at_unix <= now) row["auth_state"] = scyllagpt::Json::string("expired");
        }
        arr.push(std::move(row));
    }
    return write_utf8(buf, buf_len, arr.dump());
}

int scylla_core_file_mentions(const char* query, char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    auto store = load_store(paths);
    scyllagpt::KnowledgeStore knowledge;
    knowledge.load(paths.knowledge_path);
    auto settings = scyllagpt::load_settings(paths.settings_path);
    const auto* project = ensure_active_project(store, settings);
    std::vector<std::string> scopes;
    for (const auto& candidate : store.projects)
        if (store.contains_open_project(candidate.id)) scopes.push_back(candidate.id);
    const auto files = scyllagpt::agent_file_catalog(knowledge, store.active_project_id,
        project ? project->root : std::wstring{}, store.open_roots(), scopes);
    auto result = scyllagpt::Json::array();
    for (const auto& file : scyllagpt::match_agent_files(files, scyllagpt::utf16(query ? query : "")))
        result.push(scyllagpt::Json::string(scyllagpt::utf8(scyllagpt::agent_file_reference(
            file.path, project ? project->root : std::wstring{}))));
    return write_utf8(buf, buf_len, result.dump());
}

int scylla_core_security_json(char* buf, int buf_len) {
    std::lock_guard lock(g_security_mutex);
    const auto paths = scyllagpt::make_paths();
    auto store = load_store(paths);
    auto settings = scyllagpt::load_settings(paths.settings_path);
    ensure_active_project(store, settings);
    auto result = scyllagpt::Json::object();
    result["exists"] = scyllagpt::Json::boolean(scyllagpt::Keyring::app_vault_exists());
    result["unlocked"] = scyllagpt::Json::boolean(g_app_keyring.is_unlocked());
    result["projectId"] = scyllagpt::Json::string(store.active_project_id);
    auto refs = scyllagpt::Json::array();
    for (const auto& ref : g_app_keyring.list_refs_for_ui(store.active_project_id)) {
        auto row = scyllagpt::Json::object();
        row["name"] = scyllagpt::Json::string(ref.name);
        row["description"] = scyllagpt::Json::string(ref.description);
        row["scope"] = scyllagpt::Json::string(ref.scope == scyllagpt::SecretScope::Global ? "global" : "project");
        refs.push(std::move(row));
    }
    result["items"] = std::move(refs);
    scyllagpt::ProjectConnectionManager connections;
    result["connectionsLoaded"] = scyllagpt::Json::boolean(connections.load(paths.connections_path));
    auto rows = scyllagpt::Json::array();
    for (const auto* connection : connections.for_project(store.active_project_id))
        rows.push(scyllagpt::project_connection_json(*connection));
    result["connections"] = std::move(rows);
    return write_utf8(buf, buf_len, result.dump());
}

int scylla_core_security_action(const char* action, const char* metadata, const char* value,
                               char* error, int error_len) {
    std::lock_guard lock(g_security_mutex);
    auto fail = [&](const std::string& message) { write_utf8(error, error_len, message); return 0; };
    const std::string operation = action ? action : "";
    std::string parse_error;
    const auto data = scyllagpt::Json::parse(metadata ? metadata : "{}", &parse_error);
    if (!parse_error.empty() || !data.is_object()) return fail("Invalid settings request.");
    const auto paths = scyllagpt::make_paths();
    auto store = load_store(paths);
    auto settings = scyllagpt::load_settings(paths.settings_path);
    ensure_active_project(store, settings);
    using scyllagpt::KeyringStatus;
    KeyringStatus status = KeyringStatus::Ok;
    const std::string_view secret(value ? value : "");
    if (operation == "lock") g_app_keyring.lock();
    else if (operation == "create" || operation == "unlock") {
        if (secret.empty()) return fail("Enter a passphrase.");
        if (operation == "create") {
            status = scyllagpt::Keyring::create_app(secret);
            if (status != KeyringStatus::Ok) return fail(scyllagpt::keyring_status_string(status));
        }
        status = g_app_keyring.open_app();
        if (status == KeyringStatus::Ok) status = g_app_keyring.unlock(secret);
    } else if (operation == "save_item") {
        const bool scoped = data.at("scope").as_string("") == "project";
        for (const auto& ref : g_app_keyring.list_refs()) {
            if (ref.name == data.at("name").as_string("") &&
                (ref.scope != (scoped ? scyllagpt::SecretScope::Project : scyllagpt::SecretScope::Global) ||
                 (scoped && ref.project_id != store.active_project_id)))
                return fail("Use a unique reference name; that name exists in another scope.");
        }
        if (scoped && store.active_project_id.empty()) return fail("Open a project first.");
        if (secret.empty()) return fail("Enter a value. Existing values are never loaded into this form.");
        status = g_app_keyring.add_secret(data.at("name").as_string(""), secret,
            data.at("description").as_string(""), scoped ? scyllagpt::SecretScope::Project : scyllagpt::SecretScope::Global,
            scoped ? store.active_project_id : "");
    } else if (operation == "remove_item") {
        // Only allow removal of metadata visible in this project's settings.
        const auto name = data.at("name").as_string("");
        const auto refs = g_app_keyring.list_refs_for_ui(store.active_project_id);
        if (std::none_of(refs.begin(), refs.end(), [&](const auto& ref) { return ref.name == name; }))
            return fail("Item is unavailable in this project or the keyring is locked.");
        const auto scope = data.at("scope").as_string("") == "global" ?
            scyllagpt::SecretScope::Global : scyllagpt::SecretScope::Project;
        status = g_app_keyring.remove_secret(name, scope, store.active_project_id);
    } else if (operation == "save_connection" || operation == "remove_connection") {
        if (store.active_project_id.empty()) return fail("Open a project first.");
        scyllagpt::ProjectConnectionManager connections;
        if (!connections.load(paths.connections_path)) return fail("Could not load connections; nothing was overwritten.");
        const auto id = data.at("id").as_string("");
        if (const auto* existing = connections.find(id); existing && existing->project_id != store.active_project_id)
            return fail("Connection belongs to another project.");
        if (operation == "remove_connection") {
            if (!connections.remove(store.active_project_id, id)) return fail("Connection not found.");
        } else {
            const auto engine = data.at("database").at("engine").as_string("");
            if (data.at("kind").as_string("database") != "ssh" && engine != "mysql" && engine != "postgresql" && engine != "sql-server" && engine != "mongodb")
                return fail("This database engine is not supported by the connection store.");
            auto connection = scyllagpt::project_connection_from_json(data);
            connection.project_id = store.active_project_id;
            if (!connections.upsert(std::move(connection), &parse_error)) return fail(parse_error);
        }
        if (!connections.save(paths.connections_path)) return fail("Could not save connections.");
    } else return fail("Unknown security action.");
    if (status != KeyringStatus::Ok) return fail(scyllagpt::keyring_status_string(status));
    write_utf8(error, error_len, "");
    return 1;
}

int scylla_core_mcp_scopes_json(const char* endpoint, char* buf, int buf_len) {
    std::string error;
    auto result = scyllagpt::Json::object();
    auto scopes = scyllagpt::Json::array();
    for (const auto& scope : scyllagpt::mcp_oauth_discover_scopes(scyllagpt::utf16(endpoint ? endpoint : ""), &error))
        scopes.push(scyllagpt::Json::string(scope));
    result["scopes"] = std::move(scopes);
    result["message"] = scyllagpt::Json::string(error);
    return write_utf8(buf, buf_len, result.dump());
}

int scylla_core_mcp_catalog_json(char* buf, int buf_len) {
    auto result = scyllagpt::Json::array();
    for (const auto& item : scyllagpt::McpManager::known_templates()) {
        if (item.service_id == "workspace-knowledge") continue; // Native STRATA has its own settings.
        auto row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(item.service_id);
        row["name"] = scyllagpt::Json::string(item.display_name);
        row["endpoint"] = scyllagpt::Json::string(item.suggested_endpoint_or_cmd);
        auto scopes = scyllagpt::Json::array();
        for (const auto& scope : scyllagpt::McpManager::suggested_oauth_scopes(item.service_id)) scopes.push(scyllagpt::Json::string(scope));
        row["scopes"] = std::move(scopes);
        result.push(std::move(row));
    }
    return write_utf8(buf, buf_len, result.dump());
}

int scylla_core_mcp_action(const char* action, const char* json_utf8, char* err_utf8, int err_len) {
    if (!action) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null action");
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    scyllagpt::McpManager mgr;
    if (!mgr.load(paths.mcp_path)) {
        write_utf8(err_utf8, err_len, "Could not load MCP connections; nothing was overwritten."); return 0;
    }
    std::string err;
    scyllagpt::Json payload = json_utf8 ? scyllagpt::Json::parse(json_utf8, &err) : scyllagpt::Json::object();
    if (!err.empty()) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, err);
        return 0;
    }
    const std::string act = action;
    if (payload.has("oauth_scopes")) {
        if (!payload.at("oauth_scopes").is_array()) { write_utf8(err_utf8, err_len, "Scopes must be a list."); return 0; }
        for (const auto& scope : payload.at("oauth_scopes").array_items()) {
            const auto value = scope.as_string("");
            if (!scope.is_string() || value.empty() || value.find_first_of(" \t\r\n\"\\") != std::string::npos) {
                write_utf8(err_utf8, err_len, "Each scope must be a single OAuth scope name."); return 0;
            }
        }
    }
    if (act == "authorize" || act == "check" || act == "token") {
        auto* connection = mgr.by_id(payload.at("id").as_string(""));
        if (!connection || connection->transport_kind != scyllagpt::McpTransportKind::Http) {
            write_utf8(err_utf8, err_len, "Select an existing HTTP MCP connection."); return 0;
        }
        const auto id = connection->id;
        const auto endpoint = connection->endpoint_or_cmd;
        const auto scopes = connection->oauth_scopes;
        if (act != "check" && !connection->scopes_selected) {
            write_utf8(err_utf8, err_len, "Select scopes or explicitly choose provider consent first."); return 0;
        }
        scyllagpt::McpOAuthResult auth;
        if (act == "token") {
            const auto token = payload.at("token").as_string("");
            if (token.empty() || token.find_first_of("\r\n") != std::string::npos) {
                write_utf8(err_utf8, err_len, "Enter a valid access token."); return 0;
            }
            auth = scyllagpt::mcp_oauth_probe(endpoint, token);
            if (auth.ok) {
                auth.tokens.access_token = token;
                auth.ok = scyllagpt::mcp_oauth_cred_save(id, auth.tokens);
                if (!auth.ok) { auth.state = scyllagpt::McpAuthState::NeedsReauth; auth.message = "Could not store token securely."; }
            }
        } else auth = act == "authorize"
            ? scyllagpt::mcp_oauth_authorize(nullptr, endpoint, id, 300000, scopes)
            : scyllagpt::mcp_oauth_check_now(endpoint, id);
        // Browser authorization may take minutes: preserve settings edited meanwhile.
        if (!mgr.load(paths.mcp_path) || !mgr.by_id(id)) {
            scyllagpt::mcp_oauth_cred_clear(id);
            write_utf8(err_utf8, err_len, "Connection was removed during authorization."); return 0;
        }
        if (mgr.by_id(id)->endpoint_or_cmd != endpoint || mgr.by_id(id)->oauth_scopes != scopes || !mgr.by_id(id)->enabled) {
            scyllagpt::mcp_oauth_cred_clear(id);
            mgr.mark_auth(id, scyllagpt::McpAuthState::NeedsReauth, "Settings changed during authorization. Reauthenticate.");
            mgr.save(paths.mcp_path);
            write_utf8(err_utf8, err_len, "Settings changed during authorization. Reauthenticate."); return 0;
        }
        mgr.mark_auth(id, auth.state, auth.message);
        if (!mgr.save(paths.mcp_path) || !auth.ok) {
            write_utf8(err_utf8, err_len, auth.message);
            return 0;
        }
        return 1;
    }
    if (act == "add" || act == "add_catalog") {
        scyllagpt::McpConnection c;
        c.service_id = payload.has("service_id") ? payload.at("service_id").as_string("custom") : "custom";
        c.display_name = scyllagpt::utf16(payload.has("display_name") ? payload.at("display_name").as_string("MCP") : "MCP");
        c.connection_name = scyllagpt::utf16(payload.has("connection_name") ? payload.at("connection_name").as_string("") : "");
        c.endpoint_or_cmd = scyllagpt::utf16(payload.has("endpoint_or_cmd") ? payload.at("endpoint_or_cmd").as_string("") : "");
        c.transport_kind = scyllagpt::mcp_transport_from_name(
            payload.has("transport") ? payload.at("transport").as_string("http") : "http");
        c.enabled = !payload.has("enabled") || payload.at("enabled").as_bool(true);
        if (act == "add_catalog") {
            bool found = false;
            for (const auto& item : scyllagpt::McpManager::known_templates()) {
                if (item.service_id != c.service_id) continue;
                c = scyllagpt::McpManager::from_template(item, c.connection_name);
                found = true;
                break;
            }
            if (!found) { write_utf8(err_utf8, err_len, "Unknown catalog service"); return 0; }
        }
        c.id = payload.at("id").as_string(c.id.c_str());
        if (!c.id.empty() && mgr.by_id(c.id)) { write_utf8(err_utf8, err_len, "Duplicate connection id."); return 0; }
        c.connection_name = scyllagpt::utf16(payload.at("connection_name").as_string(""));
        c.account_label.clear(); // A nickname is not a verified provider username.
        if (payload.has("endpoint_or_cmd")) c.endpoint_or_cmd = scyllagpt::utf16(payload.at("endpoint_or_cmd").as_string());
        for (const auto& scope : payload.at("oauth_scopes").array_items())
            if (scope.is_string()) c.oauth_scopes.push_back(scope.as_string());
        c.scopes_selected = payload.at("scopes_selected").as_bool(false);
        auto store = load_store(paths);
        if (store.active_project_id.empty()) {
            write_utf8(err_utf8, err_len, "Open a project before adding an MCP connection."); return 0;
        }
        c.project_scope = {store.active_project_id};
        c.policy.read = scyllagpt::McpApprovalMode::Ask;
        c.agent_alias = scyllagpt::McpManager::normalize_alias(payload.at("agent_alias").as_string(""));
        if (!c.agent_alias.empty() && (!scyllagpt::McpManager::is_valid_alias(c.agent_alias) || mgr.resolve_alias(c.agent_alias))) {
            write_utf8(err_utf8, err_len, "Alias is invalid or already in use."); return 0;
        }
        if (!mgr.add(c)) {
            if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "add failed");
            return 0;
        }
    } else if (act == "remove") {
        const std::string id = payload.has("id") ? payload.at("id").as_string("") : "";
        if (!mgr.remove(id)) {
            if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "remove failed");
            return 0;
        }
    } else if (act == "set_enabled") {
        const std::string id = payload.has("id") ? payload.at("id").as_string("") : "";
        const bool en = payload.has("enabled") && payload.at("enabled").as_bool(true);
        mgr.set_enabled(id, en);
    } else if (act == "disconnect") {
        const std::string id = payload.has("id") ? payload.at("id").as_string("") : "";
        mgr.disconnect(id);
    } else if (act == "update") {
        const std::string id = payload.has("id") ? payload.at("id").as_string("") : "";
        auto* c = mgr.by_id(id);
        if (!c) {
            if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "not found");
            return 0;
        }
        const auto old_endpoint = c->endpoint_or_cmd;
        const auto old_scopes = c->oauth_scopes;
        if (payload.has("oauth_scopes")) {
            c->oauth_scopes.clear();
            for (const auto& scope : payload.at("oauth_scopes").array_items())
                if (scope.is_string()) c->oauth_scopes.push_back(scope.as_string());
        }
        if (payload.has("scopes_selected")) c->scopes_selected = payload.at("scopes_selected").as_bool(false);
        if (payload.has("agent_alias") && !mgr.set_alias(id, payload.at("agent_alias").as_string(), &err)) {
            write_utf8(err_utf8, err_len, err); return 0;
        }
        if (payload.has("display_name") && payload.at("display_name").is_string())
            c->display_name = scyllagpt::utf16(payload.at("display_name").as_string());
        if (payload.has("connection_name") && payload.at("connection_name").is_string())
            c->connection_name = scyllagpt::utf16(payload.at("connection_name").as_string());
        if (payload.has("endpoint_or_cmd") && payload.at("endpoint_or_cmd").is_string())
            c->endpoint_or_cmd = scyllagpt::utf16(payload.at("endpoint_or_cmd").as_string());
        if (payload.has("transport") && payload.at("transport").is_string())
            c->transport_kind = scyllagpt::mcp_transport_from_name(payload.at("transport").as_string());
        if (payload.has("enabled") && payload.at("enabled").is_bool()) c->enabled = payload.at("enabled").as_bool();
        if (old_endpoint != c->endpoint_or_cmd || old_scopes != c->oauth_scopes) mgr.disconnect(id);
    } else {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "unknown action");
        return 0;
    }
    if (!mgr.save(paths.mcp_path)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
        return 0;
    }
    return 1;
}

int scylla_core_strata_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    scyllagpt::StrataClient client;
    client.load(paths.strata_path);
    scyllagpt::Json j = scyllagpt::Json::object();
    j["endpoint"] = scyllagpt::Json::string(scyllagpt::utf8(client.settings().endpoint));
    j["enabled"] = scyllagpt::Json::boolean(client.settings().enabled);
    j["mode"] = scyllagpt::Json::string(client.settings().team ? "team" : "solo");
    j["has_bearer"] = scyllagpt::Json::boolean(!client.settings().bearer.empty());
    scyllagpt::Json arr = scyllagpt::Json::array();
    for (const auto& b : client.bindings()) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["project_id"] = scyllagpt::Json::string(b.project_id);
        row["org"] = scyllagpt::Json::string(b.org);
        row["client"] = scyllagpt::Json::string(b.client);
        row["strata_project"] = scyllagpt::Json::string(b.strata_project);
        row["repo"] = scyllagpt::Json::string(b.repo);
        arr.push(std::move(row));
    }
    j["bindings"] = std::move(arr);
    auto launch = scyllagpt::discover_strata_bridge_launch();
    j["bridge_exe"] = scyllagpt::Json::string(scyllagpt::utf8(launch.exe));
    j["discovery_note"] = scyllagpt::Json::string(launch.discovery_note);
    return write_utf8(buf, buf_len, j.dump());
}

int scylla_core_strata_action(const char* action, const char* json_utf8, char* buf, int buf_len, char* err_utf8,
                              int err_len) {
    if (!action) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null action");
        return -1;
    }
    const auto paths = scyllagpt::make_paths();
    scyllagpt::StrataClient client;
    client.load(paths.strata_path);
    std::string err;
    scyllagpt::Json payload = json_utf8 ? scyllagpt::Json::parse(json_utf8, &err) : scyllagpt::Json::object();
    if (!err.empty()) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, err);
        return -1;
    }
    const std::string act = action;
    if (act == "save") {
        if (payload.has("enabled"))
            client.settings().enabled = payload.at("enabled").as_bool(false);
        if (payload.has("mode"))
            client.settings().team = payload.at("mode").as_string("solo") == "team";
        if (payload.has("endpoint") && payload.at("endpoint").is_string())
            client.settings().endpoint = scyllagpt::utf16(payload.at("endpoint").as_string());
        if (payload.has("bearer") && payload.at("bearer").is_string())
            client.settings().bearer = scyllagpt::utf16(payload.at("bearer").as_string());
        if (payload.has("binding") && payload.at("binding").is_object()) {
            const auto& b = payload.at("binding");
            scyllagpt::StrataBinding binding;
            binding.project_id = b.has("project_id") ? b.at("project_id").as_string("") : "";
            binding.org = b.has("org") ? b.at("org").as_string("") : "";
            binding.client = b.has("client") ? b.at("client").as_string("") : "";
            binding.strata_project = b.has("strata_project") ? b.at("strata_project").as_string("") : "";
            binding.repo = b.has("repo") ? b.at("repo").as_string("") : "";
            if (!binding.project_id.empty()) client.upsert_binding(binding);
        }
        if (!client.save(paths.strata_path)) {
            if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save failed");
            return -1;
        }
        return scylla_core_strata_json(buf, buf_len);
    }
    if (act == "credential") {
        scyllagpt::Json j = scyllagpt::Json::object();
        j["bearer"] = scyllagpt::Json::string(scyllagpt::utf8(client.settings().bearer));
        return write_utf8(buf, buf_len, j.dump());
    }
    if (act == "test") {
        auto health = client.health_check();
        scyllagpt::Json j = scyllagpt::Json::object();
        j["ok"] = scyllagpt::Json::boolean(health.ok);
        j["http_status"] = scyllagpt::Json::number(health.http_status);
        j["message"] = scyllagpt::Json::string(health.message);
        return write_utf8(buf, buf_len, j.dump());
    }
    if (act == "search") {
        const std::string q = payload.has("q") ? payload.at("q").as_string("") : "";
        auto store = load_store(paths);
        const auto projects = scyllagpt::open_project_names(store);
        scyllagpt::Json j = scyllagpt::Json::object();
        bool ok = true;
        std::string message = projects.empty() ? "Open a project to search STRATA." : "";
        scyllagpt::Json hits = scyllagpt::Json::array();
        for (const auto& project : projects) {
            const auto result = client.search(q, 20, project);
            ok = ok && result.ok;
            if (!result.ok) message += result.message + " ";
            for (const auto& h : result.hits) {
                if (scyllagpt::project_key(scyllagpt::utf16(h.project)) != scyllagpt::utf16(project)) continue;
                scyllagpt::Json row = scyllagpt::Json::object();
                row["id"] = scyllagpt::Json::string(h.id);
                row["title"] = scyllagpt::Json::string(h.title);
                row["path"] = scyllagpt::Json::string(h.path);
                row["kind"] = scyllagpt::Json::string(h.kind);
                row["project"] = scyllagpt::Json::string(h.project);
                hits.push(std::move(row));
            }
        }
        j["ok"] = scyllagpt::Json::boolean(ok);
        j["message"] = scyllagpt::Json::string(message);
        j["hits"] = std::move(hits);
        return write_utf8(buf, buf_len, j.dump());
    }
    if (act == "status") {
        scyllagpt::StrataBridge bridge;
        std::wstring bridge_err;
        scyllagpt::Json j = scyllagpt::Json::object();
        if (!bridge.start(&bridge_err)) {
            j["ok"] = scyllagpt::Json::boolean(false);
            j["message"] = scyllagpt::Json::string(scyllagpt::utf8(bridge_err));
            return write_utf8(buf, buf_len, j.dump());
        }
        auto st = bridge.status();
        j["ok"] = scyllagpt::Json::boolean(st.ok);
        j["message"] = scyllagpt::Json::string(st.message);
        j["index_exists"] = scyllagpt::Json::boolean(st.index_exists);
        j["total"] = scyllagpt::Json::number(static_cast<double>(st.total));
        j["workspace_root"] = scyllagpt::Json::string(st.workspace_root);
        bridge.stop();
        return write_utf8(buf, buf_len, j.dump());
    }
    if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "unknown action");
    return -1;
}

int scylla_core_terminal_profiles_json(char* buf, int buf_len) {
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto discovered = scyllagpt::discover_terminal_profiles();
    auto custom = scyllagpt::load_custom_terminal_profiles(paths.terminals_path);
    auto merged = scyllagpt::merge_terminal_profiles(discovered, custom, settings.terminal_profile_enabled);
    scyllagpt::Json arr = scyllagpt::Json::array();
    for (const auto& p : merged) {
        scyllagpt::Json row = scyllagpt::Json::object();
        row["id"] = scyllagpt::Json::string(p.id);
        row["name"] = scyllagpt::Json::string(scyllagpt::utf8(p.name));
        row["executable"] = scyllagpt::Json::string(scyllagpt::utf8(p.executable));
        row["args"] = scyllagpt::Json::string(scyllagpt::utf8(p.args));
        row["source"] = scyllagpt::Json::string(scyllagpt::terminal_profile_source_string(p.source));
        row["enabled"] = scyllagpt::Json::boolean(p.enabled);
        row["is_default"] = scyllagpt::Json::boolean(p.id == settings.default_terminal_profile_id);
        row["agent_policy"] = scyllagpt::Json::string(scyllagpt::terminal_agent_policy(
            settings.terminal_profile_policy, p.id, settings.agent_terminal_policy));
        arr.push(std::move(row));
    }
    scyllagpt::Json root = scyllagpt::Json::object();
    root["default_id"] = scyllagpt::Json::string(settings.default_terminal_profile_id);
    root["agent_terminal_policy"] = scyllagpt::Json::string(settings.agent_terminal_policy);
    root["profiles"] = std::move(arr);
    return write_utf8(buf, buf_len, root.dump());
}

int scylla_core_terminal_profiles_save(const char* json_utf8, char* err_utf8, int err_len) {
    if (!json_utf8) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null json");
        return 0;
    }
    std::string err;
    auto patch = scyllagpt::Json::parse(json_utf8, &err);
    if (!err.empty() || !patch.is_object()) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, err.empty() ? "invalid json" : err);
        return 0;
    }
    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    if (patch.has("default_id") && patch.at("default_id").is_string())
        settings.default_terminal_profile_id = patch.at("default_id").as_string();
    if (patch.has("agent_terminal_policy") && patch.at("agent_terminal_policy").is_string()) {
        settings.agent_terminal_policy = patch.at("agent_terminal_policy").as_string();
        if (settings.agent_terminal_policy != "allow" && settings.agent_terminal_policy != "block")
            settings.agent_terminal_policy = "ask";
    }
    if (patch.has("enabled") && patch.at("enabled").is_object()) {
        for (const auto& kv : patch.at("enabled").object_items()) {
            if (kv.second.is_bool()) settings.terminal_profile_enabled[kv.first] = kv.second.as_bool();
        }
    }
    if (patch.has("policies")) {
        if (!patch.at("policies").is_object()) { write_utf8(err_utf8, err_len, "Invalid terminal policies."); return 0; }
        for (const auto& [id, value] : patch.at("policies").object_items()) {
            if (!value.is_string() || !scyllagpt::is_valid_agent_terminal_policy(value.as_string())) {
                write_utf8(err_utf8, err_len, "Terminal policy must be block, ask or allow."); return 0;
            }
            settings.terminal_profile_policy[id] = scyllagpt::normalize_agent_terminal_policy(value.as_string());
        }
    }
    if (!scyllagpt::save_settings(paths.settings_path, settings)) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "save_settings failed");
        return 0;
    }
    return 1;
}

struct scylla_terminal {
    std::unique_ptr<scyllagpt::TerminalHost> host;
};

scylla_terminal* scylla_terminal_create(void* parent_hwnd, void* notify_hwnd, int control_id,
                                        const char* cwd_utf8, char* err_utf8, int err_len) {
    return scylla_terminal_create_profile(parent_hwnd, notify_hwnd, control_id, cwd_utf8, nullptr, err_utf8, err_len);
}

scylla_terminal* scylla_terminal_create_profile(void* parent_hwnd, void* notify_hwnd, int control_id,
    const char* cwd_utf8, const char* profile_id_utf8, char* err_utf8, int err_len) {
    HWND parent = reinterpret_cast<HWND>(parent_hwnd);
    HWND notify = reinterpret_cast<HWND>(notify_hwnd);
    if (!parent) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "null parent");
        return nullptr;
    }
    if (!notify) notify = parent;
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    if (!inst) inst = GetModuleHandleW(nullptr);

    const auto paths = scyllagpt::make_paths();
    auto settings = scyllagpt::load_settings(paths.settings_path);
    auto discovered = scyllagpt::discover_terminal_profiles();
    auto custom = scyllagpt::load_custom_terminal_profiles(paths.terminals_path);
    auto merged = scyllagpt::merge_terminal_profiles(discovered, custom, settings.terminal_profile_enabled);
    const scyllagpt::TerminalProfile* profile =
        scyllagpt::find_default_terminal_profile(merged, settings.default_terminal_profile_id);
    if (profile_id_utf8 && *profile_id_utf8) {
        profile = nullptr;
        for (const auto& candidate : merged)
            if (candidate.enabled && candidate.id == profile_id_utf8) { profile = &candidate; break; }
    }
    if (!profile) {
        if (err_utf8 && err_len > 0) write_utf8(err_utf8, err_len, "no terminal profile");
        return nullptr;
    }

    std::wstring cwd = cwd_utf8 && *cwd_utf8 ? scyllagpt::utf16(cwd_utf8) : settings.project_folder;
    if (cwd.empty()) {
        wchar_t home[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        cwd = home;
    }

    auto term = std::make_unique<scylla_terminal>();
    term->host = std::make_unique<scyllagpt::TerminalHost>();
    std::wstring error;
    if (!term->host->create(parent, notify, control_id, inst, *profile, cwd, nullptr, &error)) {
        if (err_utf8 && err_len > 0)
            write_utf8(err_utf8, err_len, error.empty() ? "terminal create failed" : scyllagpt::utf8(error));
        return nullptr;
    }
    return term.release();
}

void scylla_terminal_destroy(scylla_terminal* term) {
    if (!term) return;
    if (term->host) term->host->destroy();
    delete term;
}

void scylla_terminal_move(scylla_terminal* term, int x, int y, int w, int h) {
    if (!term || !term->host) return;
    term->host->move(x, y, w, h);
}

void scylla_terminal_set_visible(scylla_terminal* term, int visible) {
    if (!term || !term->host) return;
    term->host->set_visible(visible != 0);
}

int scylla_terminal_write_utf8(scylla_terminal* term, const char* text_utf8) {
    if (!term || !term->host || !text_utf8) return 0;
    return term->host->write_utf8(text_utf8) ? 1 : 0;
}

int scylla_terminal_poll(scylla_terminal* term, char* buf, int buf_len) {
    if (!term || !term->host) return -1;
    std::string out;
    term->host->poll(&out);
    return write_utf8(buf, buf_len, out);
}

int scylla_terminal_resize_pixels(scylla_terminal* term, int w, int h) {
    if (!term || !term->host) return 0;
    return term->host->resize_pixels(w, h) ? 1 : 0;
}

int scylla_terminal_running(scylla_terminal* term) {
    if (!term || !term->host) return 0;
    return term->host->running() ? 1 : 0;
}

}  // extern "C"
