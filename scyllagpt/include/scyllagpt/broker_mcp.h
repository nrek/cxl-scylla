#pragma once

// The agent-facing side of the broker: a minimal MCP server exposing exactly one tool.
//
// This runs in a *secret-free* helper process. It can name a connection alias and carry SQL, but it
// holds no Keyring, no credentials, and no database or SSH configuration. Everything it learns comes
// back from the Workbench over the broker pipe, already bounded by policy.

#include "scyllagpt/broker_protocol.h"

#include <functional>
#include <string>
#include <string_view>

namespace scyllagpt {

// Performs one brokered query and returns the raw response line (see encode_broker_response).
// Returning an empty string means the transport itself failed.
using BrokerCallFn = std::function<std::string(const QueryToolCall&)>;

struct McpReply {
    bool has_body = false;  // notifications produce no reply
    std::string body;
};

// Handles one newline-delimited JSON-RPC message. Pure apart from `call_broker`, so the whole
// agent-facing protocol is testable without stdio, a pipe, or a window.
McpReply handle_mcp_message(std::string_view line, const BrokerCallFn& call_broker);

// stdio server loop for the `--mcp-query-broker` process mode. Reads JSON-RPC from stdin, writes
// replies to stdout, and forwards tool calls to the Workbench over the pipe named by
// kBrokerPipeEnvVar, authenticating with kBrokerTokenEnvVar.
int run_broker_mcp_helper();

// Single request/response exchange against a running broker pipe. Empty return means failure.
std::string call_broker_over_pipe(const std::wstring& pipe_name, std::string_view token,
                                  const QueryToolCall& call, std::uint32_t timeout_ms);

}  // namespace scyllagpt
