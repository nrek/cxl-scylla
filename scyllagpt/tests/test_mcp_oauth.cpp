#include "scyllagpt/mcp_oauth.h"

#include <iostream>
#include <string>

static int g_fail = 0;

static void expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

int run_mcp_oauth_tests() {
    g_fail = 0;

    {
        const unsigned char raw[] = {0xfb, 0xef, 0xbe, 0xde};
        const std::string enc = scyllagpt::mcp_oauth_base64url(raw, sizeof(raw));
        // base64url must not use '+' '/' or '=' padding
        expect(enc.find('+') == std::string::npos && enc.find('/') == std::string::npos &&
                   enc.find('=') == std::string::npos,
               "base64url alphabet");
        expect(!enc.empty(), "base64url non-empty");
    }

    {
        // RFC 7636 appendix B example: verifier -> challenge
        const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
        const std::string challenge = scyllagpt::mcp_oauth_pkce_challenge_s256(verifier);
        expect(challenge == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "pkce s256 rfc7636");
    }

    {
        std::string meta;
        std::string scope;
        const std::string hdr =
            "Bearer realm=\"mcp\", "
            "resource_metadata=\"https://mcp.example.com/.well-known/oauth-protected-resource\", "
            "scope=\"mcp:tools\"";
        expect(scyllagpt::mcp_oauth_parse_www_authenticate(hdr, &meta, &scope), "www-auth parse");
        expect(meta == "https://mcp.example.com/.well-known/oauth-protected-resource",
               "www-auth resource_metadata");
        expect(scope == "mcp:tools", "www-auth scope");
    }

    {
        const std::string id = "test-mcp-oauth-cred-roundtrip";
        scyllagpt::mcp_oauth_cred_clear(id);
        scyllagpt::McpOAuthTokens t;
        t.access_token = "access-xyz";
        t.refresh_token = "refresh-xyz";
        t.client_id = "client-1";
        t.token_endpoint = "https://auth.example.com/token";
        t.authorization_endpoint = "https://auth.example.com/authorize";
        t.resource = "https://mcp.example.com/mcp";
        t.expires_at_unix = 1893456000;
        expect(scyllagpt::mcp_oauth_cred_save(id, t), "cred save");
        expect(scyllagpt::mcp_oauth_cred_present(id), "cred present");
        scyllagpt::McpOAuthTokens loaded;
        expect(scyllagpt::mcp_oauth_cred_load(id, &loaded), "cred load");
        expect(loaded.access_token == "access-xyz", "cred access");
        expect(loaded.refresh_token == "refresh-xyz", "cred refresh");
        expect(loaded.client_id == "client-1", "cred client_id");
        expect(loaded.resource == "https://mcp.example.com/mcp", "cred resource");
        expect(scyllagpt::mcp_oauth_cred_clear(id), "cred clear");
        expect(!scyllagpt::mcp_oauth_cred_present(id), "cred gone");
    }

    return g_fail;
}
