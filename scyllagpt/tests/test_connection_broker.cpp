#include "scyllagpt/broker_protocol.h"
#include "scyllagpt/connection_broker.h"

#include <iostream>
#include <string>
#include <utility>

namespace {
int failures = 0;
void expect(bool value, const char* name) {
    if (value) std::cout << "ok   " << name << "\n";
    else { std::cerr << "FAIL " << name << "\n"; ++failures; }
}

class FakeExecutor final : public scyllagpt::TrustedConnectionExecutor {
public:
    scyllagpt::ConnectionResult execute(const scyllagpt::TrustedExecutionContext& context) override {
        ++executions;
        saw_credentials = !context.credentials.values.empty();
        scyllagpt::ConnectionResult result;
        result.ok = succeed;
        result.warning = warning;
        return result;
    }
    void close(std::string_view) override { ++closes; }
    void close_all() override { ++close_alls; }
    int executions = 0;
    int closes = 0;
    int close_alls = 0;
    bool saw_credentials = false;
    bool succeed = true;
    std::string warning;
};

scyllagpt::ProjectConnection connection() {
    scyllagpt::ProjectConnection value;
    value.project_id = "project-a";
    value.name = "Production";
    value.alias = "prod-db";
    value.route_type = scyllagpt::ConnectionRouteType::Direct;
    value.database.host = "database.internal";
    value.database.database = "app";
    value.database.username_ref = "scylla_DB_USER";
    value.database.password_ref = "scylla_DB_PASSWORD";
    return value;
}
}

int run_connection_broker_tests() {
    using namespace scyllagpt;
    failures = 0;
    ProjectConnectionManager manager;
    std::string error;
    expect(manager.upsert(connection(), &error), "broker test connection setup");
    Keyring keyring;
    FakeExecutor executor;
    ConnectionBroker broker(manager, keyring, executor);

    ConnectionQueryRequest missing{"op-1", "project-a", "missing", "SELECT 1", false};
    expect(broker.execute(missing).status == BrokerStatus::ConnectionNotFound,
           "broker keeps aliases project scoped");
    ConnectionQueryRequest ddl{"op-2", "project-a", "prod-db", "DROP TABLE users", false};
    expect(broker.execute(ddl).status == BrokerStatus::ApprovalRequired,
           "broker asks before schema change");
    ConnectionQueryRequest read{"op-3", "project-a", "prod-db", "SELECT 1", false};
    expect(broker.execute(read).status == BrokerStatus::KeyringLocked,
           "broker requires unlocked keyring");
    expect(executor.executions == 0, "untrusted request never reaches executor");
    expect(!broker.cancel("unknown"), "cancel rejects unknown operation");

    // The executor has already sanitized its diagnostics. Preserve that useful
    // reason instead of replacing every failure with a generic broker message.
    executor.succeed = false;
    executor.warning = "Host key verification failed.";
    PreparedOperation failed_execution;
    failed_execution.ready = true;
    failed_execution.context.operation_id = "op-executor-failure";
    const BrokerResponse failed_response = broker.finish(std::move(failed_execution));
    expect(failed_response.status == BrokerStatus::ExecutionFailed,
           "executor failure reaches broker response");
    expect(failed_response.safe_message == "Host key verification failed.",
           "broker preserves sanitized executor diagnostic");
    executor.succeed = true;
    executor.warning.clear();

    // A refused prepare must not leave the operation registered, or the id becomes unusable.
    auto refused = broker.prepare(read);
    expect(!refused.ready, "locked keyring prepares nothing");
    expect(refused.context.credentials.values.empty(), "refused prepare carries no credentials");
    expect(broker.finish(std::move(refused)).status == BrokerStatus::KeyringLocked,
           "finish preserves the prepare failure");
    // The earlier ready-but-failed finish already exercised the executor; clear the counter so this
    // assertion is specifically about unready finish, not about prior cases.
    executor.executions = 0;
    auto refused_again = broker.prepare(read);
    expect(broker.finish(std::move(refused_again)).status == BrokerStatus::KeyringLocked,
           "second refused finish preserves status");
    expect(executor.executions == 0, "finish of an unready operation never executes");
    auto retry = broker.prepare(read);
    expect(!retry.ready && retry.response.status == BrokerStatus::KeyringLocked,
           "operation id is released after a refused prepare");

    AuthorizedCredentials creds;
    creds.values.emplace_back("scylla_DB_PASSWORD", "secret-value");
    expect(creds.lookup("scylla_DB_PASSWORD") == "secret-value", "credential lookup by name");
    expect(creds.lookup("scylla_MISSING").empty(), "unknown credential returns empty");
    creds.wipe();
    expect(creds.lookup("scylla_DB_PASSWORD").empty(), "wiped credentials are unreadable");

    // Protocol layer: agent-supplied arguments and the pipe encoding.
    std::string protocol_error;
    QueryToolCall call;
    std::string args_error;
    const Json good = Json::parse(
        R"({"connection_alias":"synq-hot-ro","sql":"SELECT 1","operation_id":"latest-price"})",
        &protocol_error);
    expect(parse_query_tool_arguments(good, &call, &args_error), "parse tool arguments");
    expect(call.connection_alias == "synq-hot-ro" && call.sql == "SELECT 1", "tool arguments carry alias and sql");
    const Json no_sql = Json::parse(R"({"connection_alias":"synq-hot-ro"})", &protocol_error);
    expect(!parse_query_tool_arguments(no_sql, &call, &args_error), "reject tool call without sql");
    Json oversize = Json::object();
    oversize["connection_alias"] = Json::string("synq-hot-ro");
    oversize["sql"] = Json::string(std::string(kBrokerMaxSqlBytes + 1, 'x'));
    expect(!parse_query_tool_arguments(oversize, &call, &args_error), "reject oversize sql");

    call.connection_alias = "synq-hot-ro";
    call.sql = "SELECT 1";
    call.operation_label = "latest-price";
    const std::string wire = encode_broker_request(call, "tok-123");
    QueryToolCall decoded;
    std::string token;
    std::string decode_error;
    expect(decode_broker_request(wire, &decoded, &token, &decode_error), "decode broker request");
    expect(token == "tok-123" && decoded.sql == "SELECT 1", "broker request round-trip");
    expect(!decode_broker_request("{not json", &decoded, &token, &decode_error), "reject malformed request");
    expect(!decode_broker_request(R"({"v":2,"op":"query","alias":"a","sql":"SELECT 1"})", &decoded,
                                 &token, &decode_error),
           "reject unsupported protocol version");

    BrokerResponse blocked;
    blocked.status = BrokerStatus::PolicyBlocked;
    blocked.safe_message = "Schema changes are blocked on this connection.";
    const std::string blocked_wire = encode_broker_response(blocked);
    const std::string blocked_text = format_query_tool_result(blocked_wire);
    expect(blocked_text.find("policy_blocked") != std::string::npos, "tool result names the refusal");
    expect(blocked_text.find("do not infer or fabricate") != std::string::npos,
           "tool result forbids fabricating rows");

    BrokerResponse ok_response;
    ok_response.status = BrokerStatus::Ok;
    ok_response.safe_message = "Query completed.";
    ok_response.result.ok = true;
    ok_response.result.columns_json = R"(["price"])";
    ok_response.result.rows_json = R"([["64000.00"]])";
    ok_response.result.row_count = 1;
    const std::string ok_text = format_query_tool_result(encode_broker_response(ok_response));
    expect(ok_text.find("64000.00") != std::string::npos, "successful tool result carries rows");
    expect(format_query_tool_result("not json").find("unreadable") != std::string::npos,
           "unreadable broker response fails loudly");
    return failures;
}
