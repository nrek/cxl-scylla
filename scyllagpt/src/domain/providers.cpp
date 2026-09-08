#include "scyllagpt/provider.h"

#include "scyllagpt/json.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace scyllagpt {
namespace {

constexpr wchar_t kClaudeCredTarget[] = L"ScyllaGPT/ClaudeApiKey";

ClaudeCodeSession g_claude_session_cache{};
DWORD g_claude_session_cache_tick = 0;
constexpr DWORD kClaudeSessionCacheMs = 2500;
bool g_claude_sticky_connected = false;
std::vector<ClaudeModelRow> g_claude_models_cache;
DWORD g_claude_models_tick = 0;
constexpr DWORD kClaudeModelsCacheMs = 60 * 60 * 1000;  // 1h

bool ends_with_ci(const std::wstring& s, const wchar_t* suf) {
    const std::size_t n = wcslen(suf);
    if (s.size() < n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        wchar_t a = s[s.size() - n + i];
        wchar_t b = suf[i];
        if (a >= L'A' && a <= L'Z') {
            a = static_cast<wchar_t>(a - L'A' + L'a');
        }
        if (b >= L'A' && b <= L'Z') {
            b = static_cast<wchar_t>(b - L'A' + L'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool run_claude_cli(const std::wstring& args, std::string* stdout_utf8, std::wstring* error, DWORD timeout_ms) {
    const std::wstring cli = discover_claude_cli();
    if (cli.empty()) {
        if (error) {
            *error = L"Claude Code CLI was not found on PATH.";
        }
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        if (error) {
            *error = L"Could not create pipe for Claude CLI.";
        }
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd;
    if (ends_with_ci(cli, L".cmd") || ends_with_ci(cli, L".bat")) {
        cmd = L"cmd.exe /d /c \"" + cli + L"\" " + args;
    } else {
        cmd = L"\"" + cli + L"\" " + args;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    CloseHandle(wr);
    wr = nullptr;
    if (!ok) {
        CloseHandle(rd);
        if (error) {
            *error = L"Could not launch Claude CLI.";
        }
        return false;
    }

    std::string out;
    char buf[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(rd, buf, sizeof(buf), &n, nullptr) || n == 0) {
            break;
        }
        out.append(buf, buf + n);
        if (out.size() > 256 * 1024) {
            break;
        }
    }
    CloseHandle(rd);

    const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    } else {
        TerminateProcess(pi.hProcess, 1);
        if (error) {
            *error = L"Claude CLI timed out.";
        }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (stdout_utf8) {
        *stdout_utf8 = std::move(out);
    }
    if (exit_code != 0 && error && error->empty()) {
        *error = L"Claude CLI exited with an error.";
    }
    return exit_code == 0;
}

}  // namespace

bool claude_api_key_save(const std::wstring& key) {
    if (key.empty()) {
        return false;
    }
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(kClaudeCredTarget);
    cred.CredentialBlobSize = static_cast<DWORD>((key.size() + 1) * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(key.c_str()));
    cred.Persist = CRED_PERSIST_ENTERPRISE;
    cred.UserName = const_cast<wchar_t*>(L"anthropic");
    return CredWriteW(&cred, 0) != 0;
}

bool claude_api_key_load(std::wstring* out) {
    if (!out) {
        return false;
    }
    out->clear();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(kClaudeCredTarget, CRED_TYPE_GENERIC, 0, &cred) || !cred) {
        return false;
    }
    if (cred->CredentialBlobSize >= sizeof(wchar_t) && cred->CredentialBlob) {
        const std::size_t n = cred->CredentialBlobSize / sizeof(wchar_t);
        out->assign(reinterpret_cast<wchar_t*>(cred->CredentialBlob), n);
        while (!out->empty() && out->back() == L'\0') {
            out->pop_back();
        }
    }
    CredFree(cred);
    return !out->empty();
}

bool claude_api_key_clear() {
    return CredDeleteW(kClaudeCredTarget, CRED_TYPE_GENERIC, 0) != 0 || GetLastError() == ERROR_NOT_FOUND;
}

bool claude_api_key_present() {
    std::wstring k;
    return claude_api_key_load(&k);
}

std::wstring discover_claude_cli() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, L"claude.exe", nullptr, MAX_PATH, buf, nullptr);
    if (n > 0 && n < MAX_PATH) {
        return buf;
    }
    const DWORD n_cmd = SearchPathW(nullptr, L"claude.cmd", nullptr, MAX_PATH, buf, nullptr);
    if (n_cmd > 0 && n_cmd < MAX_PATH) {
        return buf;
    }
    const DWORD n2 = SearchPathW(nullptr, L"claude", L".exe", MAX_PATH, buf, nullptr);
    if (n2 > 0 && n2 < MAX_PATH) {
        return buf;
    }
    return {};
}

std::wstring discover_cursor_agent_cli() {
    auto env = [](const wchar_t* name) {
        const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
        std::wstring value(n, L'\0');
        if (n) value.resize(GetEnvironmentVariableW(name, value.data(), n));
        return value;
    };
    std::vector<std::wstring> dirs;
    const auto home = env(L"USERPROFILE");
    if (!home.empty()) dirs.push_back(home + L"\\.local\\bin");
    const auto path = env(L"PATH");
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find(L';', begin);
        auto dir = path.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"')
            dir = dir.substr(1, dir.size() - 2);
        // No relative or UNC paths: provider discovery must not probe project scripts/network shares.
        if (dir.size() > 2 && dir[1] == L':' && (dir[2] == L'\\' || dir[2] == L'/')) dirs.push_back(dir);
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    for (const auto& dir : dirs) {
        for (const auto* name : {L"agent.exe", L"agent.cmd", L"cursor-agent.exe", L"cursor-agent.cmd"}) {
            const auto candidate = dir + L"\\" + name;
            const auto attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return candidate;
        }
    }
    return {};
}

bool claude_code_login_launch(std::wstring* error) {
    const std::wstring cli = discover_claude_cli();
    if (cli.empty()) {
        if (error) {
            *error = L"Claude Code CLI (claude.exe) was not found on PATH.";
        }
        return false;
    }
    std::wstring cmd;
    if (ends_with_ci(cli, L".cmd") || ends_with_ci(cli, L".bat")) {
        cmd = L"cmd.exe /d /c \"" + cli + L"\" auth login";
    } else {
        cmd = L"\"" + cli + L"\" auth login";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si,
                        &pi)) {
        if (error) {
            *error = L"Could not launch Claude Code login.";
        }
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    g_claude_session_cache_tick = 0;  // invalidate cache
    return true;
}

ClaudeCodeSession claude_code_session_status(bool force) {
    const DWORD now = GetTickCount();
    if (!force && g_claude_session_cache_tick != 0 && (now - g_claude_session_cache_tick) < kClaudeSessionCacheMs) {
        return g_claude_session_cache;
    }

    const ClaudeCodeSession prev = g_claude_session_cache;
    const bool had_good = prev.logged_in;

    ClaudeCodeSession s;
    if (discover_claude_cli().empty()) {
        // No CLI — keep prior OAuth only if we still have an API key path elsewhere.
        g_claude_session_cache = s;
        g_claude_session_cache_tick = now;
        return s;
    }

    std::string out;
    std::wstring err;
    if (!run_claude_cli(L"auth status", &out, &err, 20000)) {
        // Transient CLI failure must not wipe a known-good OAuth session (that was dropping
        // Claude rows from the agent menu right after Settings showed connected).
        if (had_good) {
            g_claude_session_cache_tick = now;
            return prev;
        }
        g_claude_session_cache = s;
        g_claude_session_cache_tick = now;
        return s;
    }

    // Status may include a banner before JSON — take the first '{' object.
    const auto brace = out.find('{');
    if (brace == std::string::npos) {
        if (had_good) {
            g_claude_session_cache_tick = now;
            return prev;
        }
        g_claude_session_cache = s;
        g_claude_session_cache_tick = now;
        return s;
    }
    std::string parse_err;
    Json j = Json::parse(out.substr(brace), &parse_err);
    if (!parse_err.empty()) {
        if (had_good) {
            g_claude_session_cache_tick = now;
            return prev;
        }
        g_claude_session_cache = s;
        g_claude_session_cache_tick = now;
        return s;
    }
    s.logged_in = j.at("loggedIn").as_bool(false);
    s.email = j.at("email").as_string("");
    s.org_name = j.at("orgName").as_string("");
    s.auth_method = j.at("authMethod").as_string("");
    s.subscription = j.at("subscriptionType").as_string("");
    if (s.logged_in) {
        g_claude_sticky_connected = true;
    } else {
        g_claude_sticky_connected = false;
    }
    g_claude_session_cache = s;
    g_claude_session_cache_tick = now;
    return s;
}

bool claude_code_logout(std::wstring* error) {
    g_claude_session_cache_tick = 0;
    g_claude_sticky_connected = false;
    g_claude_session_cache = {};
    std::string out;
    return run_claude_cli(L"auth logout", &out, error, 20000);
}

ProviderStatus openai_provider_status(bool signed_in, const std::string& email, const std::string& plan,
                                      const std::string& type) {
    ProviderStatus s;
    s.id = ProviderId::OpenAI;
    s.display_name = "OpenAI";
    s.caps.streaming = true;
    s.caps.file_context = true;
    s.caps.chat_send = true;
    if (signed_in) {
        s.connected = true;
        s.auth_label = type.empty() ? "ChatGPT" : type;
        s.detail = email;
        if (!plan.empty()) {
            s.detail += s.detail.empty() ? plan : (" (" + plan + ")");
        }
    } else if (type == "apiKey") {
        s.connected = false;
        s.auth_label = "API key present — Sign in with ChatGPT required";
    } else {
        s.connected = false;
        s.auth_label = "Not connected";
    }
    return s;
}

ProviderStatus claude_provider_status() {
    ProviderStatus s;
    s.id = ProviderId::Claude;
    s.display_name = "Claude";
    s.caps.streaming = false;
    s.caps.file_context = false;
    s.caps.chat_send = false;
    std::wstring key;
    if (claude_api_key_load(&key)) {
        s.connected = true;
        s.caps.chat_send = true;
        s.auth_label = "API key";
        if (key.size() > 12) {
            s.detail = "sk-ant-…" + utf8(key.substr(key.size() - 4));
        } else {
            s.detail = "key saved";
        }
        return s;
    }

    const ClaudeCodeSession sess = claude_code_session_status(false);
    if (sess.logged_in) {
        s.connected = true;
        s.caps.chat_send = true;
        s.caps.streaming = false;
        s.caps.file_context = true;
        s.auth_label = sess.auth_method.empty() ? "Claude Code" : ("Claude Code · " + sess.auth_method);
        s.detail = sess.email;
        if (!sess.org_name.empty()) {
            if (!s.detail.empty()) {
                s.detail += " · ";
            }
            s.detail += sess.org_name;
        }
        if (!sess.subscription.empty()) {
            if (!s.detail.empty()) {
                s.detail += " · ";
            }
            s.detail += sess.subscription;
        }
        return s;
    }

    if (!discover_claude_cli().empty()) {
        s.connected = false;
        s.auth_label = "Claude Code CLI found — sign in to connect";
    } else {
        s.connected = false;
        s.auth_label = "Not connected";
    }
    return s;
}

bool claude_is_connected() {
    if (claude_api_key_present()) {
        g_claude_sticky_connected = true;
        return true;
    }
    if (claude_code_session_status(false).logged_in) {
        g_claude_sticky_connected = true;
        return true;
    }
    // Sticky: keep menu rows after a good OAuth probe even if a later CLI call flakes.
    return g_claude_sticky_connected;
}

void claude_clear_connection_state() {
    g_claude_sticky_connected = false;
    g_claude_session_cache = {};
    g_claude_session_cache_tick = 0;
    g_claude_models_cache.clear();
    g_claude_models_tick = 0;
}

namespace {

std::vector<ClaudeModelRow> curated_as_rows() {
    std::vector<ClaudeModelRow> rows;
    std::size_t n = 0;
    const ClaudeModelInfo* cat = curated_claude_models(&n);
    for (std::size_t i = 0; i < n && i < 3; ++i) {
        rows.push_back({cat[i].id, cat[i].display});
    }
    return rows;
}

}  // namespace

const ClaudeModelInfo* curated_claude_models(std::size_t* count) {
    static const ClaudeModelInfo kModels[] = {
        {"claude-opus-4-6", "Opus 4.6"},
        {"claude-sonnet-4-6", "Sonnet 4.6"},
        {"claude-haiku-4-5-20251001", "Haiku 4.5"},
    };
    if (count) {
        *count = sizeof(kModels) / sizeof(kModels[0]);
    }
    return kModels;
}

std::vector<ClaudeModelRow> claude_code_list_models(bool force) {
    (void)force;
    const DWORD now = GetTickCount();
    if (!g_claude_models_cache.empty() && g_claude_models_tick != 0 &&
        (now - g_claude_models_tick) < kClaudeModelsCacheMs) {
        return g_claude_models_cache;
    }
    // Claude Code CLI has no `models` subcommand (auth/mcp/plugin/…). Use the curated
    // latest aliases that match `claude --model` (opus/sonnet/haiku family).
    auto rows = curated_as_rows();
    g_claude_models_cache = rows;
    g_claude_models_tick = now;
    return rows;
}

bool claude_code_print(const std::string& prompt_utf8, const std::string& model_id, const std::wstring& cwd,
                       std::string* result_text, std::wstring* error, unsigned timeout_ms) {
    const std::wstring cli = discover_claude_cli();
    if (cli.empty()) {
        if (error) {
            *error = L"Claude Code CLI was not found on PATH.";
        }
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out_rd = nullptr;
    HANDLE out_wr = nullptr;
    HANDLE in_rd = nullptr;
    HANDLE in_wr = nullptr;
    if (!CreatePipe(&out_rd, &out_wr, &sa, 0) || !CreatePipe(&in_rd, &in_wr, &sa, 0)) {
        if (error) {
            *error = L"Could not create pipes for Claude print.";
        }
        return false;
    }
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);

    std::wstring model = utf16(model_id.empty() ? "claude-sonnet-4-6" : model_id);
    std::wstring args = L"-p --output-format json --no-session-persistence --model \"" + model + L"\"";
    std::wstring cmd;
    if (ends_with_ci(cli, L".cmd") || ends_with_ci(cli, L".bat")) {
        cmd = L"cmd.exe /d /c \"" + cli + L"\" " + args;
    } else {
        cmd = L"\"" + cli + L"\" " + args;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_wr;
    si.hStdError = out_wr;
    si.hStdInput = in_rd;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    const wchar_t* cwd_ptr = cwd.empty() ? nullptr : cwd.c_str();
    const BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   cwd_ptr, &si, &pi);
    CloseHandle(out_wr);
    CloseHandle(in_rd);
    out_wr = nullptr;
    in_rd = nullptr;
    if (!ok) {
        CloseHandle(out_rd);
        CloseHandle(in_wr);
        if (error) {
            *error = L"Could not launch Claude Code print.";
        }
        return false;
    }

    // Feed prompt on stdin, then close so Claude sees EOF.
    DWORD written = 0;
    if (!prompt_utf8.empty()) {
        WriteFile(in_wr, prompt_utf8.data(), static_cast<DWORD>(prompt_utf8.size()), &written, nullptr);
    }
    CloseHandle(in_wr);
    in_wr = nullptr;

    std::string out;
    char buf[4096];
    const DWORD deadline = GetTickCount() + timeout_ms;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_rd, nullptr, 0, nullptr, &avail, nullptr)) {
            break;
        }
        if (avail > 0) {
            DWORD n = 0;
            const DWORD want = avail > sizeof(buf) ? static_cast<DWORD>(sizeof(buf)) : avail;
            if (!ReadFile(out_rd, buf, want, &n, nullptr) || n == 0) {
                break;
            }
            out.append(buf, buf + n);
            if (out.size() > 8 * 1024 * 1024) {
                break;
            }
            continue;
        }
        const DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            // Drain remaining.
            DWORD n = 0;
            while (ReadFile(out_rd, buf, sizeof(buf), &n, nullptr) && n > 0) {
                out.append(buf, buf + n);
                n = 0;
            }
            break;
        }
        if (GetTickCount() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(out_rd);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (error) {
                *error = L"Claude print timed out.";
            }
            return false;
        }
    }
    CloseHandle(out_rd);

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // Prefer JSON result.result; fall back to raw stdout text.
    std::string text;
    const auto brace = out.find('{');
    if (brace != std::string::npos) {
        std::string parse_err;
        Json j = Json::parse(out.substr(brace), &parse_err);
        if (parse_err.empty()) {
            text = j.at("result").as_string("");
            if (text.empty()) {
                text = j.at("content").as_string("");
            }
            if (j.at("is_error").as_bool(false) || j.at("subtype").as_string("") == "error") {
                if (error) {
                    *error = utf16(text.empty() ? "Claude print returned an error." : text);
                }
                return false;
            }
        }
    }
    if (text.empty()) {
        text = out;
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
    }
    if (exit_code != 0 && text.empty()) {
        if (error) {
            *error = L"Claude print exited with an error.";
        }
        return false;
    }
    if (result_text) {
        *result_text = std::move(text);
    }
    return true;
}

bool provider_can_send(ProviderId id) {
    if (id == ProviderId::OpenAI) {
        return true;
    }
    if (id == ProviderId::Claude) {
        return claude_is_connected();
    }
    return false;
}

}  // namespace scyllagpt
