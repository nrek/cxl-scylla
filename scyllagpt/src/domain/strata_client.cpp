#include "scyllagpt/strata_client.h"

#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#include <winhttp.h>

#include <algorithm>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace scyllagpt {
namespace {

constexpr wchar_t kStrataCredentialTarget[] = L"ScyllaGPT/STRATA Remote API Key";

bool store_strata_credential(const std::wstring& value) {
    if (value.empty()) return CredDeleteW(kStrataCredentialTarget, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(kStrataCredentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"STRATA");
    return CredWriteW(&credential, 0) != FALSE;
}

std::wstring load_strata_credential() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kStrataCredentialTarget, CRED_TYPE_GENERIC, 0, &credential) || !credential) return {};
    std::wstring value(reinterpret_cast<const wchar_t*>(credential->CredentialBlob),
                       credential->CredentialBlobSize / sizeof(wchar_t));
    CredFree(credential);
    return value;
}

std::string read_all(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return {};
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    return raw;
}

bool write_all(const std::wstring& path, const std::string& body) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

struct ParsedUrl {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 80;
    std::wstring path = L"/";
};

bool parse_endpoint(const std::wstring& endpoint, ParsedUrl* out) {
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
    out->port = uc.nPort ? uc.nPort : (out->https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    out->path.assign(path, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) {
        out->path.append(extra, uc.dwExtraInfoLength);
    }
    if (out->path.empty()) {
        out->path = L"/";
    }
    // Trim trailing slash from base endpoint path so we can append /api/...
    while (out->path.size() > 1 && out->path.back() == L'/') {
        out->path.pop_back();
    }
    return !out->host.empty();
}

std::wstring join_url_path(const std::wstring& base_path, const wchar_t* suffix) {
    std::wstring p = base_path;
    if (p.empty() || p == L"/") {
        return suffix;
    }
    if (p.back() == L'/') {
        p.pop_back();
    }
    return p + suffix;
}

struct HttpResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string message;
};

HttpResult http_request(const StrataSettings& settings, const wchar_t* method, const wchar_t* path_suffix,
                        const std::string* body_utf8, DWORD timeout_ms) {
    HttpResult r;
    ParsedUrl url;
    if (!parse_endpoint(settings.endpoint, &url)) {
        r.message = "invalid strata endpoint";
        return r;
    }
    const std::wstring object = join_url_path(url.path, path_suffix);

    HINTERNET session = WinHttpOpen(L"ScyllaGPT-Strata/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        r.message = "WinHttpOpen failed";
        return r;
    }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET conn = WinHttpConnect(session, url.host.c_str(), url.port, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        r.message = "connect failed";
        return r;
    }

    DWORD flags = url.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, method, object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        r.message = "OpenRequest failed";
        return r;
    }

    std::wstring headers = L"Accept: application/json\r\n";
    if (!settings.bearer.empty()) {
        headers += L"Authorization: Bearer ";
        headers += settings.bearer;
        headers += L"\r\n";
    }
    if (body_utf8 && !body_utf8->empty()) {
        headers += L"Content-Type: application/json\r\n";
    }

    const void* send_body = WINHTTP_NO_REQUEST_DATA;
    DWORD send_len = 0;
    if (body_utf8 && !body_utf8->empty()) {
        send_body = body_utf8->data();
        send_len = static_cast<DWORD>(body_utf8->size());
    }

    const BOOL sent = WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L), const_cast<void*>(send_body),
                                         send_len, send_len, 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        const DWORD err = GetLastError();
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        r.message = "request failed (server down?) err=" + std::to_string(err);
        return r;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &status_size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);

    std::string response;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            break;
        }
        if (avail == 0) {
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

StrataHit hit_from_json(const Json& j) {
    StrataHit h;
    if (!j.is_object()) {
        return h;
    }
    h.id = j.at("id").as_string("");
    if (h.id.empty()) {
        h.id = j.at("path").as_string("");
    }
    h.kind = j.at("kind").as_string("");
    h.title = j.at("title").as_string("");
    if (h.title.empty()) {
        h.title = j.at("name").as_string("");
    }
    h.path = j.at("path").as_string("");
    h.project = j.at("project").as_string("");
    h.source = j.at("source").as_string("");
    return h;
}

void collect_hits(const Json& node, std::vector<StrataHit>* out) {
    if (!out) {
        return;
    }
    if (node.is_array()) {
        for (const Json& item : node.array_items()) {
            if (item.is_object()) {
                out->push_back(hit_from_json(item));
            }
        }
        return;
    }
    if (!node.is_object()) {
        return;
    }
    if (node.at("results").is_array()) {
        collect_hits(node.at("results"), out);
        return;
    }
    if (node.at("items").is_array()) {
        collect_hits(node.at("items"), out);
        return;
    }
    if (node.at("documents").is_array()) {
        collect_hits(node.at("documents"), out);
    }
}

}  // namespace

bool StrataClient::load(const std::wstring& path) {
    bindings_.clear();
    settings_ = StrataSettings{};
    const std::string raw = read_all(path);
    if (raw.empty()) {
        return true;
    }
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return false;
    }
    settings_.enabled = j.at("enabled").as_bool(false);
    settings_.team = j.at("mode").as_string("solo") == "team";
    if (j.at("endpoint").is_string()) {
        settings_.endpoint = utf16(j.at("endpoint").as_string());
    }
    settings_.bearer = load_strata_credential();
    if (settings_.bearer.empty() && j.at("bearer").is_string()) {
        settings_.bearer = utf16(j.at("bearer").as_string());
        store_strata_credential(settings_.bearer); // migrate legacy plaintext on the next save
    }
    const Json& arr = j.at("bindings");
    if (arr.is_array()) {
        for (const Json& item : arr.array_items()) {
            if (!item.is_object()) {
                continue;
            }
            StrataBinding b;
            b.project_id = item.at("project_id").as_string("");
            b.org = item.at("org").as_string("");
            b.client = item.at("client").as_string("");
            b.strata_project = item.at("strata_project").as_string("");
            b.repo = item.at("repo").as_string("");
            if (!b.project_id.empty()) {
                bindings_.push_back(std::move(b));
            }
        }
    }
    return true;
}

bool StrataClient::save(const std::wstring& path) const {
    if (!store_strata_credential(settings_.bearer)) return false;
    Json j = Json::object();
    j["version"] = Json::number(2);
    j["enabled"] = Json::boolean(settings_.enabled);
    j["mode"] = Json::string(settings_.team ? "team" : "solo");
    j["endpoint"] = Json::string(utf8(settings_.endpoint));
    Json arr = Json::array();
    for (const auto& b : bindings_) {
        Json bj = Json::object();
        bj["project_id"] = Json::string(b.project_id);
        bj["org"] = Json::string(b.org);
        bj["client"] = Json::string(b.client);
        bj["strata_project"] = Json::string(b.strata_project);
        bj["repo"] = Json::string(b.repo);
        arr.push(std::move(bj));
    }
    j["bindings"] = std::move(arr);
    return write_all(path, j.dump());
}

StrataBinding* StrataClient::binding_for(const std::string& project_id) {
    for (auto& b : bindings_) {
        if (b.project_id == project_id) {
            return &b;
        }
    }
    return nullptr;
}

const StrataBinding* StrataClient::binding_for(const std::string& project_id) const {
    for (const auto& b : bindings_) {
        if (b.project_id == project_id) {
            return &b;
        }
    }
    return nullptr;
}

void StrataClient::upsert_binding(const StrataBinding& b) {
    if (b.project_id.empty()) {
        return;
    }
    if (StrataBinding* existing = binding_for(b.project_id)) {
        *existing = b;
        return;
    }
    bindings_.push_back(b);
}

bool StrataClient::remove_binding(const std::string& project_id) {
    for (auto it = bindings_.begin(); it != bindings_.end(); ++it) {
        if (it->project_id == project_id) {
            bindings_.erase(it);
            return true;
        }
    }
    return false;
}

StrataHealth StrataClient::health_check() const {
    StrataHealth h;
    HttpResult stats = http_request(settings_, L"GET", L"/api/stats", nullptr, 3000);
    if (stats.ok) {
        h.ok = true;
        h.http_status = stats.status;
        h.message = "ok";
        std::string err;
        h.body = Json::parse(stats.body, &err);
        if (!err.empty()) {
            h.body = Json::null();
        }
        return h;
    }
    HttpResult health = http_request(settings_, L"GET", L"/health", nullptr, 3000);
    h.http_status = health.status ? health.status : stats.status;
    if (health.ok) {
        h.ok = true;
        h.message = "ok";
        std::string err;
        h.body = Json::parse(health.body, &err);
        if (!err.empty()) {
            h.body = Json::null();
        }
        return h;
    }
    h.ok = false;
    h.message = !health.message.empty() ? health.message : stats.message;
    if (h.message.empty()) {
        h.message = "strata unreachable";
    }
    return h;
}

StrataSearchResult StrataClient::search(const std::string& query, int limit, const std::string& project_filter) const {
    StrataSearchResult out;
    if (limit < 1) {
        limit = 1;
    }
    if (limit > 500) {
        limit = 500;
    }
    Json body = Json::object();
    body["q"] = Json::string(query);
    body["limit"] = Json::number(limit);
    body["source"] = Json::string("local");
    if (!project_filter.empty()) {
        body["project"] = Json::string(project_filter);
    }
    const std::string payload = body.dump();
    HttpResult res = http_request(settings_, L"POST", L"/api/query", &payload, 8000);
    out.http_status = res.status;
    if (!res.ok) {
        // Fallback GET with query string for older/alternate endpoints.
        std::wstring qpath = L"/api/query?q=";
        // Minimal URL-encode for spaces / reserved.
        for (unsigned char ch : query) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                ch == '_' || ch == '.' || ch == '~') {
                qpath.push_back(static_cast<wchar_t>(ch));
            } else if (ch == ' ') {
                qpath += L"%20";
            } else {
                wchar_t hex[8]{};
                swprintf_s(hex, L"%%%02X", ch);
                qpath += hex;
            }
        }
        qpath += L"&limit=" + std::to_wstring(limit);
        if (!project_filter.empty()) {
            qpath += L"&project=";
            for (unsigned char ch : project_filter) {
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                    qpath.push_back(static_cast<wchar_t>(ch));
                } else {
                    wchar_t hex[8]{};
                    swprintf_s(hex, L"%%%02X", ch);
                    qpath += hex;
                }
            }
        }
        HttpResult get = http_request(settings_, L"GET", qpath.c_str(), nullptr, 8000);
        out.http_status = get.status ? get.status : res.status;
        if (!get.ok) {
            out.ok = false;
            out.message = !get.message.empty() ? get.message : res.message;
            if (out.message.empty()) {
                out.message = "strata unreachable";
            }
            return out;
        }
        res = std::move(get);
    }

    std::string err;
    out.raw = Json::parse(res.body, &err);
    if (!err.empty()) {
        out.ok = false;
        out.message = "invalid json from strata";
        return out;
    }
    collect_hits(out.raw, &out.hits);
    out.ok = true;
    out.message = "ok";
    return out;
}

}  // namespace scyllagpt
