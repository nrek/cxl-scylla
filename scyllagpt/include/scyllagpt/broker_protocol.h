#pragma once

// Wire contract for the trusted query broker.
//
// Two hops share this file:
//   agent → MCP helper   : tools/call arguments (parse_query_tool_arguments)
//   helper → Workbench   : newline-delimited JSON over a local named pipe
//
// Nothing here touches the Keyring. The helper process is secret-free by construction: it can
// name a connection alias but can never read what that alias resolves to.

#include "scyllagpt/connection_broker.h"
#include "scyllagpt/json.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace scyllagpt {

inline constexpr const char* kQueryToolName = "scylla_query";
// The pipe name is the prefix plus the session token, so the helper only ever needs the token.
inline constexpr const char* kBrokerPipePrefix = R"(\\.\pipe\ScyllaQueryBroker.)";
inline constexpr const char* kBrokerTokenEnvVar = "SCYLLA_BROKER_TOKEN";
inline constexpr std::size_t kBrokerMaxMessageBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kBrokerMaxSqlBytes = 64u * 1024u;

// What the agent asked for. `operation_label` is the agent's own tag for the call; it is NOT the
// broker operation id. Scylla mints that itself so a caller cannot collide with or replay another
// in-flight operation.
struct QueryToolCall {
    std::string connection_alias;
    std::string sql;
    std::string operation_label;
    bool ssh_command = false;
};

inline Json ssh_tool_descriptor() {
    auto tool = Json::object();
    tool["name"] = Json::string("scylla_ssh");
    tool["description"] = Json::string("Execute a shell command through a saved SSH connection alias and its selected terminal. Scylla resolves Keyring credentials internally. Supply no credentials. Returns exit status, stdout and stderr subject to saved limits.");
    auto schema = Json::object();
    schema["type"] = Json::string("object");
    auto properties = Json::object();
    for (const auto* key : {"connection_alias", "command"}) {
        auto field = Json::object(); field["type"] = Json::string("string"); properties[key] = std::move(field);
    }
    schema["properties"] = std::move(properties);
    auto required = Json::array(); required.push(Json::string("connection_alias")); required.push(Json::string("command"));
    schema["required"] = std::move(required);
    schema["additionalProperties"] = Json::boolean(false);
    tool["inputSchema"] = std::move(schema);
    return tool;
}

// MCP tool descriptor for tools/list.
inline Json query_tool_descriptor() {
    Json alias = Json::object();
    alias["type"] = Json::string("string");
    alias["description"] = Json::string(
        "Alias of a connection saved in Scylla for this project (for example synq-hot-ro). The "
        "saved connection owns the SSH and database credential mapping; you cannot supply "
        "credentials, hosts, or Keyring values.");

    Json sql = Json::object();
    sql["type"] = Json::string("string");
    sql["description"] = Json::string(
        "One read-only SQL statement. Statements that modify data or schema are refused unless the "
        "saved connection's policy allows them, and may require the user to approve.");

    Json label = Json::object();
    label["type"] = Json::string("string");
    label["description"] = Json::string("Short human-readable tag for this call, e.g. latest-bitcoin-price.");

    Json properties = Json::object();
    properties["connection_alias"] = std::move(alias);
    properties["sql"] = std::move(sql);
    properties["operation_id"] = std::move(label);

    Json required = Json::array();
    required.push(Json::string("connection_alias"));
    required.push(Json::string("sql"));

    Json schema = Json::object();
    schema["type"] = Json::string("object");
    schema["properties"] = std::move(properties);
    schema["required"] = std::move(required);

    Json tool = Json::object();
    tool["name"] = Json::string(kQueryToolName);
    tool["description"] = Json::string(
        "Run a read-only SQL query through Scylla against a saved project connection. Scylla "
        "resolves credentials from its Keyring internally, executes over SSH, enforces row and "
        "byte limits, and returns rows. Secret values are never disclosed to you.");
    tool["inputSchema"] = std::move(schema);
    return tool;
}

// Returns false and sets *error when the call is unusable. Fails closed on oversize SQL so a
// runaway statement cannot reach the pipe.
inline bool parse_query_tool_arguments(const Json& arguments, QueryToolCall* out, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!out) return fail("internal error");
    if (!arguments.is_object()) return fail("arguments must be an object");
    out->connection_alias = arguments.at("connection_alias").as_string("");
    out->sql = arguments.at("sql").as_string("");
    out->operation_label = arguments.at("operation_id").as_string("");
    if (out->connection_alias.empty()) return fail("connection_alias is required");
    if (out->sql.empty()) return fail("sql is required");
    if (out->sql.size() > kBrokerMaxSqlBytes) return fail("sql exceeds the allowed size");
    if (error) error->clear();
    return true;
}

inline std::string encode_broker_request(const QueryToolCall& call, std::string_view token) {
    Json root = Json::object();
    root["v"] = Json::number(1);
    root["op"] = Json::string(call.ssh_command ? "ssh" : "query");
    root["token"] = Json::string(std::string(token));
    root["alias"] = Json::string(call.connection_alias);
    root["sql"] = Json::string(call.sql);
    root["label"] = Json::string(call.operation_label);
    return root.dump();
}

inline bool decode_broker_request(std::string_view line, QueryToolCall* out, std::string* out_token,
                                 std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!out || !out_token) return fail("internal error");
    if (line.size() > kBrokerMaxMessageBytes) return fail("request too large");
    std::string parse_error;
    const Json root = Json::parse(line, &parse_error);
    if (!parse_error.empty() || !root.is_object()) return fail("malformed request");
    if (root.at("v").as_int(0) != 1) return fail("unsupported protocol version");
    const auto operation = root.at("op").as_string("");
    if (operation != "query" && operation != "ssh") return fail("unsupported operation");
    out->ssh_command = operation == "ssh";
    *out_token = root.at("token").as_string("");
    out->connection_alias = root.at("alias").as_string("");
    out->sql = root.at("sql").as_string("");
    out->operation_label = root.at("label").as_string("");
    if (out->connection_alias.empty() || out->sql.empty()) return fail("incomplete request");
    if (out->sql.size() > kBrokerMaxSqlBytes) return fail("sql exceeds the allowed size");
    if (error) error->clear();
    return true;
}

// Only policy-sanctioned, already-bounded data crosses back. `safe_message` is the sole free-text
// field and is written by trusted code, never by the executor's stderr.
inline std::string encode_broker_response(const BrokerResponse& response) {
    Json root = Json::object();
    root["v"] = Json::number(1);
    root["status"] = Json::string(broker_status_string(response.status));
    root["message"] = Json::string(response.safe_message);
    root["rowCount"] = Json::number(static_cast<std::int64_t>(response.result.row_count));
    root["elapsedMs"] = Json::number(static_cast<std::int64_t>(response.result.elapsed_ms));
    root["truncated"] = Json::boolean(response.result.truncated);
    if (!response.result.warning.empty()) root["warning"] = Json::string(response.result.warning);
    if (!response.result.columns_json.empty()) {
        std::string ignored;
        const Json columns = Json::parse(response.result.columns_json, &ignored);
        if (ignored.empty()) root["columns"] = columns;
    }
    if (!response.result.rows_json.empty()) {
        std::string ignored;
        const Json rows = Json::parse(response.result.rows_json, &ignored);
        if (ignored.empty()) root["rows"] = rows;
    }
    return root.dump();
}

// Text the agent actually sees in the tool result. Keeps the failure reason legible so the agent
// stops instead of inventing rows.
inline std::string format_query_tool_result(std::string_view response_line) {
    std::string parse_error;
    const Json root = Json::parse(response_line, &parse_error);
    if (!parse_error.empty() || !root.is_object()) {
        return "Scylla broker returned an unreadable response. No rows were retrieved.";
    }
    const std::string status = root.at("status").as_string("invalid_request");
    if (status != "ok") {
        std::string message = root.at("message").as_string("");
        std::string out = "Scylla did not execute the query (" + status + ").";
        if (!message.empty()) out += " " + message;
        out += " No rows were retrieved; do not infer or fabricate results.";
        return out;
    }
    return root.dump();
}

}  // namespace scyllagpt
