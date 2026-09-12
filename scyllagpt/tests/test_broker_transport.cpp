#include "scyllagpt/broker_mcp.h"
#include "scyllagpt/broker_service.h"
#include "scyllagpt/utf.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool value, const char* name) {
    if (value) std::cout << "ok   " << name << "\n";
    else { std::cerr << "FAIL " << name << "\n"; ++failures; }
}

scyllagpt::Json parse(std::string_view text) {
    std::string error;
    return scyllagpt::Json::parse(text, &error);
}
}  // namespace

int run_broker_transport_tests() {
    using namespace scyllagpt;
    failures = 0;

    // --- MCP protocol surface (no pipe, no stdio) ---
    const auto initialize = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", nullptr);
    expect(initialize.has_body, "initialize is answered");
    const Json initialized = parse(initialize.body);
    expect(initialized.at("result").at("serverInfo").at("name").as_string("") == "scylla-query-broker",
           "initialize names the broker server");
    expect(initialized.at("result").at("capabilities").has("tools"), "initialize advertises tools");

    expect(!handle_mcp_message(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", nullptr).has_body,
           "notifications get no reply");
    expect(handle_mcp_message("{not json", nullptr).body.find("-32700") != std::string::npos,
           "malformed json is a parse error");
    expect(handle_mcp_message(R"({"jsonrpc":"2.0","id":9,"method":"nope"})", nullptr).body.find("-32601") !=
               std::string::npos,
           "unknown method is rejected");

    const auto listed = handle_mcp_message(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", nullptr);
    const Json tools = parse(listed.body).at("result").at("tools");
    expect(tools.size() == 2, "query and SSH tools are exposed");
    expect(tools.at(std::size_t{1}).at("name").as_string("") == "scylla_ssh", "SSH has a separate tool");
    {
        bool called = false;
        handle_mcp_message(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"scylla_ssh","arguments":{"connection_alias":"ssh_synq","command":"uname -a"}}})",
            [&](const QueryToolCall& call) {
                called = call.ssh_command && call.connection_alias == "ssh_synq" && call.sql == "uname -a";
                QueryToolCall decoded;
                std::string token, error;
                expect(decode_broker_request(encode_broker_request(call, "token"), &decoded, &token, &error) && decoded.ssh_command,
                    "SSH operation type survives pipe serialization");
                return std::string{};
            });
        expect(called, "SSH tool forwards alias and command without SQL interpretation");
    }
    expect(tools.at(std::size_t{0}).at("name").as_string("") == "scylla_query", "the tool is scylla_query");
    const Json properties = tools.at(std::size_t{0}).at("inputSchema").at("properties");
    expect(properties.has("connection_alias") && properties.has("sql"), "tool schema takes alias and sql");
    expect(!properties.has("host") && !properties.has("username") && !properties.has("password") &&
               !properties.has("role_map"),
           "tool schema exposes no credential surface");

    // A successful call: the broker's rows reach the agent.
    QueryToolCall seen;
    auto ok_broker = [&seen](const QueryToolCall& call) {
        seen = call;
        BrokerResponse response;
        response.status = BrokerStatus::Ok;
        response.safe_message = "Query completed.";
        response.result.ok = true;
        response.result.columns_json = R"(["price"])";
        response.result.rows_json = R"([["64000.00"]])";
        response.result.row_count = 1;
        return encode_broker_response(response);
    };
    const auto called = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"scylla_query",)"
        R"("arguments":{"connection_alias":"synq-hot-ro","sql":"SELECT price FROM t","operation_id":"p"}}})",
        ok_broker);
    const Json call_result = parse(called.body).at("result");
    expect(seen.connection_alias == "synq-hot-ro", "tool call reaches the broker with its alias");
    expect(!call_result.has("isError"), "successful call is not an error");
    expect(call_result.at("content").at(std::size_t{0}).at("text").as_string("").find("64000.00") !=
               std::string::npos,
           "rows reach the agent");

    // Refusals must be legible and must forbid fabrication.
    auto blocked_broker = [](const QueryToolCall&) {
        BrokerResponse response;
        response.status = BrokerStatus::PolicyBlocked;
        response.safe_message = "Schema changes are blocked on this connection.";
        return encode_broker_response(response);
    };
    const auto blocked = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"scylla_query",)"
        R"("arguments":{"connection_alias":"synq-hot-ro","sql":"DROP TABLE t"}}})",
        blocked_broker);
    const Json blocked_result = parse(blocked.body).at("result");
    expect(blocked_result.at("isError").as_bool(false), "a refused query is an error result");
    const std::string blocked_text = blocked_result.at("content").at(std::size_t{0}).at("text").as_string("");
    expect(blocked_text.find("policy_blocked") != std::string::npos, "refusal names the policy");
    expect(blocked_text.find("fabricate") != std::string::npos, "refusal forbids fabrication");

    // Transport failure must never look like an empty result set.
    const auto unreachable = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"scylla_query",)"
        R"("arguments":{"connection_alias":"a","sql":"SELECT 1"}}})",
        [](const QueryToolCall&) { return std::string(); });
    const Json unreachable_result = parse(unreachable.body).at("result");
    expect(unreachable_result.at("isError").as_bool(false), "unreachable broker is an error");
    expect(unreachable_result.at("content").at(std::size_t{0}).at("text").as_string("").find("did not run") !=
               std::string::npos,
           "unreachable broker says the query did not run");

    const auto wrong_tool = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"rm_rf","arguments":{}}})",
        ok_broker);
    expect(wrong_tool.body.find("-32602") != std::string::npos, "unknown tool name is rejected");

    const auto no_sql = handle_mcp_message(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"scylla_query",)"
        R"("arguments":{"connection_alias":"a"}}})",
        ok_broker);
    expect(parse(no_sql.body).at("result").at("isError").as_bool(false), "missing sql is a tool error");

    // --- Live pipe round-trip against the real service ---
    expect(BrokerPipeService::make_secret_token().size() == 32, "session token is 32 hex characters");
    expect(BrokerPipeService::make_secret_token() != BrokerPipeService::make_secret_token(),
           "session tokens do not repeat");

    // The helper never receives a pipe path; it rebuilds one from the token alone.
    BrokerPipeService service;
    std::string start_error;
    QueryToolCall served;
    const bool started = service.start(
        [&served](const QueryToolCall& call) {
            served = call;
            BrokerResponse response;
            response.status = BrokerStatus::Ok;
            response.safe_message = "Query completed.";
            response.result.ok = true;
            response.result.rows_json = R"([["1"]])";
            response.result.row_count = 1;
            return encode_broker_response(response);
        },
        &start_error);
    expect(started, "broker pipe service starts");
    if (started) {
        QueryToolCall call;
        call.connection_alias = "synq-hot-ro";
        call.sql = "SELECT 1";
        call.operation_label = "smoke";

        expect(service.pipe_name() == utf16(std::string(kBrokerPipePrefix) + service.token()),
               "pipe name is derivable from the token alone");
        const std::string good =
            call_broker_over_pipe(service.pipe_name(), service.token(), call, 5000);
        expect(!good.empty(), "pipe round-trip returns a response");
        expect(parse(good).at("status").as_string("") == "ok", "authorized call succeeds");
        expect(served.sql == "SELECT 1", "handler received the sql");

        // The token is the whole authorization story; a wrong one must be refused.
        served = {};
        const std::string bad = call_broker_over_pipe(service.pipe_name(), "wrong-token", call, 5000);
        expect(!bad.empty(), "unauthorized call still gets a reply");
        expect(parse(bad).at("status").as_string("") == "invalid_request", "wrong token is refused");
        expect(served.sql.empty(), "refused call never reaches the handler");

        service.stop();
        expect(!service.running(), "broker pipe service stops");
        expect(call_broker_over_pipe(service.pipe_name(), "any", call, 500).empty(),
               "stopped service accepts nothing");
    }
    return failures;
}
