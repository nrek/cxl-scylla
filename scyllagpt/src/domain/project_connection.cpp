#include "scyllagpt/project_connection.h"

#include "scyllagpt/json.h"
#include "scyllagpt/paths.h"

#include <algorithm>
#include <cctype>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>

#pragma comment(lib, "rpcrt4.lib")

namespace scyllagpt {
namespace {

bool read_all(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(file);
    return ok && read == out.size();
}

bool write_all(const std::wstring& path, const std::string& body) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && !ensure_dir(path.substr(0, slash))) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == body.size();
}

const char* route_name(ConnectionRouteType value) {
    switch (value) {
        case ConnectionRouteType::SshTunnel: return "ssh-tunnel";
        case ConnectionRouteType::Direct: return "direct";
        default: return "remote-execution";
    }
}

ConnectionRouteType route_from(std::string_view value) {
    if (value == "ssh-tunnel") return ConnectionRouteType::SshTunnel;
    if (value == "direct") return ConnectionRouteType::Direct;
    return ConnectionRouteType::RemoteExecution;
}

const char* engine_name(DatabaseEngine value) {
    switch (value) {
        case DatabaseEngine::PostgreSql: return "postgresql";
        case DatabaseEngine::SqlServer: return "sql-server";
        default: return "mysql";
    }
}

DatabaseEngine engine_from(std::string_view value) {
    if (value == "postgresql") return DatabaseEngine::PostgreSql;
    if (value == "sql-server") return DatabaseEngine::SqlServer;
    return DatabaseEngine::MySql;
}

const char* authority_name(ConnectionAuthority value) {
    switch (value) {
        case ConnectionAuthority::Ask: return "ask";
        case ConnectionAuthority::Block: return "block";
        default: return "auto";
    }
}

ConnectionAuthority authority_from(std::string_view value, ConnectionAuthority fallback) {
    if (value == "auto") return ConnectionAuthority::Auto;
    if (value == "ask") return ConnectionAuthority::Ask;
    if (value == "block") return ConnectionAuthority::Block;
    return fallback;
}

const char* visibility_name(ResultVisibility value) {
    switch (value) {
        case ResultVisibility::AgentOnly: return "agent-only";
        case ResultVisibility::HumanOnly: return "human-only";
        case ResultVisibility::MetadataOnly: return "metadata-only";
        case ResultVisibility::AggregateOnly: return "aggregate-only";
        default: return "agent-and-human";
    }
}

ResultVisibility visibility_from(std::string_view value) {
    if (value == "agent-only") return ResultVisibility::AgentOnly;
    if (value == "human-only") return ResultVisibility::HumanOnly;
    if (value == "metadata-only") return ResultVisibility::MetadataOnly;
    if (value == "aggregate-only") return ResultVisibility::AggregateOnly;
    return ResultVisibility::AgentAndHuman;
}

std::uint32_t bounded_u32(const Json& value, std::uint32_t fallback, std::uint32_t maximum) {
    const auto raw = value.as_int(fallback);
    if (raw < 0 || raw > maximum) return fallback;
    return static_cast<std::uint32_t>(raw);
}

Json connection_to_json(const ProjectConnection& connection) {
    Json root = Json::object();
    root["id"] = Json::string(connection.id);
    root["projectId"] = Json::string(connection.project_id);
    root["name"] = Json::string(connection.name);
    root["alias"] = Json::string(connection.alias);
    root["enabled"] = Json::boolean(connection.enabled);
    root["routeType"] = Json::string(route_name(connection.route_type));

    Json ssh = Json::object();
    ssh["host"] = Json::string(connection.ssh.host);
    ssh["hostRef"] = Json::string(connection.ssh.host_ref);
    ssh["port"] = Json::number(connection.ssh.port);
    ssh["portRef"] = Json::string(connection.ssh.port_ref);
    ssh["username"] = Json::string(connection.ssh.username);
    ssh["usernameRef"] = Json::string(connection.ssh.username_ref);
    ssh["authRef"] = Json::string(connection.ssh.auth_ref);
    ssh["privateKeyRef"] = Json::string(connection.ssh.private_key_ref);
    ssh["keyPassphraseRef"] = Json::string(connection.ssh.key_passphrase_ref);
    ssh["hostKey"] = Json::string(connection.ssh.host_key);
    ssh["hostKeyFingerprint"] = Json::string(connection.ssh.host_key_fingerprint);
    root["ssh"] = std::move(ssh);

    Json database = Json::object();
    database["engine"] = Json::string(engine_name(connection.database.engine));
    database["host"] = Json::string(connection.database.host);
    database["hostRef"] = Json::string(connection.database.host_ref);
    database["port"] = Json::number(connection.database.port);
    database["portRef"] = Json::string(connection.database.port_ref);
    database["database"] = Json::string(connection.database.database);
    database["usernameRef"] = Json::string(connection.database.username_ref);
    database["passwordRef"] = Json::string(connection.database.password_ref);
    database["tlsCaRef"] = Json::string(connection.database.tls_ca_ref);
    root["database"] = std::move(database);

    Json query = Json::object();
    query["read"] = Json::string(authority_name(connection.query_policy.read));
    query["dataModification"] = Json::string(authority_name(connection.query_policy.data_modification));
    query["schemaModification"] = Json::string(authority_name(connection.query_policy.schema_modification));
    query["administrative"] = Json::string(authority_name(connection.query_policy.administrative));
    query["unrestricted"] = Json::boolean(connection.query_policy.unrestricted);
    query["allowMultipleStatements"] = Json::boolean(connection.query_policy.allow_multiple_statements);
    root["queryPolicy"] = std::move(query);

    Json result = Json::object();
    result["visibility"] = Json::string(visibility_name(connection.result_policy.visibility));
    result["maxRows"] = Json::number(connection.result_policy.max_rows);
    result["maxBytes"] = Json::number(connection.result_policy.max_bytes);
    result["timeoutSeconds"] = Json::number(connection.result_policy.timeout_seconds);
    result["maxTextBytes"] = Json::number(connection.result_policy.max_text_bytes);
    result["includeBinary"] = Json::boolean(connection.result_policy.include_binary);
    root["resultPolicy"] = std::move(result);
    return root;
}

ProjectConnection connection_from_json(const Json& root) {
    ProjectConnection connection;
    connection.id = root.at("id").as_string("");
    connection.project_id = root.at("projectId").as_string("");
    connection.name = root.at("name").as_string("");
    connection.alias = ProjectConnectionManager::normalize_alias(root.at("alias").as_string(""));
    connection.enabled = root.at("enabled").as_bool(true);
    connection.route_type = route_from(root.at("routeType").as_string("remote-execution"));

    const Json& ssh = root.at("ssh");
    connection.ssh.host = ssh.at("host").as_string("");
    connection.ssh.host_ref = ssh.at("hostRef").as_string("");
    connection.ssh.port = static_cast<std::uint16_t>(bounded_u32(ssh.at("port"), 22, 65535));
    connection.ssh.port_ref = ssh.at("portRef").as_string("");
    connection.ssh.username = ssh.at("username").as_string("");
    connection.ssh.username_ref = ssh.at("usernameRef").as_string("");
    connection.ssh.auth_ref = ssh.at("authRef").as_string("");
    connection.ssh.private_key_ref = ssh.at("privateKeyRef").as_string("");
    connection.ssh.key_passphrase_ref = ssh.at("keyPassphraseRef").as_string("");
    connection.ssh.host_key = ssh.at("hostKey").as_string("");
    connection.ssh.host_key_fingerprint = ssh.at("hostKeyFingerprint").as_string("");

    const Json& database = root.at("database");
    connection.database.engine = engine_from(database.at("engine").as_string("mysql"));
    connection.database.host = database.at("host").as_string("");
    connection.database.host_ref = database.at("hostRef").as_string("");
    connection.database.port = static_cast<std::uint16_t>(bounded_u32(database.at("port"), 3306, 65535));
    connection.database.port_ref = database.at("portRef").as_string("");
    connection.database.database = database.at("database").as_string("");
    connection.database.username_ref = database.at("usernameRef").as_string("");
    connection.database.password_ref = database.at("passwordRef").as_string("");
    connection.database.tls_ca_ref = database.at("tlsCaRef").as_string("");

    const Json& query = root.at("queryPolicy");
    connection.query_policy.read = authority_from(query.at("read").as_string("auto"), ConnectionAuthority::Auto);
    connection.query_policy.data_modification = authority_from(query.at("dataModification").as_string("ask"), ConnectionAuthority::Ask);
    connection.query_policy.schema_modification = authority_from(query.at("schemaModification").as_string("ask"), ConnectionAuthority::Ask);
    connection.query_policy.administrative = authority_from(query.at("administrative").as_string("block"), ConnectionAuthority::Block);
    connection.query_policy.unrestricted = query.at("unrestricted").as_bool(false);
    connection.query_policy.allow_multiple_statements = query.at("allowMultipleStatements").as_bool(false);

    const Json& result = root.at("resultPolicy");
    connection.result_policy.visibility = visibility_from(result.at("visibility").as_string("agent-and-human"));
    connection.result_policy.max_rows = bounded_u32(result.at("maxRows"), 500, 1000000);
    connection.result_policy.max_bytes = bounded_u32(result.at("maxBytes"), 2 * 1024 * 1024, 128 * 1024 * 1024);
    connection.result_policy.timeout_seconds = bounded_u32(result.at("timeoutSeconds"), 30, 3600);
    connection.result_policy.max_text_bytes = bounded_u32(result.at("maxTextBytes"), 16 * 1024, 16 * 1024 * 1024);
    connection.result_policy.include_binary = result.at("includeBinary").as_bool(false);
    return connection;
}

}  // namespace

std::string ProjectConnectionManager::make_id() {
    UUID uuid{};
    UuidCreate(&uuid);
    RPC_CSTR text = nullptr;
    if (UuidToStringA(&uuid, &text) != RPC_S_OK || !text) return "connection-" + std::to_string(GetTickCount64());
    std::string id(reinterpret_cast<char*>(text));
    RpcStringFreeA(&text);
    return id;
}

std::string ProjectConnectionManager::normalize_alias(std::string_view alias) {
    while (!alias.empty() && (alias.front() == '@' || std::isspace(static_cast<unsigned char>(alias.front())))) alias.remove_prefix(1);
    while (!alias.empty() && std::isspace(static_cast<unsigned char>(alias.back()))) alias.remove_suffix(1);
    std::string normalized(alias);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

bool ProjectConnectionManager::is_valid_alias(std::string_view alias) {
    const std::string normalized = normalize_alias(alias);
    if (normalized.empty() || normalized.size() > 48 || !std::isalnum(static_cast<unsigned char>(normalized.front()))) return false;
    return std::all_of(normalized.begin(), normalized.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

bool ProjectConnectionManager::validate(const ProjectConnection& connection, std::string* error) {
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (connection.project_id.empty()) return fail("project binding is required");
    if (connection.name.empty()) return fail("connection name is required");
    if (!is_valid_alias(connection.alias)) return fail("alias must be 1-48 letters, numbers, dashes, or underscores");
    if (connection.route_type != ConnectionRouteType::Direct) {
        // Either a literal host or a Keyring name that resolves to one; the same for the username.
        if (connection.ssh.host.empty() && connection.ssh.host_ref.empty()) return fail("SSH host is required");
        if (connection.ssh.port == 0 && connection.ssh.port_ref.empty()) return fail("SSH port is invalid");
        if (connection.ssh.username.empty() && connection.ssh.username_ref.empty())
            return fail("SSH username is required");
        if (connection.ssh.auth_ref.empty() && connection.ssh.private_key_ref.empty())
            return fail("an SSH password or private key reference is required");
        // The pinned public key, not just its fingerprint: known_hosts needs the key itself, and
        // without it StrictHostKeyChecking cannot be enforced.
        if (connection.ssh.host_key.empty()) return fail("pinned SSH host public key is required");
    }
    if (connection.database.host.empty() && connection.database.host_ref.empty())
        return fail("database host is required");
    if (connection.database.port == 0 && connection.database.port_ref.empty())
        return fail("database port is invalid");
    if (connection.database.username_ref.empty() || connection.database.password_ref.empty())
        return fail("database credential references are required");
    if (connection.result_policy.max_rows == 0 || connection.result_policy.max_bytes == 0 ||
        connection.result_policy.timeout_seconds == 0) return fail("result limits must be greater than zero");
    if (error) error->clear();
    return true;
}

bool ProjectConnectionManager::load(const std::wstring& path) {
    connections_.clear();
    if (!file_exists(path)) return true;
    std::string raw;
    if (!read_all(path, raw)) return false;
    std::string error;
    const Json root = Json::parse(raw, &error);
    if (!error.empty() || !root.is_object()) return false;
    const Json& connections = root.at("connections");
    if (!connections.is_array()) return true;
    for (const auto& item : connections.array_items()) {
        ProjectConnection connection = connection_from_json(item);
        std::string validation;
        if (connection.id.empty() || !validate(connection, &validation) || resolve_alias(connection.project_id, connection.alias))
            continue;
        connections_.push_back(std::move(connection));
    }
    return true;
}

bool ProjectConnectionManager::save(const std::wstring& path) const {
    Json connections = Json::array();
    for (const auto& connection : connections_) connections.push(connection_to_json(connection));
    Json root = Json::object();
    root["version"] = Json::number(1);
    root["connections"] = std::move(connections);
    return write_all(path, root.dump());
}

std::vector<const ProjectConnection*> ProjectConnectionManager::for_project(std::string_view project_id) const {
    std::vector<const ProjectConnection*> result;
    for (const auto& connection : connections_) if (connection.project_id == project_id) result.push_back(&connection);
    return result;
}

const ProjectConnection* ProjectConnectionManager::find(std::string_view id) const {
    for (const auto& connection : connections_) if (connection.id == id) return &connection;
    return nullptr;
}

ProjectConnection* ProjectConnectionManager::find_mut(std::string_view id) {
    for (auto& connection : connections_) if (connection.id == id) return &connection;
    return nullptr;
}

const ProjectConnection* ProjectConnectionManager::resolve_alias(std::string_view project_id, std::string_view alias) const {
    const std::string normalized = normalize_alias(alias);
    for (const auto& connection : connections_) {
        if (connection.project_id == project_id && connection.alias == normalized) return &connection;
    }
    return nullptr;
}

bool ProjectConnectionManager::upsert(ProjectConnection connection, std::string* error) {
    connection.alias = normalize_alias(connection.alias);
    if (!validate(connection, error)) return false;
    for (const auto& existing : connections_) {
        if (existing.project_id == connection.project_id && existing.alias == connection.alias && existing.id != connection.id) {
            if (error) *error = "alias is already used in this project";
            return false;
        }
    }
    if (connection.id.empty()) connection.id = make_id();
    if (auto* existing = find_mut(connection.id)) {
        if (existing->project_id != connection.project_id) {
            if (error) *error = "connection project binding cannot be changed";
            return false;
        }
        *existing = std::move(connection);
    } else {
        connections_.push_back(std::move(connection));
    }
    if (error) error->clear();
    return true;
}

bool ProjectConnectionManager::remove(std::string_view project_id, std::string_view id) {
    const auto before = connections_.size();
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [&](const ProjectConnection& connection) {
        return connection.project_id == project_id && connection.id == id;
    }), connections_.end());
    return connections_.size() != before;
}

}  // namespace scyllagpt
