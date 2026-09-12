#include "scyllagpt/mcp_manager.h"

#include "scyllagpt/json.h"
#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/store.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cctype>

namespace scyllagpt {
namespace {

std::string read_all(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return {};
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    return raw;
}

bool write_all(const std::wstring& path, const std::string& body) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

Json policy_to_json(const McpApprovalPolicy& p) {
    Json j = Json::object();
    j["read"] = Json::string(mcp_approval_name(p.read));
    j["write"] = Json::string(mcp_approval_name(p.write));
    j["destructive"] = Json::string(mcp_approval_name(p.destructive));
    j["unknown"] = Json::string(mcp_approval_name(p.unknown));
    return j;
}

McpApprovalPolicy policy_from_json(const Json& j) {
    McpApprovalPolicy p;
    if (!j.is_object()) {
        return p;
    }
    p.read = mcp_approval_from_name(j.at("read").as_string("auto"));
    p.write = mcp_approval_from_name(j.at("write").as_string("ask"));
    p.destructive = mcp_approval_from_name(j.at("destructive").as_string("ask"));
    p.unknown = mcp_approval_from_name(j.at("unknown").as_string("ask"));
    return p;
}

Json connection_to_json(const McpConnection& c) {
    Json j = Json::object();
    j["id"] = Json::string(c.id);
    j["service_id"] = Json::string(c.service_id);
    j["display_name"] = Json::string(utf8(c.display_name));
    j["connection_name"] = Json::string(utf8(c.connection_name));
    j["account_label"] = Json::string(utf8(c.account_label));
    j["agent_alias"] = Json::string(c.agent_alias);
    j["transport_kind"] = Json::string(mcp_transport_name(c.transport_kind));
    j["endpoint_or_cmd"] = Json::string(utf8(c.endpoint_or_cmd));
    Json oauth_scopes = Json::array();
    for (const auto& value : c.oauth_scopes) oauth_scopes.push(Json::string(value));
    j["oauth_scopes"] = std::move(oauth_scopes);
    j["scopes_selected"] = Json::boolean(c.scopes_selected);
    j["has_authenticated"] = Json::boolean(c.has_authenticated);
    Json args = Json::array();
    for (const auto& arg : c.arguments) args.push(Json::string(utf8(arg)));
    j["arguments"] = std::move(args);
    Json env = Json::object();
    for (const auto& [name, value] : c.environment) env[utf8(name)] = Json::string(utf8(value));
    j["environment"] = std::move(env);
    j["enabled"] = Json::boolean(c.enabled);
    j["disconnected"] = Json::boolean(c.disconnected);
    j["auth_state"] = Json::string(mcp_auth_state_name(c.auth_state));
    j["last_checked_iso"] = Json::string(c.last_checked_iso);
    j["last_error"] = Json::string(c.last_error);
    Json scope = Json::array();
    for (const auto& pid : c.project_scope) {
        scope.push(Json::string(pid));
    }
    j["project_scope"] = std::move(scope);
    j["policy"] = policy_to_json(c.policy);
    Json tools = Json::array();
    for (const auto& t : c.tools) {
        Json tj = Json::object();
        tj["name"] = Json::string(t.name);
        tj["title"] = Json::string(t.title);
        tj["classification"] = Json::string(mcp_tool_class_name(t.classification));
        tj["enabled"] = Json::boolean(t.enabled);
        tools.push(std::move(tj));
    }
    j["tools"] = std::move(tools);
    return j;
}

McpConnection connection_from_json(const Json& j) {
    McpConnection c;
    c.id = j.at("id").as_string("");
    c.service_id = j.at("service_id").as_string("");
    c.display_name = utf16(j.at("display_name").as_string());
    c.connection_name = utf16(j.at("connection_name").as_string());
    if (c.connection_name.empty()) {
        c.connection_name = c.display_name;
    }
    c.account_label = utf16(j.at("account_label").as_string());
    c.agent_alias = McpManager::normalize_alias(j.at("agent_alias").as_string());
    c.transport_kind = mcp_transport_from_name(j.at("transport_kind").as_string("http"));
    c.endpoint_or_cmd = utf16(j.at("endpoint_or_cmd").as_string());
    for (const auto& value : j.at("oauth_scopes").array_items())
        if (value.is_string()) c.oauth_scopes.push_back(value.as_string());
    c.scopes_selected = j.at("scopes_selected").as_bool(false);
    if (const Json& args = j.at("arguments"); args.is_array()) {
        for (const auto& arg : args.array_items()) c.arguments.push_back(utf16(arg.as_string()));
    }
    if (const Json& env = j.at("environment"); env.is_object()) {
        for (const auto& [name, value] : env.object_items())
            c.environment.emplace_back(utf16(name), utf16(value.as_string()));
    }
    c.enabled = j.at("enabled").is_null() ? true : j.at("enabled").as_bool(true);
    c.disconnected = j.at("disconnected").is_null() ? false : j.at("disconnected").as_bool(false);
    c.auth_state = mcp_auth_state_from_name(j.at("auth_state").as_string("unknown"));
    c.has_authenticated = j.at("has_authenticated").as_bool(
        c.auth_state == McpAuthState::Healthy || c.auth_state == McpAuthState::Expired || c.disconnected);
    c.last_checked_iso = j.at("last_checked_iso").as_string("");
    c.last_error = j.at("last_error").as_string("");
    const Json& scope = j.at("project_scope");
    if (scope.is_array()) {
        for (const Json& item : scope.array_items()) {
            const std::string pid = item.as_string("");
            if (!pid.empty()) {
                c.project_scope.push_back(pid);
            }
        }
    }
    c.policy = policy_from_json(j.at("policy"));
    const Json& tools = j.at("tools");
    if (tools.is_array()) {
        for (const Json& tj : tools.array_items()) {
            if (!tj.is_object()) {
                continue;
            }
            McpToolState t;
            t.name = tj.at("name").as_string("");
            t.title = tj.at("title").as_string("");
            t.classification = mcp_tool_class_from_name(tj.at("classification").as_string("unknown"));
            t.enabled = tj.at("enabled").is_null() ? true : tj.at("enabled").as_bool(true);
            if (!t.name.empty()) {
                c.tools.push_back(std::move(t));
            }
        }
    }
    return c;
}

bool in_scope(const McpConnection& c, const std::string& project_id) {
    if (c.project_scope.empty()) {
        return true;
    }
    for (const auto& pid : c.project_scope) {
        if (pid == project_id) {
            return true;
        }
    }
    return false;
}

bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

}  // namespace

const char* mcp_transport_name(McpTransportKind k) {
    switch (k) {
        case McpTransportKind::Stdio:
            return "stdio";
        case McpTransportKind::Http:
        default:
            return "http";
    }
}

const char* mcp_tool_class_name(McpToolClass c) {
    switch (c) {
        case McpToolClass::Read:
            return "read";
        case McpToolClass::Write:
            return "write";
        case McpToolClass::Destructive:
            return "destructive";
        case McpToolClass::Unknown:
        default:
            return "unknown";
    }
}

const char* mcp_approval_name(McpApprovalMode m) {
    switch (m) {
        case McpApprovalMode::Auto:
            return "auto";
        case McpApprovalMode::Block:
            return "block";
        case McpApprovalMode::Ask:
        default:
            return "ask";
    }
}

const char* mcp_auth_state_name(McpAuthState s) {
    switch (s) {
        case McpAuthState::Healthy:
            return "healthy";
        case McpAuthState::Expired:
            return "expired";
        case McpAuthState::NeedsReauth:
            return "needs_reauth";
        case McpAuthState::Offline:
            return "offline";
        case McpAuthState::Disabled:
            return "disabled";
        case McpAuthState::Unknown:
        default:
            return "unknown";
    }
}

McpTransportKind mcp_transport_from_name(const std::string& s) {
    if (s == "stdio") {
        return McpTransportKind::Stdio;
    }
    return McpTransportKind::Http;
}

McpToolClass mcp_tool_class_from_name(const std::string& s) {
    if (s == "read") {
        return McpToolClass::Read;
    }
    if (s == "write") {
        return McpToolClass::Write;
    }
    if (s == "destructive") {
        return McpToolClass::Destructive;
    }
    return McpToolClass::Unknown;
}

McpApprovalMode mcp_approval_from_name(const std::string& s) {
    if (s == "auto") {
        return McpApprovalMode::Auto;
    }
    if (s == "block") {
        return McpApprovalMode::Block;
    }
    return McpApprovalMode::Ask;
}

McpAuthState mcp_auth_state_from_name(const std::string& s) {
    if (s == "healthy") {
        return McpAuthState::Healthy;
    }
    if (s == "expired") {
        return McpAuthState::Expired;
    }
    if (s == "needs_reauth" || s == "reauth_required") {
        return McpAuthState::NeedsReauth;
    }
    if (s == "offline") {
        return McpAuthState::Offline;
    }
    if (s == "disabled") {
        return McpAuthState::Disabled;
    }
    return McpAuthState::Unknown;
}

bool McpManager::is_valid_alias(const std::string& alias_without_at) {
    if (alias_without_at.empty() || alias_without_at.size() > 64) {
        return false;
    }
    for (char ch : alias_without_at) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

std::string McpManager::normalize_alias(const std::string& alias_with_or_without_at) {
    std::string a = alias_with_or_without_at;
    while (!a.empty() && (a.front() == '@' || a.front() == ' ' || a.front() == '\t')) {
        a.erase(a.begin());
    }
    while (!a.empty() && (a.back() == ' ' || a.back() == '\t')) {
        a.pop_back();
    }
    return a;
}

bool McpManager::load(const std::wstring& path) {
    connections_.clear();
    const std::string raw = read_all(path);
    if (raw.empty()) {
        return true;
    }
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return false;
    }
    const Json& arr = j.at("connections");
    if (!arr.is_array()) {
        return true;
    }
    for (const Json& item : arr.array_items()) {
        if (!item.is_object()) {
            continue;
        }
        McpConnection c = connection_from_json(item);
        if (c.id.empty() || c.service_id.empty()) {
            continue;
        }
        if (!c.agent_alias.empty() && !is_valid_alias(c.agent_alias)) {
            c.agent_alias.clear();
        }
        connections_.push_back(std::move(c));
    }
    return true;
}

bool McpManager::save(const std::wstring& path) const {
    Json arr = Json::array();
    for (const auto& c : connections_) {
        arr.push(connection_to_json(c));
    }
    Json j = Json::object();
    j["version"] = Json::number(1);
    j["connections"] = std::move(arr);
    return write_all(path, j.dump());
}

McpConnection* McpManager::by_id(const std::string& id) {
    for (auto& c : connections_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const McpConnection* McpManager::by_id(const std::string& id) const {
    for (const auto& c : connections_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

McpConnection* McpManager::add(const McpConnection& c) {
    McpConnection copy = c;
    if (copy.id.empty()) {
        copy.id = make_uuid();
    }
    if (copy.id.empty() || copy.service_id.empty()) {
        return nullptr;
    }
    if (by_id(copy.id)) {
        return nullptr;
    }
    copy.agent_alias = normalize_alias(copy.agent_alias);
    if (!copy.agent_alias.empty()) {
        if (!is_valid_alias(copy.agent_alias)) {
            return nullptr;
        }
        if (resolve_alias(copy.agent_alias)) {
            return nullptr;
        }
    }
    if (copy.connection_name.empty()) {
        copy.connection_name = copy.display_name;
    }
    connections_.push_back(std::move(copy));
    return &connections_.back();
}

bool McpManager::remove(const std::string& id) {
    for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        if (it->id == id) {
            // Drop CredMan tokens with the connection — never leave orphan OAuth grants.
            mcp_oauth_cred_clear(id);
            connections_.erase(it);
            return true;
        }
    }
    return false;
}

bool McpManager::set_alias(const std::string& id, const std::string& alias, std::string* error_out) {
    McpConnection* c = by_id(id);
    if (!c) {
        if (error_out) {
            *error_out = "connection not found";
        }
        return false;
    }
    const std::string normalized = normalize_alias(alias);
    if (normalized.empty()) {
        c->agent_alias.clear();
        return true;
    }
    if (!is_valid_alias(normalized)) {
        if (error_out) {
            *error_out = "alias must match [a-zA-Z0-9_-]+";
        }
        return false;
    }
    for (const auto& other : connections_) {
        if (other.id != id && other.agent_alias == normalized) {
            if (error_out) {
                *error_out = "alias already in use";
            }
            return false;
        }
    }
    c->agent_alias = normalized;
    return true;
}

McpConnection* McpManager::resolve_alias(const std::string& alias_with_or_without_at) {
    const std::string key = normalize_alias(alias_with_or_without_at);
    if (key.empty()) {
        return nullptr;
    }
    for (auto& c : connections_) {
        if (c.agent_alias == key) {
            return &c;
        }
    }
    return nullptr;
}

const McpConnection* McpManager::resolve_alias(const std::string& alias_with_or_without_at) const {
    const std::string key = normalize_alias(alias_with_or_without_at);
    if (key.empty()) {
        return nullptr;
    }
    for (const auto& c : connections_) {
        if (c.agent_alias == key) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<std::pair<std::string, std::wstring>> McpManager::alias_completions(const std::string& prefix) const {
    const std::string p = normalize_alias(prefix);
    std::vector<std::pair<std::string, std::wstring>> out;
    for (const auto& c : connections_) {
        if (c.agent_alias.empty()) {
            continue;
        }
        if (!p.empty() && !starts_with_ci(c.agent_alias, p)) {
            continue;
        }
        std::wstring label = c.connection_name.empty() ? c.display_name : c.connection_name;
        if (label.empty()) {
            label = utf16(c.service_id);
        }
        out.emplace_back(c.agent_alias, std::move(label));
    }
    return out;
}

void McpManager::set_enabled(const std::string& id, bool enabled) {
    if (McpConnection* c = by_id(id)) {
        c->enabled = enabled;
        if (!enabled) {
            c->auth_state = McpAuthState::Disabled;
        } else if (c->auth_state == McpAuthState::Disabled) {
            c->auth_state = c->disconnected ? McpAuthState::NeedsReauth : McpAuthState::Unknown;
        }
    }
}

void McpManager::disconnect(const std::string& id) {
    if (McpConnection* c = by_id(id)) {
        c->disconnected = true;
        c->auth_state = McpAuthState::NeedsReauth;
        c->last_error = "Disconnected — reauthentication required";
        // Disconnect clears Scylla's auth state; the connection config remains.
        mcp_oauth_cred_clear(id);
    }
}

void McpManager::mark_auth(const std::string& id, McpAuthState state, const std::string& err) {
    if (McpConnection* c = by_id(id)) {
        c->auth_state = state;
        c->last_error = err;
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char buf[32]{};
        sprintf_s(buf, "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        c->last_checked_iso = buf;
        if (state == McpAuthState::Healthy) {
            c->has_authenticated = true;
            c->disconnected = false;
            c->enabled = true;
        }
    }
}

std::vector<McpConnection*> McpManager::list_for_project(const std::string& project_id) {
    std::vector<McpConnection*> out;
    for (auto& c : connections_) {
        if (in_scope(c, project_id)) {
            out.push_back(&c);
        }
    }
    return out;
}

std::vector<const McpConnection*> McpManager::list_for_project(const std::string& project_id) const {
    std::vector<const McpConnection*> out;
    for (const auto& c : connections_) {
        if (in_scope(c, project_id)) {
            out.push_back(&c);
        }
    }
    return out;
}

std::vector<McpServiceTemplate> McpManager::known_templates() {
    return {
        {"github", "GitHub", McpTransportKind::Http, "https://api.githubcopilot.com/mcp/", {}, {}},
        {"linear", "Linear", McpTransportKind::Http, "https://mcp.linear.app/mcp", {}, {}},
        {"notion", "Notion", McpTransportKind::Http, "https://mcp.notion.com/mcp", {}, {}},
        {"gitlab", "GitLab", McpTransportKind::Http, "https://gitlab.com/api/v4/mcp", {}, {}},
        {"sentry", "Sentry", McpTransportKind::Http, "https://mcp.sentry.dev/mcp", {}, {}},
        {"supabase", "Supabase", McpTransportKind::Http, "https://mcp.supabase.com/mcp", {}, {}},
        {"atlassian", "Atlassian Rovo", McpTransportKind::Http, "https://mcp.atlassian.com/v2/mcp", {}, {}},
        {"workspace-knowledge", "STRATA Workspace Knowledge", McpTransportKind::Stdio, "python",
         {L"-m", L"cxl_strata.workspace_index.mcp_server"},
         {{L"STRATA_WORKSPACE_ROOT", L"d:/projects"}}},
    };
}

// Verified against each provider's protected resource metadata on 2026-09-11.
// These are unchecked choices, not default grants. Discovery can refresh the list.
std::vector<std::string> McpManager::suggested_oauth_scopes(const std::string& service) {
    if (service == "linear" || service == "linear-readonly") return {"read", "write"};
    if (service == "github") return {"repo", "read:org", "read:user", "user:email", "read:packages", "write:packages", "read:project", "project", "gist", "notifications"};
    if (service == "notion") return {"default"};
    if (service == "gitlab") return {"mcp"};
    if (service == "sentry") return {"org:read", "project:write", "team:write", "event:write"};
    if (service == "supabase") return {"organizations:read", "projects:read", "projects:write", "database:write", "database:read", "analytics:read", "secrets:read", "edge_functions:read", "edge_functions:write", "environment:read", "environment:write", "storage:read", "storage:write"};
    if (service == "atlassian") return {"read:me", "read:account", "offline_access", "email", "read:jira:agent-interface", "write:jira:agent-interface", "search:jira:agent-interface", "delete:jira:agent-interface", "manage:jira:agent-interface", "read:confluence:agent-interface", "write:confluence:agent-interface", "search:confluence:agent-interface", "search:rovo:agent-interface", "search:code:agent-interface", "read:all:twg", "write:all:twg", "read:goals:agent-interface", "write:goals:agent-interface", "read:projects:agent-interface", "write:projects:agent-interface", "read:bitbucket:agent-interface", "write:bitbucket:agent-interface", "read:loom:agent-interface", "write:loom:agent-interface", "read:talent:agent-interface", "write:talent:agent-interface", "read:teams:agent-interface", "write:teams:agent-interface", "read:artifacts:agent-interface", "write:artifacts:agent-interface", "read:focus:agent-interface", "write:focus:agent-interface"};
    return {};
}

McpConnection McpManager::from_template(const McpServiceTemplate& t, const std::wstring& account_label) {
    McpConnection c;
    c.id = make_uuid();
    c.service_id = t.service_id;
    c.display_name = utf16(t.display_name);
    c.account_label = account_label;
    if (!account_label.empty()) {
        c.connection_name = account_label;
        if (!c.display_name.empty()) {
            c.connection_name += L" · ";
            c.connection_name += c.display_name;
        }
    } else {
        c.connection_name = c.display_name;
    }
    c.transport_kind = t.default_transport;
    c.endpoint_or_cmd = utf16(t.suggested_endpoint_or_cmd);
    c.arguments = t.arguments;
    c.environment = t.environment;
    c.enabled = true;
    c.disconnected = false;
    c.auth_state = McpAuthState::Unknown;
    c.policy = McpApprovalPolicy{};
    return c;
}

}  // namespace scyllagpt
