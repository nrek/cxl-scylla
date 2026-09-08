#pragma once

// MCP OAuth 2.1 client (HTTP transports): PRM discovery, AS metadata, DCR, PKCE loopback,
// CredMan token storage, silent refresh. Tokens never enter mcp.json or agent context.

#include "scyllagpt/mcp_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace scyllagpt {

struct McpOAuthTokens {
    std::string access_token;
    std::string refresh_token;
    std::string token_type = "Bearer";
    std::int64_t expires_at_unix = 0;  // 0 = unknown
    std::string scope;
    // Client registration + endpoints needed for refresh without rediscovery.
    std::string client_id;
    std::string client_secret;  // usually empty for public clients
    std::string token_endpoint;
    std::string authorization_endpoint;
    std::string resource;  // canonical MCP resource URI (RFC 8707)
};

struct McpOAuthResult {
    bool ok = false;
    McpAuthState state = McpAuthState::Unknown;
    std::string message;
    McpOAuthTokens tokens;
};

// CredMan target: ScyllaGPT/MCP/<connection_id>
bool mcp_oauth_cred_save(const std::string& connection_id, const McpOAuthTokens& tokens);
bool mcp_oauth_cred_load(const std::string& connection_id, McpOAuthTokens* out);
bool mcp_oauth_cred_clear(const std::string& connection_id);
bool mcp_oauth_cred_present(const std::string& connection_id);

// Unit-testable helpers.
std::string mcp_oauth_base64url(const unsigned char* data, std::size_t n);
std::string mcp_oauth_pkce_challenge_s256(const std::string& verifier);
bool mcp_oauth_parse_www_authenticate(const std::string& header, std::string* resource_metadata_url,
                                      std::string* scope_out);

// Probe the MCP HTTP endpoint with an optional Bearer token.
// Sets state to Healthy on 2xx, NeedsReauth on 401, Offline otherwise.
McpOAuthResult mcp_oauth_probe(const std::wstring& endpoint, const std::string& access_token);

// Full interactive authorize (blocks until browser returns or timeout). HWND is for ShellExecute owner.
McpOAuthResult mcp_oauth_authorize(HWND owner, const std::wstring& endpoint,
                                   const std::string& connection_id, DWORD timeout_ms = 300000);

// Silent refresh when a refresh_token is stored. Falls back to NeedsReauth on failure.
McpOAuthResult mcp_oauth_refresh(const std::string& connection_id);

// Check Now: try refresh if near/expired, else probe with current access token.
McpOAuthResult mcp_oauth_check_now(const std::wstring& endpoint, const std::string& connection_id);

// Background-friendly: refresh if needed then probe; never opens a browser.
McpOAuthResult mcp_oauth_freshness_check(const std::wstring& endpoint, const std::string& connection_id);

}  // namespace scyllagpt
