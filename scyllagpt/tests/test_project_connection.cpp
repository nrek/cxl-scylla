#include "scyllagpt/project_connection.h"
#include "scyllagpt/json.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char* name) {
    if (condition) std::cout << "ok   " << name << "\n";
    else { std::cerr << "FAIL " << name << "\n"; ++failures; }
}

std::wstring temp_path() {
    wchar_t directory[MAX_PATH]{};
    wchar_t file[MAX_PATH]{};
    GetTempPathW(MAX_PATH, directory);
    GetTempFileNameW(directory, L"scyc", 0, file);
    DeleteFileW(file);
    return std::wstring(file) + L".connections.json";
}

scyllagpt::ProjectConnection valid_connection(std::string project, std::string alias) {
    scyllagpt::ProjectConnection connection;
    connection.project_id = std::move(project);
    connection.name = "Production Database";
    connection.alias = std::move(alias);
    connection.route_type = scyllagpt::ConnectionRouteType::RemoteExecution;
    connection.ssh.host = "server.example";
    connection.ssh.username = "ubuntu";
    connection.ssh.private_key_ref = "scylla_PROD_SSH_KEY";
    connection.ssh.key_passphrase_ref = "scylla_PROD_SSH_KEY_PASS";
    connection.ssh.host_key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestHostKeyMaterial";
    connection.ssh.host_key_fingerprint = "SHA256:test-host-key";
    connection.database.host = "database.internal";
    connection.database.database = "application";
    connection.database.username_ref = "scylla_DB_USER";
    connection.database.password_ref = "scylla_DB_PASSWORD";
    return connection;
}
}  // namespace

int run_project_connection_tests() {
    using scyllagpt::ProjectConnectionManager;
    failures = 0;
    expect(ProjectConnectionManager::normalize_alias(" @Prod-DB ") == "prod-db", "normalize connection alias");
    expect(ProjectConnectionManager::is_valid_alias("prod_db-2"), "valid connection alias");
    expect(!ProjectConnectionManager::is_valid_alias("bad alias"), "reject spaced connection alias");

    ProjectConnectionManager manager;
    auto production = valid_connection("project-a", "@prod-db");
    {
        auto mongo = production;
        mongo.database.engine = scyllagpt::DatabaseEngine::MongoDb;
        mongo.database.port = 27017;
        const auto json = scyllagpt::project_connection_json(mongo);
        const auto restored = scyllagpt::project_connection_from_json(json);
        expect(restored.database.engine == scyllagpt::DatabaseEngine::MongoDb && restored.database.port == 27017,
               "MongoDB connection metadata roundtrips without falling back to MySQL");
        expect(restored.database.password_ref == mongo.database.password_ref && restored.ssh.private_key_ref == mongo.ssh.private_key_ref,
               "settings serialization preserves credential references");
    }
    std::string error;
    expect(manager.upsert(production, &error), "add project connection");
    expect(manager.all().size() == 1, "connection count");
    const auto* resolved = manager.resolve_alias("project-a", "@PROD-DB");
    expect(resolved != nullptr, "resolve project connection alias");
    expect(manager.resolve_alias("project-b", "prod-db") == nullptr, "alias remains project scoped");

    auto duplicate = valid_connection("project-a", "prod-db");
    duplicate.name = "Duplicate";
    expect(!manager.upsert(duplicate, &error), "reject duplicate project alias");
    auto other_project = valid_connection("project-b", "prod-db");
    expect(manager.upsert(other_project, &error), "allow alias in different project");

    auto missing_host_key = valid_connection("project-a", "unsafe-db");
    missing_host_key.ssh.host_key.clear();
    expect(!manager.upsert(missing_host_key, &error), "require pinned SSH host key");

    auto no_ssh_credential = valid_connection("project-a", "no-cred-db");
    no_ssh_credential.ssh.private_key_ref.clear();
    no_ssh_credential.ssh.auth_ref.clear();
    expect(!manager.upsert(no_ssh_credential, &error), "require an SSH password or key reference");

    auto password_only = valid_connection("project-a", "pw-db");
    password_only.ssh.private_key_ref.clear();
    password_only.ssh.key_passphrase_ref.clear();
    password_only.ssh.auth_ref = "scylla_PROD_SSH_PASSWORD";
    expect(manager.upsert(password_only, &error), "password-only SSH credential is accepted");

    // Hosts and usernames may be Keyring names instead of literals, so a connection file can carry
    // no infrastructure detail. One of the two forms is still required.
    auto referenced = valid_connection("project-a", "ref-db");
    referenced.ssh.host.clear();
    referenced.ssh.host_ref = "scylla_SSH_HOST";
    referenced.ssh.username.clear();
    referenced.ssh.username_ref = "scylla_SSH_USER";
    referenced.ssh.port_ref = "scylla_SSH_PORT";
    referenced.database.host.clear();
    referenced.database.host_ref = "scylla_DB_HOST";
    referenced.database.port_ref = "scylla_DB_PORT";
    expect(manager.upsert(referenced, &error), "accept Keyring-referenced hosts, ports, and username");

    auto no_host = valid_connection("project-a", "no-host-db");
    no_host.ssh.host.clear();
    expect(!manager.upsert(no_host, &error), "require an SSH host or a host reference");
    auto no_username = valid_connection("project-a", "no-user-db");
    no_username.ssh.username.clear();
    expect(!manager.upsert(no_username, &error), "require an SSH username or a username reference");
    auto no_db_host = valid_connection("project-a", "no-db-host");
    no_db_host.database.host.clear();
    expect(!manager.upsert(no_db_host, &error), "require a database host or a host reference");

    const std::wstring path = temp_path();
    expect(manager.save(path), "save project connections");
    ProjectConnectionManager loaded;
    expect(loaded.load(path), "load project connections");
    const auto* restored = loaded.resolve_alias("project-a", "prod-db");
    expect(restored != nullptr, "round-trip connection");
    expect(restored && restored->database.password_ref == "scylla_DB_PASSWORD", "round-trip secret reference");
    expect(restored && restored->ssh.private_key_ref == "scylla_PROD_SSH_KEY", "round-trip ssh key reference");
    expect(restored && restored->ssh.key_passphrase_ref == "scylla_PROD_SSH_KEY_PASS",
           "round-trip ssh key passphrase reference");
    expect(restored && restored->ssh.host_key.rfind("ssh-ed25519 ", 0) == 0, "round-trip pinned host key");
    const auto* restored_refs = loaded.resolve_alias("project-a", "ref-db");
    expect(restored_refs && restored_refs->ssh.host_ref == "scylla_SSH_HOST", "round-trip ssh host reference");
    expect(restored_refs && restored_refs->ssh.port_ref == "scylla_SSH_PORT", "round-trip ssh port reference");
    expect(restored_refs && restored_refs->ssh.username_ref == "scylla_SSH_USER", "round-trip ssh username reference");
    expect(restored_refs && restored_refs->database.host_ref == "scylla_DB_HOST", "round-trip database host reference");
    expect(restored_refs && restored_refs->database.port_ref == "scylla_DB_PORT", "round-trip database port reference");
    expect(restored && restored->database.password_ref.find("password-value") == std::string::npos,
           "connection stores references only");
    DeleteFileW(path.c_str());
    return failures;
}
