#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

enum class ConnectionRouteType { RemoteExecution = 0, SshTunnel, Direct };
enum class DatabaseEngine { MySql = 0, PostgreSql, SqlServer };
enum class ConnectionAuthority { Auto = 0, Ask, Block };
enum class ResultVisibility { AgentAndHuman = 0, AgentOnly, HumanOnly, MetadataOnly, AggregateOnly };

// All *_ref fields are Keyring secret NAMES, never values. Where a plain field and its _ref are
// both present the reference wins, so a stored connection can carry no infrastructure detail at all.
struct SshConnectionRoute {
    std::string host;
    std::string host_ref;           // Keyring-held hostname (optional; overrides host)
    std::uint16_t port = 22;
    std::string port_ref;           // Keyring-held port (optional; overrides port)
    std::string username;
    std::string username_ref;       // Keyring-held username (optional; overrides username)
    std::string auth_ref;           // password reference (optional when a key is used)
    std::string private_key_ref;    // private key reference (optional when a password is used)
    std::string key_passphrase_ref; // passphrase protecting private_key_ref (optional)
    // Pinned host public key line, e.g. "ssh-ed25519 AAAAC3Nz...". A fingerprint alone cannot
    // drive StrictHostKeyChecking, so the full key is what gets written to known_hosts.
    std::string host_key;
    std::string host_key_fingerprint;  // display only
};

struct DatabaseConnectionTarget {
    DatabaseEngine engine = DatabaseEngine::MySql;
    std::string host;
    std::string host_ref;           // Keyring-held hostname (optional; overrides host)
    std::uint16_t port = 3306;
    std::string port_ref;           // Keyring-held port (optional; overrides port)
    std::string database;
    std::string username_ref;
    std::string password_ref;
    std::string tls_ca_ref;
};

struct ConnectionQueryPolicy {
    ConnectionAuthority read = ConnectionAuthority::Auto;
    ConnectionAuthority data_modification = ConnectionAuthority::Ask;
    ConnectionAuthority schema_modification = ConnectionAuthority::Ask;
    ConnectionAuthority administrative = ConnectionAuthority::Block;
    bool unrestricted = false;
    bool allow_multiple_statements = false;
};

struct ConnectionResultPolicy {
    ResultVisibility visibility = ResultVisibility::AgentAndHuman;
    std::uint32_t max_rows = 500;
    std::uint32_t max_bytes = 2 * 1024 * 1024;
    std::uint32_t timeout_seconds = 30;
    std::uint32_t max_text_bytes = 16 * 1024;
    bool include_binary = false;
};

struct ProjectConnection {
    std::string id;
    std::string project_id;
    std::string name;
    std::string alias;
    ConnectionRouteType route_type = ConnectionRouteType::RemoteExecution;
    SshConnectionRoute ssh;
    DatabaseConnectionTarget database;
    ConnectionQueryPolicy query_policy;
    ConnectionResultPolicy result_policy;
    bool enabled = true;
};

class ProjectConnectionManager {
public:
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    const std::vector<ProjectConnection>& all() const { return connections_; }
    std::vector<const ProjectConnection*> for_project(std::string_view project_id) const;
    const ProjectConnection* find(std::string_view id) const;
    ProjectConnection* find_mut(std::string_view id);
    const ProjectConnection* resolve_alias(std::string_view project_id, std::string_view alias) const;

    bool upsert(ProjectConnection connection, std::string* error = nullptr);
    bool remove(std::string_view project_id, std::string_view id);

    static std::string make_id();
    static std::string normalize_alias(std::string_view alias);
    static bool is_valid_alias(std::string_view alias);
    static bool validate(const ProjectConnection& connection, std::string* error = nullptr);

private:
    std::vector<ProjectConnection> connections_;
};

}  // namespace scyllagpt
