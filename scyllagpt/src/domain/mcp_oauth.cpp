#include "scyllagpt/mcp_oauth.h"

#include "scyllagpt/json.h"
#include "scyllagpt/utf.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincred.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")

namespace scyllagpt {
namespace {

constexpr wchar_t kCredPrefix[] = L"ScyllaGPT/MCP/";
constexpr char kUserAgent[] = "Scylla-Workbench-MCP-OAuth/1.0";

struct ParsedUrl {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 443;
    std::wstring path = L"/";
    std::wstring origin;  // scheme://host:port
};

bool parse_url(const std::wstring& endpoint, ParsedUrl* out) {
    if (!out || endpoint.empty()) {
        return false;
    }
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    wchar_t extra[1024]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(endpoint.c_str(), 0, 0, &uc)) {
        return false;
    }
    out->https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out->host.assign(host, uc.dwHostNameLength);
    out->port = uc.nPort ? uc.nPort
                         : (out->https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    out->path.assign(path, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) {
        out->path.append(extra, uc.dwExtraInfoLength);
    }
    if (out->path.empty()) {
        out->path = L"/";
    }
    out->origin = out->https ? L"https://" : L"http://";
    out->origin += out->host;
    if ((out->https && out->port != 443) || (!out->https && out->port != 80)) {
        out->origin += L":";
        out->origin += std::to_wstring(out->port);
    }
    return !out->host.empty();
}

std::wstring cred_target(const std::string& connection_id) {
    return std::wstring(kCredPrefix) + utf16(connection_id);
}

std::int64_t unix_now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

bool random_bytes(unsigned char* out, ULONG n) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

std::string random_urlsafe(std::size_t nbytes) {
    std::vector<unsigned char> buf(nbytes);
    if (!random_bytes(buf.data(), static_cast<ULONG>(nbytes))) {
        return {};
    }
    return mcp_oauth_base64url(buf.data(), buf.size());
}

std::string sha256_raw(const std::string& in) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string digest(32, '\0');
    DWORD obj_len = 0;
    DWORD cb = 0;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return {};
    }
    if (!BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                                          sizeof(obj_len), &cb, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::vector<UCHAR> obj(obj_len);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, obj.data(), obj_len, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(in.data())),
                   static_cast<ULONG>(in.size()), 0);
    BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), 32, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return digest;
}

std::wstring url_encode_w(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return utf16(out);
}

struct HttpExchange {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string www_authenticate;
    std::string message;
};

HttpExchange http_exchange(const std::wstring& url, const wchar_t* method, const std::string* body,
                           const std::wstring& content_type, const std::string& bearer,
                           DWORD timeout_ms) {
    HttpExchange r;
    ParsedUrl u;
    if (!parse_url(url, &u)) {
        r.message = "invalid URL";
        return r;
    }
    HINTERNET session =
        WinHttpOpen(utf16(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        r.message = "WinHttpOpen failed";
        return r;
    }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    HINTERNET conn = WinHttpConnect(session, u.host.c_str(), u.port, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        r.message = "connect failed";
        return r;
    }
    DWORD flags = u.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, method, u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        r.message = "OpenRequest failed";
        return r;
    }

    std::wstring headers = L"Accept: application/json, text/event-stream\r\n";
    if (!bearer.empty()) {
        headers += L"Authorization: Bearer ";
        headers += utf16(bearer);
        headers += L"\r\n";
    }
    if (body && !body->empty() && !content_type.empty()) {
        headers += L"Content-Type: ";
        headers += content_type;
        headers += L"\r\n";
    }

    const void* send_body = WINHTTP_NO_REQUEST_DATA;
    DWORD send_len = 0;
    if (body && !body->empty()) {
        send_body = body->data();
        send_len = static_cast<DWORD>(body->size());
    }
    if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L), const_cast<void*>(send_body),
                            send_len, send_len, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        r.message = "request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return r;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);

    wchar_t www[4096]{};
    DWORD www_len = sizeof(www);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_WWW_AUTHENTICATE, WINHTTP_HEADER_NAME_BY_INDEX, www, &www_len,
                            WINHTTP_NO_HEADER_INDEX)) {
        r.www_authenticate = utf8(www);
    }

    std::string response;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) {
            break;
        }
        if (response.size() + avail > 4 * 1024 * 1024) {
            break;
        }
        const std::size_t off = response.size();
        response.resize(off + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, response.data() + off, avail, &read)) {
            response.resize(off);
            break;
        }
        response.resize(off + read);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    r.body = std::move(response);
    r.ok = (r.status >= 200 && r.status < 300);
    if (!r.ok && r.message.empty()) {
        r.message = "HTTP " + std::to_string(r.status);
    }
    return r;
}

std::wstring join_origin_path(const ParsedUrl& u, const std::wstring& path) {
    std::wstring out = u.origin;
    if (!path.empty() && path[0] != L'/') {
        out += L'/';
    }
    out += path;
    return out;
}

std::vector<std::wstring> prm_candidates(const std::wstring& endpoint, const std::string& from_header) {
    std::vector<std::wstring> out;
    if (!from_header.empty()) {
        out.push_back(utf16(from_header));
    }
    ParsedUrl u;
    if (!parse_url(endpoint, &u)) {
        return out;
    }
    // Path-aware then root well-known (RFC9728).
    std::wstring path = u.path;
    if (const auto query = path.find_first_of(L"?#"); query != std::wstring::npos) path.resize(query);
    while (!path.empty() && path.back() == L'/') {
        path.pop_back();
    }
    if (!path.empty() && path != L"/") {
        out.push_back(u.origin + L"/.well-known/oauth-protected-resource" + path);
    }
    out.push_back(u.origin + L"/.well-known/oauth-protected-resource");
    return out;
}

struct AuthServerMeta {
    std::string issuer;
    std::string authorization_endpoint;
    std::string token_endpoint;
    std::string registration_endpoint;
};

bool load_as_metadata(const std::string& as_url, AuthServerMeta* out, std::string* err) {
    const std::wstring base = utf16(as_url);
    ParsedUrl u;
    if (!parse_url(base, &u)) {
        if (err) {
            *err = "bad authorization server URL";
        }
        return false;
    }
    // Strip path to issuer root for well-known; keep as-is if already a metadata URL.
    std::vector<std::wstring> tries;
    if (base.find(L"/.well-known/") != std::wstring::npos) {
        tries.push_back(base);
    } else {
        std::wstring issuer = u.origin;
        // If AS URL includes a path component that is the issuer, include it.
        std::wstring path = u.path;
        while (!path.empty() && path.back() == L'/') {
            path.pop_back();
        }
        if (!path.empty() && path != L"/") {
            issuer += path;
            tries.push_back(u.origin + L"/.well-known/oauth-authorization-server" + path);
        }
        tries.push_back(issuer + L"/.well-known/oauth-authorization-server");
        tries.push_back(issuer + L"/.well-known/openid-configuration");
    }
    for (const auto& url : tries) {
        auto r = http_exchange(url, L"GET", nullptr, L"", "", 20000);
        if (!r.ok) {
            continue;
        }
        std::string jerr;
        Json j = Json::parse(r.body, &jerr);
        if (!jerr.empty() || !j.is_object()) {
            continue;
        }
        out->issuer = j.at("issuer").as_string(as_url.c_str());
        out->authorization_endpoint = j.at("authorization_endpoint").as_string("");
        out->token_endpoint = j.at("token_endpoint").as_string("");
        out->registration_endpoint = j.at("registration_endpoint").as_string("");
        if (!out->authorization_endpoint.empty() && !out->token_endpoint.empty()) {
            return true;
        }
    }
    if (err) {
        *err = "authorization server metadata not found";
    }
    return false;
}

struct ClientReg {
    std::string client_id;
    std::string client_secret;
};

bool dynamic_register(const AuthServerMeta& as, const std::string& redirect_uri, ClientReg* out,
                      std::string* err) {
    if (as.registration_endpoint.empty()) {
        if (err) {
            *err = "authorization server does not advertise dynamic client registration";
        }
        return false;
    }
    Json body = Json::object();
    body["client_name"] = Json::string("Scylla Workbench");
    Json uris = Json::array();
    uris.push(Json::string(redirect_uri));
    body["redirect_uris"] = std::move(uris);
    Json grants = Json::array();
    grants.push(Json::string("authorization_code"));
    grants.push(Json::string("refresh_token"));
    body["grant_types"] = std::move(grants);
    Json responses = Json::array();
    responses.push(Json::string("code"));
    body["response_types"] = std::move(responses);
    body["token_endpoint_auth_method"] = Json::string("none");
    body["application_type"] = Json::string("native");

    const std::string payload = body.dump();
    auto r = http_exchange(utf16(as.registration_endpoint), L"POST", &payload, L"application/json", "", 30000);
    if (!r.ok) {
        if (err) {
            *err = "dynamic client registration failed: " + r.message;
            if (!r.body.empty() && r.body.size() < 400) {
                *err += " — ";
                *err += r.body;
            }
        }
        return false;
    }
    std::string jerr;
    Json j = Json::parse(r.body, &jerr);
    out->client_id = j.at("client_id").as_string("");
    out->client_secret = j.at("client_secret").as_string("");
    if (out->client_id.empty()) {
        if (err) {
            *err = "registration response missing client_id";
        }
        return false;
    }
    return true;
}

struct LoopbackResult {
    bool ok = false;
    std::string code;
    std::string state;
    std::string error;
    std::string error_description;
};

bool ensure_winsock() {
    static bool inited = false;
    static bool ok = false;
    if (!inited) {
        WSADATA wsa{};
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
        inited = true;
    }
    return ok;
}

// Bind 127.0.0.1:0, return listening socket + chosen port.
SOCKET start_loopback(int* port_out) {
    if (!ensure_winsock() || !port_out) {
        return INVALID_SOCKET;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    if (listen(s, 1) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    sockaddr_in bound{};
    int len = sizeof(bound);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    *port_out = ntohs(bound.sin_port);
    return s;
}

std::string query_param(const std::string& query, const char* key) {
    const std::string needle = std::string(key) + "=";
    std::size_t pos = 0;
    while (pos < query.size()) {
        const std::size_t amp = query.find('&', pos);
        const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (part.rfind(needle, 0) == 0) {
            std::string v = part.substr(needle.size());
            // Minimal percent-decode for code/state.
            std::string out;
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '%' && i + 2 < v.size()) {
                    auto hex = [](char c) -> int {
                        if (c >= '0' && c <= '9') {
                            return c - '0';
                        }
                        if (c >= 'a' && c <= 'f') {
                            return 10 + c - 'a';
                        }
                        if (c >= 'A' && c <= 'F') {
                            return 10 + c - 'A';
                        }
                        return -1;
                    };
                    const int hi = hex(v[i + 1]);
                    const int lo = hex(v[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        out.push_back(static_cast<char>((hi << 4) | lo));
                        i += 2;
                        continue;
                    }
                }
                if (v[i] == '+') {
                    out.push_back(' ');
                } else {
                    out.push_back(v[i]);
                }
            }
            return out;
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return {};
}

LoopbackResult accept_callback(SOCKET listen_sock, const std::string& expect_state, DWORD timeout_ms) {
    LoopbackResult out;
    // Non-blocking select with timeout.
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listen_sock, &fds);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout_ms / 1000);
    tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
    const int sel = select(0, &fds, nullptr, nullptr, &tv);
    if (sel <= 0) {
        out.error = "timeout";
        out.error_description = "Timed out waiting for the browser to return.";
        return out;
    }
    SOCKET client = accept(listen_sock, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        out.error = "accept_failed";
        return out;
    }
    char buf[8192]{};
    const int n = recv(client, buf, sizeof(buf) - 1, 0);
    std::string req = n > 0 ? std::string(buf, buf + n) : std::string();
    // GET /callback?code=...&state=... HTTP/1.1
    std::string path;
    if (req.rfind("GET ", 0) == 0) {
        const std::size_t sp = req.find(' ', 4);
        path = req.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
    }
    std::string query;
    const std::size_t q = path.find('?');
    if (q != std::string::npos) {
        query = path.substr(q + 1);
    }
    out.code = query_param(query, "code");
    out.state = query_param(query, "state");
    out.error = query_param(query, "error");
    out.error_description = query_param(query, "error_description");
    out.ok = !out.code.empty() && out.error.empty() &&
             (expect_state.empty() || out.state == expect_state);

    const char* html = out.ok ? "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                "Connection: close\r\n\r\n"
                                "<!doctype html><title>Scylla</title>"
                                "<body style='font-family:sans-serif;padding:2rem'>"
                                "<h1>Signed in</h1><p>You can close this window and return to Scylla Workbench.</p>"
                                "</body>"
                              : "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\n"
                                "Connection: close\r\n\r\n"
                                "<!doctype html><title>Scylla</title>"
                                "<body style='font-family:sans-serif;padding:2rem'>"
                                "<h1>Sign-in failed</h1><p>Return to Scylla Workbench and try again.</p>"
                                "</body>";
    send(client, html, static_cast<int>(std::strlen(html)), 0);
    closesocket(client);
    return out;
}

bool tokens_from_json(const Json& j, McpOAuthTokens* t, std::string* err) {
    t->access_token = j.at("access_token").as_string("");
    t->refresh_token = j.at("refresh_token").as_string(t->refresh_token.c_str());
    t->token_type = j.at("token_type").as_string("Bearer");
    t->scope = j.at("scope").as_string(t->scope.c_str());
    const std::int64_t expires_in = j.at("expires_in").as_int(0);
    if (expires_in > 0) {
        t->expires_at_unix = unix_now() + expires_in;
    }
    if (t->access_token.empty()) {
        if (err) {
            *err = "token response missing access_token";
        }
        return false;
    }
    return true;
}

McpOAuthResult exchange_code(const AuthServerMeta& as, const ClientReg& client, const std::string& code,
                             const std::string& redirect_uri, const std::string& verifier,
                             const std::string& resource) {
    McpOAuthResult r;
    std::ostringstream form;
    form << "grant_type=authorization_code"
         << "&code=" << utf8(url_encode_w(code))
         << "&redirect_uri=" << utf8(url_encode_w(redirect_uri))
         << "&client_id=" << utf8(url_encode_w(client.client_id))
         << "&code_verifier=" << utf8(url_encode_w(verifier));
    if (!resource.empty()) {
        form << "&resource=" << utf8(url_encode_w(resource));
    }
    if (!client.client_secret.empty()) {
        form << "&client_secret=" << utf8(url_encode_w(client.client_secret));
    }
    const std::string body = form.str();
    auto http = http_exchange(utf16(as.token_endpoint), L"POST", &body,
                              L"application/x-www-form-urlencoded", "", 30000);
    if (!http.ok) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "token exchange failed: " + http.message;
        return r;
    }
    std::string jerr;
    Json j = Json::parse(http.body, &jerr);
    if (!tokens_from_json(j, &r.tokens, &r.message)) {
        r.state = McpAuthState::NeedsReauth;
        return r;
    }
    r.tokens.client_id = client.client_id;
    r.tokens.client_secret = client.client_secret;
    r.tokens.token_endpoint = as.token_endpoint;
    r.tokens.authorization_endpoint = as.authorization_endpoint;
    r.tokens.resource = resource;
    r.ok = true;
    r.state = McpAuthState::Healthy;
    r.message = "Authenticated";
    return r;
}

std::string canonical_resource(const std::wstring& endpoint) {
    // RFC 8707 resource parameter: the MCP endpoint URI without fragment.
    std::string s = utf8(endpoint);
    const auto hash = s.find('#');
    if (hash != std::string::npos) {
        s.resize(hash);
    }
    while (s.size() > 8 && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

}  // namespace

std::string mcp_oauth_base64url(const unsigned char* data, std::size_t n) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    b64.reserve(((n + 2) / 3) * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const unsigned int v = (static_cast<unsigned int>(data[i]) << 16) |
                               ((i + 1 < n ? static_cast<unsigned int>(data[i + 1]) : 0u) << 8) |
                               (i + 2 < n ? static_cast<unsigned int>(data[i + 2]) : 0u);
        b64.push_back(table[(v >> 18) & 63]);
        b64.push_back(table[(v >> 12) & 63]);
        b64.push_back(i + 1 < n ? table[(v >> 6) & 63] : '=');
        b64.push_back(i + 2 < n ? table[v & 63] : '=');
    }
    // URL-safe, no padding.
    for (char& c : b64) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!b64.empty() && b64.back() == '=') {
        b64.pop_back();
    }
    return b64;
}

std::string mcp_oauth_pkce_challenge_s256(const std::string& verifier) {
    const std::string dig = sha256_raw(verifier);
    if (dig.size() != 32) {
        return {};
    }
    return mcp_oauth_base64url(reinterpret_cast<const unsigned char*>(dig.data()), dig.size());
}

bool mcp_oauth_parse_www_authenticate(const std::string& header, std::string* resource_metadata_url,
                                      std::string* scope_out) {
    if (resource_metadata_url) {
        resource_metadata_url->clear();
    }
    if (scope_out) {
        scope_out->clear();
    }
    auto extract = [&](const char* key, std::string* dest) {
        if (!dest) {
            return;
        }
        const std::string k = std::string(key) + "=\"";
        const auto p = header.find(k);
        if (p == std::string::npos) {
            // Also allow unquoted.
            const std::string k2 = std::string(key) + "=";
            const auto p2 = header.find(k2);
            if (p2 == std::string::npos) {
                return;
            }
            std::size_t start = p2 + k2.size();
            std::size_t end = start;
            while (end < header.size() && header[end] != ',' && header[end] != ' ') {
                ++end;
            }
            *dest = header.substr(start, end - start);
            return;
        }
        const std::size_t start = p + k.size();
        const auto end = header.find('"', start);
        if (end == std::string::npos) {
            return;
        }
        *dest = header.substr(start, end - start);
    };
    extract("resource_metadata", resource_metadata_url);
    extract("scope", scope_out);
    return resource_metadata_url && !resource_metadata_url->empty();
}

bool mcp_oauth_cred_save(const std::string& connection_id, const McpOAuthTokens& tokens) {
    if (connection_id.empty() || tokens.access_token.empty()) {
        return false;
    }
    Json j = Json::object();
    j["access_token"] = Json::string(tokens.access_token);
    j["refresh_token"] = Json::string(tokens.refresh_token);
    j["token_type"] = Json::string(tokens.token_type);
    j["expires_at_unix"] = Json::number(tokens.expires_at_unix);
    j["scope"] = Json::string(tokens.scope);
    j["client_id"] = Json::string(tokens.client_id);
    j["client_secret"] = Json::string(tokens.client_secret);
    j["token_endpoint"] = Json::string(tokens.token_endpoint);
    j["authorization_endpoint"] = Json::string(tokens.authorization_endpoint);
    j["resource"] = Json::string(tokens.resource);
    const std::string blob = j.dump();
    std::wstring wide = utf16(blob);

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    std::wstring target = cred_target(connection_id);
    cred.TargetName = target.data();
    cred.CredentialBlobSize = static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(wide.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<wchar_t*>(L"mcp-oauth");
    return CredWriteW(&cred, 0) != 0;
}

bool mcp_oauth_cred_load(const std::string& connection_id, McpOAuthTokens* out) {
    if (!out || connection_id.empty()) {
        return false;
    }
    out->access_token.clear();
    PCREDENTIALW cred = nullptr;
    std::wstring target = cred_target(connection_id);
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred) {
        return false;
    }
    std::wstring wide;
    if (cred->CredentialBlobSize >= sizeof(wchar_t) && cred->CredentialBlob) {
        const std::size_t n = cred->CredentialBlobSize / sizeof(wchar_t);
        wide.assign(reinterpret_cast<wchar_t*>(cred->CredentialBlob), n);
        while (!wide.empty() && wide.back() == L'\0') {
            wide.pop_back();
        }
    }
    CredFree(cred);
    std::string jerr;
    Json j = Json::parse(utf8(wide), &jerr);
    if (!jerr.empty() || !j.is_object()) {
        return false;
    }
    out->access_token = j.at("access_token").as_string("");
    out->refresh_token = j.at("refresh_token").as_string("");
    out->token_type = j.at("token_type").as_string("Bearer");
    out->expires_at_unix = j.at("expires_at_unix").as_int(0);
    out->scope = j.at("scope").as_string("");
    out->client_id = j.at("client_id").as_string("");
    out->client_secret = j.at("client_secret").as_string("");
    out->token_endpoint = j.at("token_endpoint").as_string("");
    out->authorization_endpoint = j.at("authorization_endpoint").as_string("");
    out->resource = j.at("resource").as_string("");
    return !out->access_token.empty();
}

bool mcp_oauth_cred_clear(const std::string& connection_id) {
    if (connection_id.empty()) {
        return true;
    }
    std::wstring target = cred_target(connection_id);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != 0 || GetLastError() == ERROR_NOT_FOUND;
}

bool mcp_oauth_cred_present(const std::string& connection_id) {
    McpOAuthTokens t;
    return mcp_oauth_cred_load(connection_id, &t);
}

std::vector<std::string> mcp_oauth_discover_scopes(const std::wstring& endpoint, std::string* error) {
    std::vector<std::string> scopes;
    auto probe = http_exchange(endpoint, L"GET", nullptr, L"", "", 15000);
    std::string metadata, hint;
    mcp_oauth_parse_www_authenticate(probe.www_authenticate, &metadata, &hint);
    for (const auto& url : prm_candidates(endpoint, metadata)) {
        auto response = http_exchange(url, L"GET", nullptr, L"", "", 15000);
        if (!response.ok) continue;
        std::string parse_error;
        auto document = Json::parse(response.body, &parse_error);
        if (!parse_error.empty() || !document.has("authorization_servers")) continue;
        for (const auto& scope : document.at("scopes_supported").array_items()) {
            if (scope.is_string() && !scope.as_string().empty()) scopes.push_back(scope.as_string());
        }
        if (error) *error = scopes.empty()
            ? "This server does not advertise scopes. Enter documented scopes or choose permissions during provider consent."
            : "Select the permissions to request.";
        return scopes;
    }
    if (error) *error = "Could not discover scopes. Check the endpoint, or enter scopes from the provider documentation.";
    return scopes;
}

McpOAuthResult mcp_oauth_probe(const std::wstring& endpoint, const std::string& access_token) {
    McpOAuthResult r;
    if (endpoint.empty()) {
        r.state = McpAuthState::Offline;
        r.message = "Endpoint / command is empty";
        return r;
    }
    const std::string initialize = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"Scylla","version":"1.0"}}})";
    auto http = http_exchange(endpoint, L"POST", &initialize, L"application/json", access_token, 15000);
    if (http.status == 401 || http.status == 403) {
        r.state = McpAuthState::NeedsReauth;
        r.message = http.status == 401 ? "Authentication required" : "Insufficient permissions";
        return r;
    }
    if (http.status == 0) {
        r.state = McpAuthState::Offline;
        r.message = http.message.empty() ? "MCP endpoint unreachable" : http.message;
        return r;
    }
    if (!access_token.empty() && http.status >= 200 && http.status < 300 &&
        http.body.find("\"result\"") != std::string::npos) {
        r.ok = true;
        r.state = McpAuthState::Healthy;
        r.message = "Endpoint reachable; credentials accepted";
        return r;
    }
    if (http.status >= 200 && http.status < 300 && access_token.empty()) {
        r.ok = true;
        r.state = access_token.empty() ? McpAuthState::Unknown : McpAuthState::Healthy;
        r.message = "Endpoint reachable";
        return r;
    }
    r.state = McpAuthState::Offline;
    r.message = "HTTP " + std::to_string(http.status);
    return r;
}

McpOAuthResult mcp_oauth_authorize(HWND owner, const std::wstring& endpoint,
                                   const std::string& connection_id, DWORD timeout_ms,
                                   const std::vector<std::string>& scopes) {
    McpOAuthResult r;
    if (endpoint.empty()) {
        r.state = McpAuthState::Offline;
        r.message = "Endpoint is empty";
        return r;
    }
    if (connection_id.empty()) {
        r.message = "Missing connection id";
        return r;
    }

    // 1) Probe for WWW-Authenticate / confirm auth is needed.
    auto probe = http_exchange(endpoint, L"GET", nullptr, L"", "", 15000);
    if (probe.status == 0) {
        const std::string empty_obj = "{}";
        probe = http_exchange(endpoint, L"POST", &empty_obj, L"application/json", "", 15000);
    }
    std::string prm_url;
    std::string scope_hint;
    mcp_oauth_parse_www_authenticate(probe.www_authenticate, &prm_url, &scope_hint);
    // Only the user's selection determines requested authority, never a challenge hint.
    scope_hint.clear();
    for (const auto& scope : scopes) {
        if (!scope_hint.empty()) scope_hint += ' ';
        scope_hint += scope;
    }

    // 2) Protected Resource Metadata.
    Json prm;
    bool prm_ok = false;
    std::string last_err;
    for (const auto& cand : prm_candidates(endpoint, prm_url)) {
        auto http = http_exchange(cand, L"GET", nullptr, L"", "", 20000);
        if (!http.ok) {
            last_err = http.message;
            continue;
        }
        std::string jerr;
        prm = Json::parse(http.body, &jerr);
        if (jerr.empty() && prm.is_object() && prm.has("authorization_servers")) {
            prm_ok = true;
            break;
        }
        last_err = jerr.empty() ? "PRM missing authorization_servers" : jerr;
    }
    if (!prm_ok) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "Could not discover OAuth metadata for this MCP server. " + last_err;
        return r;
    }

    std::string as_url;
    const Json& servers = prm.at("authorization_servers");
    if (servers.is_array() && servers.size() > 0) {
        as_url = servers.at(0).as_string("");
    } else if (servers.is_string()) {
        as_url = servers.as_string("");
    }
    if (as_url.empty()) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "Protected resource metadata listed no authorization servers";
        return r;
    }
    std::string resource = prm.at("resource").as_string(canonical_resource(endpoint).c_str());

    AuthServerMeta as;
    if (!load_as_metadata(as_url, &as, &r.message)) {
        r.state = McpAuthState::NeedsReauth;
        return r;
    }

    int port = 0;
    SOCKET listen_sock = start_loopback(&port);
    if (listen_sock == INVALID_SOCKET || port <= 0) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "Could not start local OAuth callback listener on 127.0.0.1";
        return r;
    }
    const std::string redirect = "http://127.0.0.1:" + std::to_string(port) + "/callback";

    ClientReg client;
    if (!dynamic_register(as, redirect, &client, &r.message)) {
        closesocket(listen_sock);
        r.state = McpAuthState::NeedsReauth;
        return r;
    }

    const std::string verifier = random_urlsafe(32);
    const std::string challenge = mcp_oauth_pkce_challenge_s256(verifier);
    const std::string state = random_urlsafe(16);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        closesocket(listen_sock);
        r.message = "Failed to generate PKCE material";
        r.state = McpAuthState::NeedsReauth;
        return r;
    }

    std::wstring auth_url = utf16(as.authorization_endpoint);
    auth_url += (auth_url.find(L'?') == std::wstring::npos) ? L'?' : L'&';
    auth_url += L"response_type=code";
    auth_url += L"&client_id=";
    auth_url += url_encode_w(client.client_id);
    auth_url += L"&redirect_uri=";
    auth_url += url_encode_w(redirect);
    auth_url += L"&code_challenge=";
    auth_url += url_encode_w(challenge);
    auth_url += L"&code_challenge_method=S256";
    auth_url += L"&state=";
    auth_url += url_encode_w(state);
    auth_url += L"&resource=";
    auth_url += url_encode_w(resource);
    if (!scope_hint.empty()) {
        auth_url += L"&scope=";
        auth_url += url_encode_w(scope_hint);
    }

    const HINSTANCE shell = ShellExecuteW(owner, L"open", auth_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(shell) <= 32) {
        closesocket(listen_sock);
        r.state = McpAuthState::NeedsReauth;
        r.message = "Could not open the system browser for sign-in";
        return r;
    }

    LoopbackResult cb = accept_callback(listen_sock, state, timeout_ms);
    closesocket(listen_sock);
    if (!cb.ok) {
        r.state = McpAuthState::NeedsReauth;
        if (!cb.error.empty()) {
            r.message = cb.error;
            if (!cb.error_description.empty()) {
                r.message += ": ";
                r.message += cb.error_description;
            }
        } else {
            r.message = "Browser sign-in did not return an authorization code";
        }
        return r;
    }

    r = exchange_code(as, client, cb.code, redirect, verifier, resource);
    if (r.ok) {
        // RFC 6749: omitted scope means the requested scope was granted.
        if (r.tokens.scope.empty()) r.tokens.scope = scope_hint;
        if (!mcp_oauth_cred_save(connection_id, r.tokens)) {
            r.ok = false;
            r.state = McpAuthState::NeedsReauth;
            r.message = "Signed in, but Windows Credential Manager refused to store the tokens";
            return r;
        }
        r.message = "Signed in — tokens stored in Windows Credential Manager";
    }
    return r;
}

McpOAuthResult mcp_oauth_refresh(const std::string& connection_id) {
    McpOAuthResult r;
    McpOAuthTokens t;
    if (!mcp_oauth_cred_load(connection_id, &t) || t.refresh_token.empty() || t.token_endpoint.empty()) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "No refresh credential on file — reauthenticate";
        return r;
    }
    std::ostringstream form;
    form << "grant_type=refresh_token"
         << "&refresh_token=" << utf8(url_encode_w(t.refresh_token))
         << "&client_id=" << utf8(url_encode_w(t.client_id));
    if (!t.client_secret.empty()) {
        form << "&client_secret=" << utf8(url_encode_w(t.client_secret));
    }
    if (!t.resource.empty()) {
        form << "&resource=" << utf8(url_encode_w(t.resource));
    }
    const std::string body = form.str();
    auto http = http_exchange(utf16(t.token_endpoint), L"POST", &body, L"application/x-www-form-urlencoded", "",
                              30000);
    if (!http.ok) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "Refresh failed — reauthentication required";
        return r;
    }
    std::string jerr;
    Json j = Json::parse(http.body, &jerr);
    // Preserve refresh_token if the response omits a rotated one.
    const std::string old_refresh = t.refresh_token;
    if (!tokens_from_json(j, &t, &r.message)) {
        r.state = McpAuthState::NeedsReauth;
        return r;
    }
    if (t.refresh_token.empty()) {
        t.refresh_token = old_refresh;
    }
    if (!mcp_oauth_cred_save(connection_id, t)) {
        r.state = McpAuthState::NeedsReauth;
        r.message = "Refreshed, but could not update Credential Manager";
        return r;
    }
    r.ok = true;
    r.state = McpAuthState::Healthy;
    r.tokens = std::move(t);
    r.message = "Refreshed";
    return r;
}

McpOAuthResult mcp_oauth_check_now(const std::wstring& endpoint, const std::string& connection_id) {
    McpOAuthTokens t;
    const bool have = mcp_oauth_cred_load(connection_id, &t);
    if (have && !t.refresh_token.empty()) {
        const bool expired =
            t.expires_at_unix > 0 && t.expires_at_unix <= unix_now() + 60;  // refresh if <60s left
        if (expired || t.access_token.empty()) {
            auto refreshed = mcp_oauth_refresh(connection_id);
            if (refreshed.ok) {
                return mcp_oauth_probe(endpoint, refreshed.tokens.access_token);
            }
            // Fall through to probe — may still work; else NeedsReauth from refresh.
            if (refreshed.state == McpAuthState::NeedsReauth) {
                return refreshed;
            }
        }
    }
    return mcp_oauth_probe(endpoint, have ? t.access_token : std::string());
}

McpOAuthResult mcp_oauth_freshness_check(const std::wstring& endpoint, const std::string& connection_id) {
    return mcp_oauth_check_now(endpoint, connection_id);
}

}  // namespace scyllagpt
