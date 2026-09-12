#include "scyllagpt/connection_broker.h"

#include "scyllagpt/utf.h"

#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

std::string AuthorizedCredentials::lookup(std::string_view name) const {
    if (name.empty()) return {};
    for (const auto& [key, value] : values) {
        if (key == name) return value;
    }
    return {};
}

void AuthorizedCredentials::wipe() {
    for (auto& [key, value] : values) {
        if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    }
    values.clear();
}

ConnectionBroker::ConnectionBroker(ProjectConnectionManager& connections, Keyring& keyring,
                                   TrustedConnectionExecutor& executor)
    : connections_(connections), keyring_(keyring), executor_(executor) {}

ConnectionBroker::~ConnectionBroker() { close_all(); }

bool ConnectionBroker::authorize_refs(const ProjectConnection& connection,
                                      std::string_view operation_id, BrokerResponse& response,
                                      AuthorizedCredentials& out) {
    if (!keyring_.is_unlocked()) {
        response.status = BrokerStatus::KeyringLocked;
        response.safe_message = "Unlock the Scylla Keyring to use this connection.";
        return false;
    }
    std::vector<std::string_view> refs;
    // Routing detail is Keyring-held too, so hosts, ports, and usernames authorize alongside secrets.
    for (const std::string* ref : {&connection.ssh.host_ref, &connection.ssh.port_ref,
                                   &connection.ssh.username_ref, &connection.ssh.auth_ref,
                                   &connection.ssh.private_key_ref, &connection.ssh.key_passphrase_ref,
                                   &connection.database.host_ref, &connection.database.port_ref,
                                   &connection.database.username_ref, &connection.database.password_ref,
                                   &connection.database.tls_ca_ref}) {
        if (!ref->empty()) refs.push_back(*ref);
    }
    if (keyring_.authorize_use_many(refs, operation_id) != KeyringStatus::Ok) {
        response.status = BrokerStatus::SecretUnavailable;
        response.safe_message = "A protected credential required by this connection is unavailable.";
        return false;
    }
    // Drain the one-shot authorization into this operation's own credential set. The fragment is
    // NAME=value pairs separated by NUL; it is zeroed before returning either way.
    std::wstring fragment;
    const KeyringStatus built = keyring_.build_authorized_env_block(operation_id, fragment);
    auto scrub = [&fragment]() {
        if (!fragment.empty()) SecureZeroMemory(fragment.data(), fragment.size() * sizeof(wchar_t));
        fragment.clear();
    };
    if (built != KeyringStatus::Ok) {
        scrub();
        response.status = BrokerStatus::SecretUnavailable;
        response.safe_message = "A protected credential required by this connection is unavailable.";
        return false;
    }
    out.wipe();
    std::size_t i = 0;
    while (i < fragment.size()) {
        const std::size_t start = i;
        while (i < fragment.size() && fragment[i] != L'\0') ++i;
        if (i > start) {
            const std::wstring entry = fragment.substr(start, i - start);
            const std::size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq > 0) {
                out.values.emplace_back(utf8(entry.substr(0, eq)), utf8(entry.substr(eq + 1)));
            }
        }
        if (i < fragment.size() && fragment[i] == L'\0') ++i;
    }
    scrub();
    if (out.values.empty()) {
        response.status = BrokerStatus::SecretUnavailable;
        response.safe_message = "A protected credential required by this connection is unavailable.";
        return false;
    }
    return true;
}

PreparedOperation ConnectionBroker::prepare(const ConnectionQueryRequest& request) {
    PreparedOperation prepared;
    BrokerResponse& response = prepared.response;
    if (request.operation_id.empty() || request.project_id.empty() || request.alias.empty() || request.sql.empty()) {
        response.safe_message = "The connection request is incomplete.";
        return prepared;
    }
    const ProjectConnection* stored = connections_.resolve_alias(request.project_id, request.alias);
    if (!stored) {
        response.status = BrokerStatus::ConnectionNotFound;
        response.safe_message = "The requested project connection was not found.";
        return prepared;
    }
    const ProjectConnection connection = *stored;
    if (!connection.enabled) {
        response.status = BrokerStatus::Disabled;
        response.safe_message = "The requested connection is disabled.";
        return prepared;
    }
    if (connection.ssh_only != request.ssh_command) {
        response.safe_message = "The tool does not match this connection type.";
        return prepared;
    }
    if (request.ssh_command) {
        response.assessment.allowed = connection.command_authority != ConnectionAuthority::Block;
        response.assessment.approval_required = connection.command_authority == ConnectionAuthority::Ask;
        response.assessment.reason = "SSH commands are blocked for this connection.";
    } else response.assessment = assess_sql(request.sql, connection.query_policy);
    if (!response.assessment.allowed) {
        response.status = BrokerStatus::PolicyBlocked;
        response.safe_message = response.assessment.reason;
        return prepared;
    }
    if (response.assessment.approval_required && !request.approved) {
        response.status = BrokerStatus::ApprovalRequired;
        response.safe_message = "This query requires user approval.";
        return prepared;
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    {
        std::lock_guard lock(mutex_);
        if (active_.contains(request.operation_id)) {
            response.status = BrokerStatus::InvalidRequest;
            response.safe_message = "The operation identifier is already active.";
            return prepared;
        }
        active_.emplace(request.operation_id, ActiveOperation{request.project_id, cancelled});
    }

    AuthorizedCredentials credentials;
    if (!authorize_refs(connection, request.operation_id, response, credentials)) {
        std::lock_guard lock(mutex_);
        active_.erase(request.operation_id);
        return prepared;
    }

    prepared.context = TrustedExecutionContext{request.operation_id,
                                               connection,
                                               request.sql,
                                               connection.result_policy.timeout_seconds * 1000U,
                                               cancelled,
                                               std::move(credentials)};
    prepared.ready = true;
    return prepared;
}

BrokerResponse ConnectionBroker::finish(PreparedOperation prepared) {
    BrokerResponse response = prepared.response;
    if (!prepared.ready) return response;

    response.result = executor_.execute(prepared.context);
    {
        std::lock_guard lock(mutex_);
        active_.erase(prepared.context.operation_id);
    }
    const bool cancelled = prepared.context.cancelled && prepared.context.cancelled->load();
    prepared.context.credentials.wipe();
    if (cancelled) {
        response.status = BrokerStatus::Cancelled;
        response.safe_message = "Connection operation cancelled.";
        return response;
    }
    enforce_result_policy(response.result, prepared.context.connection.result_policy);
    response.status = response.result.ok ? BrokerStatus::Ok : BrokerStatus::ExecutionFailed;
    response.safe_message = response.result.ok
                                ? "Query completed."
                                : (response.result.warning.empty()
                                       ? "The brokered query failed."
                                       : response.result.warning);
    return response;
}

BrokerResponse ConnectionBroker::execute(const ConnectionQueryRequest& request) {
    return finish(prepare(request));
}

bool ConnectionBroker::cancel(std::string_view operation_id) {
    {
        std::lock_guard lock(mutex_);
        const auto found = active_.find(std::string(operation_id));
        if (found == active_.end()) return false;
        found->second.cancelled->store(true);
    }
    executor_.close(operation_id);
    return true;
}

void ConnectionBroker::close_project(std::string_view project_id) {
    std::vector<std::string> ids;
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, operation] : active_) {
            if (operation.project_id == project_id) { operation.cancelled->store(true); ids.push_back(id); }
        }
    }
    for (const auto& id : ids) executor_.close(id);
}

void ConnectionBroker::close_all() {
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, operation] : active_) operation.cancelled->store(true);
        active_.clear();
    }
    executor_.close_all();
}

const char* broker_status_string(BrokerStatus status) {
    switch (status) {
        case BrokerStatus::Ok: return "ok";
        case BrokerStatus::InvalidRequest: return "invalid_request";
        case BrokerStatus::ConnectionNotFound: return "connection_not_found";
        case BrokerStatus::Disabled: return "disabled";
        case BrokerStatus::PolicyBlocked: return "policy_blocked";
        case BrokerStatus::ApprovalRequired: return "approval_required";
        case BrokerStatus::KeyringLocked: return "keyring_locked";
        case BrokerStatus::SecretUnavailable: return "secret_unavailable";
        case BrokerStatus::Cancelled: return "cancelled";
        case BrokerStatus::TimedOut: return "timed_out";
        case BrokerStatus::ExecutionFailed: return "execution_failed";
    }
    return "invalid_request";
}

}  // namespace scyllagpt
