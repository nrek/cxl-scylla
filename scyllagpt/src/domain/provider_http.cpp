#include "scyllagpt/provider_http.h"

#include "scyllagpt/json.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/utf.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace scyllagpt {
namespace {

constexpr char kOpenAiUa[] = "Scylla-Workbench-OpenAI-API/1.0";
constexpr char kAnthropicUa[] = "Scylla-Workbench-Claude-API/1.0";
constexpr char kAnthropicVersion[] = "2023-06-01";

struct HttpResult {
    DWORD status = 0;
    std::string body;
    std::wstring error;
};

bool http_json(const wchar_t* host, INTERNET_PORT port, const wchar_t* method, const wchar_t* path,
               const std::wstring& extra_headers, const std::string& body_utf8, unsigned timeout_ms,
               HttpResult* out) {
    if (!out) {
        return false;
    }
    *out = {};
    HINTERNET session =
        WinHttpOpen(utf16(kOpenAiUa).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        out->error = L"WinHttpOpen failed";
        return false;
    }
    const int t = static_cast<int>((std::min)(timeout_ms, 600000u));
    WinHttpSetTimeouts(session, t, t, t, t);
    HINTERNET conn = WinHttpConnect(session, host, port, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        out->error = L"WinHttpConnect failed";
        return false;
    }
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET req = WinHttpOpenRequest(conn, method, path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        out->error = L"WinHttpOpenRequest failed";
        return false;
    }

    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    headers += extra_headers;
    if (!headers.empty() && headers.back() != L'\n') {
        headers += L"\r\n";
    }

    const void* send_body = body_utf8.empty() ? nullptr : body_utf8.data();
    const DWORD send_len = static_cast<DWORD>(body_utf8.size());
    if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L), const_cast<void*>(send_body),
                            send_len, send_len, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        out->error = L"HTTP request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    out->status = status;

    std::string response;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) {
            break;
        }
        const std::size_t off = response.size();
        response.resize(off + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, response.data() + off, avail, &read)) {
            break;
        }
        response.resize(off + read);
        if (response.size() > 16 * 1024 * 1024) {
            break;
        }
    }
    out->body = std::move(response);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return true;
}

std::wstring bearer_header(const std::wstring& key) {
    return L"Authorization: Bearer " + key + L"\r\n";
}

std::wstring anthropic_headers(const std::wstring& key) {
    return L"x-api-key: " + key + L"\r\n" + L"anthropic-version: " + utf16(kAnthropicVersion) + L"\r\n";
}

std::string display_from_id(const std::string& id) {
    return id;
}

bool looks_like_openai_chat_model(const std::string& id) {
    if (id.empty() || id.find("ft:") == 0 || id.find(":") != std::string::npos) {
        // Allow dated ids like gpt-4o-2024-08-06; reject org-scoped fine-tunes with colons after ft:
        if (id.find("ft:") == 0) {
            return false;
        }
    }
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (starts("gpt-") || starts("o1") || starts("o3") || starts("o4") || starts("chatgpt-")) {
        // Skip embeddings / audio / moderation / whisper / tts / davinci instruct leftovers when obvious.
        if (id.find("embedding") != std::string::npos || id.find("whisper") != std::string::npos ||
            id.find("tts") != std::string::npos || id.find("moderation") != std::string::npos ||
            id.find("transcribe") != std::string::npos || id.find("realtime") != std::string::npos ||
            id.find("image") != std::string::npos || id.find("dall-e") != std::string::npos) {
            return false;
        }
        return true;
    }
    return false;
}

std::wstring http_error_message(const HttpResult& r, const wchar_t* fallback) {
    if (!r.error.empty()) {
        return r.error;
    }
    std::string parse_err;
    Json j = Json::parse(r.body, &parse_err);
    if (parse_err.empty()) {
        std::string msg = j.at("error").at("message").as_string("");
        if (msg.empty()) {
            msg = j.at("error").as_string("");
        }
        if (!msg.empty()) {
            return utf16(msg);
        }
    }
    return fallback ? fallback : L"HTTP error";
}

}  // namespace

bool openai_api_list_models(std::vector<HttpModelRow>* out, std::wstring* error) {
    if (!out) {
        return false;
    }
    out->clear();
    std::wstring key;
    if (!openai_api_key_load(&key)) {
        if (error) {
            *error = L"OpenAI API key is not configured.";
        }
        return false;
    }
    HttpResult r;
    if (!http_json(L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, L"GET", L"/v1/models", bearer_header(key),
                   {}, 60000, &r)) {
        if (error) {
            *error = r.error.empty() ? L"OpenAI model list failed." : r.error;
        }
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        if (error) {
            *error = http_error_message(r, L"OpenAI model list rejected the key.");
        }
        return false;
    }
    std::string parse_err;
    Json j = Json::parse(r.body, &parse_err);
    if (!parse_err.empty() || !j.at("data").is_array()) {
        if (error) {
            *error = L"OpenAI model list returned invalid JSON.";
        }
        return false;
    }
    for (const auto& item : j.at("data").array_items()) {
        const std::string id = item.at("id").as_string("");
        if (!looks_like_openai_chat_model(id)) {
            continue;
        }
        out->push_back({id, display_from_id(id), std::to_string(item.at("created").as_int(0))});
    }
    std::sort(out->begin(), out->end(),
              [](const HttpModelRow& a, const HttpModelRow& b) {
                  if (a.created.size() != b.created.size()) return a.created.size() > b.created.size();
                  return a.created != b.created ? a.created > b.created : a.id < b.id;
              });
    return true;
}

bool openai_api_chat(const std::string& model_id, const std::vector<HttpChatMessage>& messages,
                     std::string* result_text, std::wstring* error, unsigned timeout_ms) {
    std::wstring key;
    if (!openai_api_key_load(&key)) {
        if (error) {
            *error = L"OpenAI API key is not configured.";
        }
        return false;
    }
    if (model_id.empty()) {
        if (error) {
            *error = L"No OpenAI API model selected.";
        }
        return false;
    }
    Json body = Json::object();
    body["model"] = Json::string(model_id);
    Json msgs = Json::array();
    for (const auto& m : messages) {
        Json row = Json::object();
        row["role"] = Json::string(m.user ? "user" : "assistant");
        row["content"] = Json::string(m.text);
        msgs.push(std::move(row));
    }
    body["messages"] = std::move(msgs);
    body["stream"] = Json::boolean(false);

    HttpResult r;
    if (!http_json(L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, L"POST", L"/v1/chat/completions",
                   bearer_header(key), body.dump(), timeout_ms, &r)) {
        if (error) {
            *error = r.error.empty() ? L"OpenAI chat request failed." : r.error;
        }
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        if (error) {
            *error = http_error_message(r, L"OpenAI chat request was rejected.");
        }
        return false;
    }
    std::string parse_err;
    Json j = Json::parse(r.body, &parse_err);
    if (!parse_err.empty()) {
        if (error) {
            *error = L"OpenAI chat returned invalid JSON.";
        }
        return false;
    }
    std::string text = j.at("choices").at(0).at("message").at("content").as_string("");
    if (text.empty()) {
        text = j.at("choices").at(0).at("text").as_string("");
    }
    if (result_text) {
        *result_text = std::move(text);
    }
    return true;
}

bool claude_api_list_models(std::vector<HttpModelRow>* out, std::wstring* error) {
    if (!out) {
        return false;
    }
    out->clear();
    std::wstring key;
    if (!claude_api_key_load(&key)) {
        if (error) {
            *error = L"Claude API key is not configured.";
        }
        return false;
    }
    std::wstring request_path = L"/v1/models?limit=1000";
    std::string previous_cursor;
    for (;;) {
    HttpResult r;
    if (!http_json(L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, L"GET", request_path.c_str(),
                   anthropic_headers(key), {}, 60000, &r)) {
        if (error) {
            *error = r.error.empty() ? L"Claude model list failed." : r.error;
        }
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        if (error) {
            *error = http_error_message(r, L"Claude model list rejected the key.");
        }
        return false;
    }
    std::string parse_err;
    Json j = Json::parse(r.body, &parse_err);
    if (!parse_err.empty() || !j.at("data").is_array()) {
        if (error) {
            *error = L"Claude model list returned invalid JSON.";
        }
        return false;
    }
    for (const auto& item : j.at("data").array_items()) {
        const std::string id = item.at("id").as_string("");
        if (id.empty()) {
            continue;
        }
        std::string display = item.at("display_name").as_string("");
        if (display.empty()) {
            display = id;
        }
        out->push_back({id, display, item.at("created_at").as_string("")});
    }
    if (!j.at("has_more").as_bool(false)) break;
    const std::string cursor = j.at("last_id").as_string("");
    if (cursor.empty() || cursor == previous_cursor ||
        cursor.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) {
        if (error) *error = L"Claude model list returned an invalid pagination cursor.";
        out->clear();
        return false;
    }
    previous_cursor = cursor;
    request_path = L"/v1/models?limit=1000&after_id=" + utf16(cursor);
    }
    std::sort(out->begin(), out->end(),
              [](const HttpModelRow& a, const HttpModelRow& b) { return a.created != b.created ? a.created > b.created : a.id < b.id; });
    return true;
}

bool claude_api_chat(const std::string& model_id, const std::vector<HttpChatMessage>& messages,
                     std::string* result_text, std::wstring* error, unsigned timeout_ms) {
    std::wstring key;
    if (!claude_api_key_load(&key)) {
        if (error) {
            *error = L"Claude API key is not configured.";
        }
        return false;
    }
    if (model_id.empty()) {
        if (error) {
            *error = L"No Claude API model selected.";
        }
        return false;
    }
    Json body = Json::object();
    body["model"] = Json::string(model_id);
    body["max_tokens"] = Json::number(8192);
    Json msgs = Json::array();
    for (const auto& m : messages) {
        Json row = Json::object();
        row["role"] = Json::string(m.user ? "user" : "assistant");
        row["content"] = Json::string(m.text);
        msgs.push(std::move(row));
    }
    body["messages"] = std::move(msgs);

    HttpResult r;
    if (!http_json(L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, L"POST", L"/v1/messages",
                   anthropic_headers(key), body.dump(), timeout_ms, &r)) {
        if (error) {
            *error = r.error.empty() ? L"Claude chat request failed." : r.error;
        }
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        if (error) {
            *error = http_error_message(r, L"Claude chat request was rejected.");
        }
        return false;
    }
    std::string parse_err;
    Json j = Json::parse(r.body, &parse_err);
    if (!parse_err.empty()) {
        if (error) {
            *error = L"Claude chat returned invalid JSON.";
        }
        return false;
    }
    std::string text;
    const Json& content = j.at("content");
    if (content.is_array()) {
        for (const auto& block : content.array_items()) {
            if (block.at("type").as_string("") == "text") {
                if (!text.empty()) {
                    text.push_back('\n');
                }
                text += block.at("text").as_string("");
            }
        }
    }
    if (result_text) {
        *result_text = std::move(text);
    }
    return true;
}

}  // namespace scyllagpt
