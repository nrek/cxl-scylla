#include "scyllagpt/paths.h"

#include "scyllagpt/lockdown.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace scyllagpt {
namespace {

std::wstring local_appdata() {
    wchar_t buf[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return L"";
    }
    return buf;
}

std::wstring product_version(const std::wstring& exe) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
    if (!size) {
        return L"";
    }
    std::vector<char> block(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, block.data())) {
        return L"";
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) {
        struct LANGANDCODEPAGE {
            WORD wLanguage;
            WORD wCodePage;
        };
        LANGANDCODEPAGE* trans = nullptr;
        UINT tlen = 0;
        if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&trans), &tlen) && trans) {
            wchar_t sub[64]{};
            swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\ProductVersion", trans[0].wLanguage, trans[0].wCodePage);
            wchar_t* value = nullptr;
            UINT vlen = 0;
            if (VerQueryValueW(block.data(), sub, reinterpret_cast<void**>(&value), &vlen) && value) {
                return value;
            }
        }
        return L"";
    }
    wchar_t* value = nullptr;
    UINT vlen = 0;
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* trans = nullptr;
    UINT tlen = 0;
    if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&trans), &tlen) && trans) {
        wchar_t sub[64]{};
        swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\ProductVersion", trans[0].wLanguage, trans[0].wCodePage);
        if (VerQueryValueW(block.data(), sub, reinterpret_cast<void**>(&value), &vlen) && value) {
            return value;
        }
    }
    return L"";
}

}  // namespace

std::wstring discover_codex_exe() {
    wchar_t local[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local);
    const std::wstring base = join_path(local, L"OpenAI\\Codex\\bin");

    // ChatGPT desktop 0.153.1 lives in a hash folder and has no VERSIONINFO.
    // Do not rank by ProductVersion: the unversioned sibling reports 0.130.0-alpha.5
    // and would win. Prefer the newest hashed copy; fall back to the sibling, then PATH.
    struct Hashed {
        FILETIME written{};
        std::wstring path;
    };
    std::vector<Hashed> hashed;
    WIN32_FIND_DATAW fd{};
    HANDLE dh = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (dh != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
                const std::wstring p = join_path(join_path(base, fd.cFileName), L"codex.exe");
                if (file_exists(p)) {
                    hashed.push_back({fd.ftLastWriteTime, p});
                }
            }
        } while (FindNextFileW(dh, &fd));
        FindClose(dh);
    }
    if (!hashed.empty()) {
        std::sort(hashed.begin(), hashed.end(), [](const Hashed& a, const Hashed& b) {
            return CompareFileTime(&a.written, &b.written) < 0;
        });
        return hashed.back().path;
    }

    const std::wstring sibling = join_path(base, L"codex.exe");
    if (file_exists(sibling)) {
        return sibling;
    }
    wchar_t on_path[MAX_PATH]{};
    if (SearchPathW(nullptr, L"codex.exe", nullptr, MAX_PATH, on_path, nullptr)) {
        return on_path;
    }
    return L"";
}

bool is_unversioned_openai_codex(const std::wstring& path) {
    const std::wstring needle = L"\\OpenAI\\Codex\\bin\\codex.exe";
    if (path.size() < needle.size()) {
        return false;
    }
    const std::wstring tail = path.substr(path.size() - needle.size());
    for (size_t i = 0; i < needle.size(); ++i) {
        wchar_t a = tail[i];
        wchar_t b = needle[i];
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

std::wstring join_path(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == L'\\' || a.back() == L'/') {
        return a + b;
    }
    return a + L"\\" + b;
}

bool file_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ensure_dir(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    wchar_t full[MAX_PATH * 4]{};
    if (!GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr)) {
        return false;
    }
    std::wstring accum;
    for (wchar_t* p = full; *p; ++p) {
        accum.push_back(*p);
        if (*p == L'\\' || p[1] == 0) {
            if (accum.size() >= 3) {
                CreateDirectoryW(accum.c_str(), nullptr);
            }
        }
    }
    const DWORD a = GetFileAttributesW(full);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring file_version(const std::wstring& exe) {
    return product_version(exe);
}

Paths make_paths() {
    Paths p;
    p.appdata = join_path(local_appdata(), L"ScyllaGPT");
    p.codex_home = join_path(p.appdata, L"codex-home");
    p.workspace = join_path(p.appdata, L"workspace");
    p.settings_path = join_path(p.appdata, L"settings.json");
    p.store_path = join_path(p.appdata, L"workspace.json");
    p.knowledge_path = join_path(p.appdata, L"knowledge.json");
    p.strata_path = join_path(p.appdata, L"strata.json");
    p.mcp_path = join_path(p.appdata, L"mcp_connections.json");
    p.terminals_path = join_path(p.appdata, L"terminals.json");
    p.environments_path = join_path(p.appdata, L"environments.json");
    p.connections_path = join_path(p.appdata, L"connections.json");
    p.recovery_dir = join_path(p.appdata, L"recovery");
    p.attachments_dir = join_path(p.appdata, L"attachments");
    p.stderr_log = join_path(p.appdata, L"runtime-stderr.log");
    p.runtime_pin = join_path(p.appdata, L"runtime-pin.txt");
    ensure_dir(p.appdata);
    ensure_dir(p.codex_home);
    ensure_dir(p.workspace);
    ensure_dir(p.recovery_dir);
    ensure_dir(p.attachments_dir);
    return p;
}

namespace {

bool write_utf8_file(const std::wstring& path, const char* body, size_t n) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD w = 0;
    const BOOL ok = WriteFile(h, body, static_cast<DWORD>(n), &w, nullptr);
    CloseHandle(h);
    return ok && w == n;
}

bool write_utf8_file(const std::wstring& path, const char* body) {
    return write_utf8_file(path, body, strlen(body));
}

bool write_utf8_file(const std::wstring& path, const std::string& body) {
    return write_utf8_file(path, body.c_str(), body.size());
}

// TOML basic-string path: forward slashes, escaped quotes/backslashes.
std::string toml_quote_path(const std::wstring& path) {
    std::string u = utf8(path);
    std::string out;
    out.reserve(u.size() + 8);
    out.push_back('"');
    for (unsigned char c : u) {
        if (c == '\\' || c == '/') {
            out.push_back('/');
        } else if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
    return out;
}

std::string build_isolated_codex_config(bool workspace_write_grant,
                                        const std::vector<std::wstring>& extra_writable_roots,
                                        const std::vector<CodexMcpServer>& mcp_servers) {
    std::string body = isolated_codex_config_toml(workspace_write_grant);
    if (workspace_write_grant && !extra_writable_roots.empty()) {
        body += "\n[sandbox_workspace_write]\n";
        body += "network_access = false\n";
        body += "writable_roots = [";
        for (size_t i = 0; i < extra_writable_roots.size(); ++i) {
            if (i != 0) body += ", ";
            body += toml_quote_path(extra_writable_roots[i]);
        }
        body += "]\n";
    }
    for (const auto& server : mcp_servers) {
        if (server.name.empty()) continue;
        body += "\n[mcp_servers." + server.name + "]\n";
        if (server.stdio) {
            if (server.command.empty()) continue;
            body += "command = " + toml_quote_path(server.command) + "\n";
            body += "args = [";
            for (size_t i = 0; i < server.arguments.size(); ++i) {
                if (i) body += ", ";
                body += toml_quote_path(server.arguments[i]);
            }
            body += "]\n";
            if (!server.environment.empty()) {
                body += "env = { ";
                for (size_t i = 0; i < server.environment.size(); ++i) {
                    if (i) body += ", ";
                    body += utf8(server.environment[i].first) + " = " + toml_quote_path(server.environment[i].second);
                }
                body += " }\n";
            }
        } else {
            if (server.url.empty() || server.bearer_token_env_var.empty()) continue;
            body += "url = " + toml_quote_path(server.url) + "\n";
            body += "bearer_token_env_var = " + toml_quote_path(server.bearer_token_env_var) + "\n";
        }
        body += "startup_timeout_sec = 20\n";
        body += "tool_timeout_sec = 120\n";
    }
    return body;
}

}  // namespace

bool write_isolated_codex_config(const Paths& paths, bool workspace_write_grant,
                                 const std::vector<std::wstring>& extra_writable_roots,
                                 const std::vector<CodexMcpServer>& mcp_servers) {
    if (!ensure_dir(paths.codex_home) || !ensure_dir(paths.workspace)) {
        return false;
    }
    // Overwrite every launch / grant change so a prior Codex session cannot leave
    // connectors, notify helpers, or project-doc walk enabled. Never touch auth.json.
    const std::wstring cfg = join_path(paths.codex_home, L"config.toml");
    const std::string body = build_isolated_codex_config(workspace_write_grant, extra_writable_roots, mcp_servers);
    if (!write_utf8_file(cfg, body)) {
        return false;
    }
    const std::wstring agents = join_path(paths.workspace, L"AGENTS.md");
    return write_utf8_file(agents, workspace_agents_md());
}

std::string render_isolated_codex_config(bool workspace_write_grant,
                                         const std::vector<std::wstring>& extra_writable_roots,
                                         const std::vector<CodexMcpServer>& mcp_servers) {
    return build_isolated_codex_config(workspace_write_grant, extra_writable_roots, mcp_servers);
}

}  // namespace scyllagpt
