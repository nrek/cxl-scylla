#include "scyllagpt/broker_mcp.h"

#include "scyllagpt/utf.h"

#include <cstdio>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {
namespace {

constexpr const char* kProtocolVersion = "2024-11-05";

Json rpc_envelope(const Json& id) {
    Json root = Json::object();
    root["jsonrpc"] = Json::string("2.0");
    root["id"] = id;
    return root;
}

McpReply rpc_result(const Json& id, Json result) {
    Json root = rpc_envelope(id);
    root["result"] = std::move(result);
    return McpReply{true, root.dump()};
}

McpReply rpc_error(const Json& id, int code, const std::string& message) {
    Json error = Json::object();
    error["code"] = Json::number(code);
    error["message"] = Json::string(message);
    Json root = rpc_envelope(id);
    root["error"] = std::move(error);
    return McpReply{true, root.dump()};
}

// MCP tool failures are reported inside a successful result with isError, not as a JSON-RPC error,
// so the agent sees the reason as tool output instead of a protocol fault.
McpReply tool_reply(const Json& id, const std::string& text, bool is_error) {
    Json entry = Json::object();
    entry["type"] = Json::string("text");
    entry["text"] = Json::string(text);
    Json content = Json::array();
    content.push(std::move(entry));
    Json result = Json::object();
    result["content"] = std::move(content);
    if (is_error) result["isError"] = Json::boolean(true);
    return rpc_result(id, std::move(result));
}

}  // namespace

McpReply handle_mcp_message(std::string_view line, const BrokerCallFn& call_broker) {
    std::string parse_error;
    const Json root = Json::parse(line, &parse_error);
    if (!parse_error.empty() || !root.is_object()) {
        return rpc_error(Json::null(), -32700, "parse error");
    }
    const std::string method = root.at("method").as_string("");
    const Json& id = root.at("id");
    const bool is_notification = id.is_null();

    if (method.empty()) return McpReply{};
    if (is_notification) return McpReply{};  // initialized, cancelled, and friends need no reply

    if (method == "initialize") {
        Json tools = Json::object();
        Json capabilities = Json::object();
        capabilities["tools"] = std::move(tools);
        Json info = Json::object();
        info["name"] = Json::string("scylla-query-broker");
        info["version"] = Json::string("1");
        Json result = Json::object();
        result["protocolVersion"] = Json::string(kProtocolVersion);
        result["capabilities"] = std::move(capabilities);
        result["serverInfo"] = std::move(info);
        return rpc_result(id, std::move(result));
    }
    if (method == "tools/list") {
        Json tools = Json::array();
        tools.push(query_tool_descriptor());
        Json result = Json::object();
        result["tools"] = std::move(tools);
        return rpc_result(id, std::move(result));
    }
    if (method == "tools/call") {
        const Json& params = root.at("params");
        if (params.at("name").as_string("") != kQueryToolName) {
            return rpc_error(id, -32602, "unknown tool");
        }
        QueryToolCall call;
        std::string argument_error;
        if (!parse_query_tool_arguments(params.at("arguments"), &call, &argument_error)) {
            return tool_reply(id, "Scylla rejected the call: " + argument_error +
                                      ". No query ran and no rows were retrieved.",
                              true);
        }
        if (!call_broker) {
            return tool_reply(id, "The Scylla broker is unavailable. No query ran.", true);
        }
        const std::string response = call_broker(call);
        if (response.empty()) {
            return tool_reply(id,
                              "Scylla could not be reached, so the query did not run. Do not infer "
                              "or fabricate results.",
                              true);
        }
        const std::string text = format_query_tool_result(response);
        const bool failed = text.rfind("Scylla ", 0) == 0;
        return tool_reply(id, text, failed);
    }
    return rpc_error(id, -32601, "method not found");
}

std::string call_broker_over_pipe(const std::wstring& pipe_name, std::string_view token,
                                  const QueryToolCall& call, std::uint32_t timeout_ms) {
    if (pipe_name.empty()) return {};
    // The service recycles its pipe instance between clients, leaving a brief window where the name
    // does not exist. Retry briefly so back-to-back calls do not spuriously fail.
    const DWORD wait_ms = timeout_ms == 0 ? 5000 : timeout_ms;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        if (WaitNamedPipeW(pipe_name.c_str(), wait_ms)) {
            pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND && attempt + 1 < 3) Sleep(50);
            else if (GetLastError() != ERROR_PIPE_BUSY) break;
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) return {};

    std::string request = encode_broker_request(call, token);
    request.push_back('\n');
    DWORD written = 0;
    if (!WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) {
        CloseHandle(pipe);
        return {};
    }

    std::string response;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        response.append(buffer, read);
        if (response.size() > kBrokerMaxMessageBytes) break;
        if (response.find('\n') != std::string::npos) break;
    }
    CloseHandle(pipe);
    const auto newline = response.find('\n');
    if (newline != std::string::npos) response.resize(newline);
    return response;
}

int run_broker_mcp_helper() {
    // GUI-subsystem process: stdio only exists because the parent redirected it. Bail out rather
    // than sit on a pipe that will never carry MCP traffic.
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE || !output || output == INVALID_HANDLE_VALUE) {
        return 2;
    }

    wchar_t token_buffer[256]{};
    GetEnvironmentVariableW(utf16(kBrokerTokenEnvVar).c_str(), token_buffer, 256);
    const std::string token = utf8(token_buffer);
    if (token.empty()) return 5;
    // Derived, not transmitted. The config writer rewrites backslashes to forward slashes in every
    // value it emits, which would corrupt a literal \\.\pipe\ name; the hex token survives intact.
    const std::wstring pipe_name = utf16(std::string(kBrokerPipePrefix) + token);

    const BrokerCallFn call = [&pipe_name, &token](const QueryToolCall& request) {
        // Generous: the Workbench may be waiting on the user to approve.
        return call_broker_over_pipe(pipe_name, token, request, 120000);
    };

    JsonlDecoder decoder;
    std::string error;
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(input, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        std::vector<std::string> lines;
        if (!decoder.feed(buffer, read, lines, &error)) return 3;
        for (const auto& line : lines) {
            if (line.empty()) continue;
            const McpReply reply = handle_mcp_message(line, call);
            if (!reply.has_body) continue;
            std::string body = reply.body;
            body.push_back('\n');
            DWORD written = 0;
            // No FlushFileBuffers here: on a pipe it blocks until the reader has drained everything,
            // which deadlocks a client that only reads after sending several requests.
            if (!WriteFile(output, body.data(), static_cast<DWORD>(body.size()), &written, nullptr)) {
                return 4;
            }
        }
    }
    return 0;
}

}  // namespace scyllagpt
