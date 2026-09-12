#pragma once

#include "scyllagpt/connection_policy.h"
#include "scyllagpt/keyring.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scyllagpt {

struct ConnectionQueryRequest {
    std::string operation_id;
    std::string project_id;
    std::string alias;
    std::string sql;
    bool approved = false;
    bool ssh_command = false;
};

enum class BrokerStatus {
    Ok = 0, InvalidRequest, ConnectionNotFound, Disabled, PolicyBlocked,
    ApprovalRequired, KeyringLocked, SecretUnavailable, Cancelled, TimedOut, ExecutionFailed
};

struct BrokerResponse {
    BrokerStatus status = BrokerStatus::InvalidRequest;
    SqlAssessment assessment;
    ConnectionResult result;
    std::string safe_message;
};

// Credential values authorized for exactly one operation, keyed by Keyring secret name. Produced
// by ConnectionBroker::prepare and destroyed with the context. This never crosses the broker pipe,
// never reaches the MCP helper process, and never reaches the agent.
struct AuthorizedCredentials {
    std::vector<std::pair<std::string, std::string>> values;

    // Empty string when the name was not authorized. Callers must treat empty as fail-closed.
    std::string lookup(std::string_view name) const;
    void wipe();
};

struct TrustedExecutionContext {
    std::string operation_id;
    ProjectConnection connection;
    std::string sql;
    std::uint32_t timeout_ms = 0;
    std::shared_ptr<std::atomic_bool> cancelled;
    AuthorizedCredentials credentials;
};

// Implemented only by trusted Scylla code. The executor receives one-shot authorized values and
// never holds a Keyring reference, so it cannot reach any secret outside its own operation.
class TrustedConnectionExecutor {
public:
    virtual ~TrustedConnectionExecutor() = default;
    virtual ConnectionResult execute(const TrustedExecutionContext& context) = 0;
    virtual void close(std::string_view operation_id) = 0;
    virtual void close_all() = 0;
};

// Outcome of the Keyring-touching phase. Only `ready` operations may be handed to finish().
struct PreparedOperation {
    bool ready = false;
    BrokerResponse response;
    TrustedExecutionContext context;
};

class ConnectionBroker {
public:
    ConnectionBroker(ProjectConnectionManager& connections, Keyring& keyring,
                     TrustedConnectionExecutor& executor);
    ~ConnectionBroker();

    // Touches the Keyring and does not block. Must run on the thread that owns the Keyring.
    PreparedOperation prepare(const ConnectionQueryRequest& request);
    // Blocks for the length of the query and touches no Keyring state, so it is safe on a worker
    // thread while the UI stays responsive.
    BrokerResponse finish(PreparedOperation prepared);
    // prepare + finish on the calling thread. Convenient for tests and single-threaded callers.
    BrokerResponse execute(const ConnectionQueryRequest& request);
    bool cancel(std::string_view operation_id);
    void close_project(std::string_view project_id);
    void close_all();

private:
    struct ActiveOperation {
        std::string project_id;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    bool authorize_refs(const ProjectConnection& connection, std::string_view operation_id,
                        BrokerResponse& response, AuthorizedCredentials& out);

    ProjectConnectionManager& connections_;
    Keyring& keyring_;
    TrustedConnectionExecutor& executor_;
    std::mutex mutex_;
    std::unordered_map<std::string, ActiveOperation> active_;
};

const char* broker_status_string(BrokerStatus status);

}  // namespace scyllagpt
