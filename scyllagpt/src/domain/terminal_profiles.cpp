#include "scyllagpt/terminal_profiles.h"

#include "scyllagpt/json.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

namespace scyllagpt {
namespace {

bool file_is_regular(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring join2(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == L'\\' || a.back() == L'/') {
        return a + b;
    }
    return a + L"\\" + b;
}

std::wstring system32() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        return L"C:\\Windows\\System32";
    }
    return buf;
}

std::wstring program_files() {
    wchar_t buf[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return buf;
    }
    return L"C:\\Program Files";
}

std::wstring user_profile() {
    wchar_t buf[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return buf;
    }
    return L"";
}

std::wstring search_path_exe(const wchar_t* name) {
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH, buf, nullptr)) {
        return buf;
    }
    if (SearchPathW(nullptr, name, L".exe", MAX_PATH, buf, nullptr)) {
        return buf;
    }
    return L"";
}

std::wstring basename_of(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos) {
        return std::wstring(path);
    }
    return std::wstring(path.substr(slash + 1));
}

std::wstring to_lower_copy(std::wstring s) {
    for (auto& c : s) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return s;
}

void push_if_exists(std::vector<TerminalProfile>* out, std::string id, std::wstring name, std::wstring exe,
                    std::wstring args = L"") {
    if (!file_is_regular(exe)) {
        return;
    }
    for (const auto& existing : *out) {
        if (to_lower_copy(existing.executable) == to_lower_copy(exe) && existing.args == args) {
            return;
        }
    }
    TerminalProfile p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.executable = std::move(exe);
    p.args = std::move(args);
    p.source = TerminalProfileSource::Detected;
    p.enabled = true;
    p.working_directory_mode = WorkingDirectoryMode::Project;
    out->push_back(std::move(p));
}

std::string run_capture_stdout(const std::wstring& exe, const std::wstring& cmdline, DWORD timeout_ms) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        return {};
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring mutable_cmd = cmdline;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(exe.empty() ? nullptr : exe.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return {};
    }

    std::string out;
    char buf[4096];
    const DWORD deadline = GetTickCount() + timeout_ms;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) {
            break;
        }
        if (avail) {
            DWORD got = 0;
            const DWORD to_read = (std::min)(avail, static_cast<DWORD>(sizeof(buf)));
            if (!ReadFile(rd, buf, to_read, &got, nullptr) || got == 0) {
                break;
            }
            out.append(buf, buf + got);
            continue;
        }
        const DWORD wait = WaitForSingleObject(pi.hProcess, 20);
        if (wait == WAIT_OBJECT_0) {
            // Drain remaining.
            DWORD got = 0;
            while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
                out.append(buf, buf + got);
            }
            break;
        }
        if (GetTickCount() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exit_code) || exit_code != 0) out.clear();
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return out;
}

void discover_wsl(std::vector<TerminalProfile>* out) {
    std::wstring exe = search_path_exe(L"wsl.exe");
    if (exe.empty() || !file_is_regular(exe)) {
        exe = join2(system32(), L"wsl.exe");
    }
    if (!file_is_regular(exe)) {
        return;
    }
    std::wstring cmd = L"\"" + exe + L"\" -l -q";
    const std::string raw = run_capture_stdout(exe, cmd, 4000);
    const auto distros = parse_wsl_list_quiet(raw);
    if (distros.empty()) push_if_exists(out, "wsl-default", L"WSL (default distribution)", exe);
    for (const auto& d : distros) {
        if (d.empty()) {
            continue;
        }
        const std::string id = make_terminal_profile_id("wsl", d);
        // WSL distribution names normally contain no spaces; avoid redundant
        // quoting, which some WSL launch paths retain in the distribution name.
        const auto argument = d.find_first_of(L" \t") == std::wstring::npos ? d : L"\"" + d + L"\"";
        push_if_exists(out, id, L"WSL: " + d, exe, L"-d " + argument);
    }
}

}  // namespace

std::string normalize_agent_terminal_policy(std::string_view raw) {
    std::string s(raw);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s == "allow" || s == "block" || s == "ask") {
        return s;
    }
    return "ask";
}

std::string terminal_agent_policy(const std::map<std::string, std::string>& policies,
                                 const std::string& id, std::string_view fallback) {
    const auto found = policies.find(id);
    return normalize_agent_terminal_policy(found == policies.end() ? fallback : std::string_view(found->second));
}

bool is_valid_agent_terminal_policy(std::string_view raw) {
    std::string s(raw);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "ask" || s == "allow" || s == "block";
}

std::string terminal_profile_source_string(TerminalProfileSource s) {
    return s == TerminalProfileSource::Custom ? "custom" : "detected";
}

TerminalProfileSource parse_terminal_profile_source(std::string_view s) {
    if (s == "custom") {
        return TerminalProfileSource::Custom;
    }
    return TerminalProfileSource::Detected;
}

std::string working_directory_mode_string(WorkingDirectoryMode m) {
    switch (m) {
        case WorkingDirectoryMode::Home:
            return "home";
        case WorkingDirectoryMode::Custom:
            return "custom";
        case WorkingDirectoryMode::Project:
        default:
            return "project";
    }
}

WorkingDirectoryMode parse_working_directory_mode(std::string_view s) {
    if (s == "home") {
        return WorkingDirectoryMode::Home;
    }
    if (s == "custom") {
        return WorkingDirectoryMode::Custom;
    }
    return WorkingDirectoryMode::Project;
}

std::string make_terminal_profile_id(std::string_view prefix, std::wstring_view name) {
    std::string out;
    out.reserve(prefix.size() + 1 + name.size());
    out.append(prefix.begin(), prefix.end());
    out.push_back(':');
    for (wchar_t wc : name) {
        if (wc < 128) {
            char c = static_cast<char>(wc);
            if (std::isalnum(static_cast<unsigned char>(c))) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else if (c == ' ' || c == '_' || c == '-' || c == '.') {
                out.push_back('-');
            }
        }
    }
    // Collapse duplicate dashes.
    std::string compact;
    compact.reserve(out.size());
    char prev = 0;
    for (char c : out) {
        if (c == '-' && (prev == '-' || prev == ':')) {
            continue;
        }
        compact.push_back(c);
        prev = c;
    }
    while (!compact.empty() && compact.back() == '-') {
        compact.pop_back();
    }
    return compact;
}

std::vector<std::wstring> parse_wsl_list_quiet(std::string_view raw_bytes) {
    std::vector<std::wstring> names;
    if (raw_bytes.empty()) {
        return names;
    }

    std::wstring wide;
    const auto* data = reinterpret_cast<const unsigned char*>(raw_bytes.data());
    const std::size_t n = raw_bytes.size();

    const bool utf16le_bom = n >= 2 && data[0] == 0xFF && data[1] == 0xFE;
    const bool looks_utf16le = utf16le_bom || (n >= 4 && data[1] == 0 && data[3] == 0);

    if (looks_utf16le) {
        std::size_t i = utf16le_bom ? 2 : 0;
        while (i + 1 < n) {
            const wchar_t ch = static_cast<wchar_t>(data[i] | (data[i + 1] << 8));
            i += 2;
            wide.push_back(ch);
        }
    } else {
        wide = utf16(std::string(raw_bytes));
    }

    std::wstring line;
    auto flush = [&]() {
        // Trim whitespace / CR.
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        std::size_t start = 0;
        while (start < line.size() && (line[start] == L' ' || line[start] == L'\t')) {
            ++start;
        }
        if (start < line.size()) {
            names.push_back(line.substr(start));
        }
        line.clear();
    };

    for (wchar_t ch : wide) {
        if (ch == L'\0') {
            continue;
        }
        if (ch == L'\n') {
            flush();
        } else {
            line.push_back(ch);
        }
    }
    flush();
    return names;
}

std::wstring resolve_profile_cwd(const TerminalProfile& profile, const std::wstring& project_root,
                                 const std::wstring& home_directory) {
    switch (profile.working_directory_mode) {
        case WorkingDirectoryMode::Home:
            return home_directory.empty() ? project_root : home_directory;
        case WorkingDirectoryMode::Custom:
            if (!profile.custom_working_directory.empty()) {
                return profile.custom_working_directory;
            }
            return project_root.empty() ? home_directory : project_root;
        case WorkingDirectoryMode::Project:
        default:
            if (!project_root.empty()) {
                return project_root;
            }
            return home_directory;
    }
}

bool is_known_shell_executable(std::wstring_view path_or_name) {
    const std::wstring base = to_lower_copy(basename_of(path_or_name));
    return base == L"cmd.exe" || base == L"powershell.exe" || base == L"pwsh.exe" || base == L"bash.exe" ||
           base == L"wsl.exe" || base == L"sh.exe";
}

std::vector<TerminalProfile> discover_terminal_profiles() {
    std::vector<TerminalProfile> out;

    // PowerShell 7 (pwsh)
    {
        std::wstring pwsh = search_path_exe(L"pwsh.exe");
        if (pwsh.empty()) {
            const std::wstring candidate = join2(join2(program_files(), L"PowerShell\\7"), L"pwsh.exe");
            if (file_is_regular(candidate)) {
                pwsh = candidate;
            }
        }
        push_if_exists(&out, "pwsh", L"PowerShell 7", pwsh);
    }

    // Windows PowerShell 5.x
    {
        const std::wstring ps = join2(system32(), L"WindowsPowerShell\\v1.0\\powershell.exe");
        push_if_exists(&out, "powershell", L"Windows PowerShell", ps);
    }

    // cmd.exe — always present on supported hosts when System32 is intact
    {
        const std::wstring cmd = join2(system32(), L"cmd.exe");
        push_if_exists(&out, "cmd", L"Command Prompt", cmd);
    }

    discover_wsl(&out);

    // Git Bash (Git for Windows)
    {
        const std::wstring home = user_profile();
        const std::wstring pf = program_files();
        wchar_t pf86[MAX_PATH]{};
        GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH);
        const std::wstring candidates[] = {
            join2(join2(pf, L"Git\\bin"), L"bash.exe"),
            join2(join2(pf, L"Git\\usr\\bin"), L"bash.exe"),
            pf86[0] ? join2(join2(pf86, L"Git\\bin"), L"bash.exe") : L"",
            join2(home, L"AppData\\Local\\Programs\\Git\\bin\\bash.exe"),
        };
        for (const auto& bash : candidates) {
            if (!bash.empty() && file_is_regular(bash)) {
                push_if_exists(&out, "git-bash", L"Git Bash", bash, L"--login");
                break;
            }
        }
    }

    // Cygwin at common paths
    {
        const std::wstring home = user_profile();
        const std::wstring candidates[] = {
            L"C:\\cygwin64\\bin\\bash.exe",
            L"C:\\cygwin\\bin\\bash.exe",
            join2(home, L"cygwin64\\bin\\bash.exe"),
            join2(home, L"cygwin\\bin\\bash.exe"),
        };
        for (const auto& bash : candidates) {
            if (file_is_regular(bash)) {
                push_if_exists(&out, make_terminal_profile_id("cygwin", bash), L"Cygwin bash", bash, L"--login");
                break;
            }
        }
    }

    // Discover common CLI launchers on PATH as well as popular per-user installs.
    struct Cli { const char* id; const wchar_t* name; const wchar_t* binary; const wchar_t* install; };
    const Cli tools[] = {
        {"github-cli", L"GitHub CLI", L"gh", L"GitHub CLI\\gh.exe"},
        {"claude-cli", L"Claude CLI", L"claude", L""},
        {"codex-cli", L"Codex CLI", L"codex", L""},
        {"cursor-cli", L"Cursor CLI", L"cursor-agent", L""},
        {"nu", L"Nushell", L"nu", L"nu\\bin\\nu.exe"},
        {"bash-path", L"Bash (PATH)", L"bash", L""},
    };
    for (const auto& tool : tools) {
        std::wstring executable;
        for (const auto* extension : {L".exe", L".cmd", L".bat"}) {
            executable = search_path_exe((std::wstring(tool.binary) + extension).c_str());
            if (!executable.empty()) break;
        }
        if (executable.empty() && *tool.install) {
            const auto candidate = join2(program_files(), tool.install);
            if (file_is_regular(candidate)) executable = candidate;
        }
        if (executable.empty()) {
            for (const auto* folder : {L".local\\bin", L"AppData\\Roaming\\npm", L"scoop\\shims"}) {
                for (const auto* extension : {L".exe", L".cmd", L".bat"}) {
                    const auto candidate = join2(join2(user_profile(), folder), std::wstring(tool.binary) + extension);
                    if (file_is_regular(candidate)) { executable = candidate; break; }
                }
                if (!executable.empty()) break;
            }
        }
        if (executable.empty()) continue;
        if (std::any_of(out.begin(), out.end(), [&](const auto& p) { return to_lower_copy(p.executable) == to_lower_copy(executable); })) continue;
        const auto lower = to_lower_copy(executable);
        if (lower.ends_with(L".cmd") || lower.ends_with(L".bat"))
            push_if_exists(&out, tool.id, tool.name, join2(system32(), L"cmd.exe"), L"/d /s /k \"\"" + executable + L"\"\"");
        else push_if_exists(&out, tool.id, tool.name, executable);
    }
    return out;
}

void apply_terminal_profile_enablement(std::vector<TerminalProfile>* profiles,
                                       const std::map<std::string, bool>& enabled_by_id) {
    if (!profiles) {
        return;
    }
    for (auto& p : *profiles) {
        const auto it = enabled_by_id.find(p.id);
        if (it != enabled_by_id.end()) {
            p.enabled = it->second;
        }
    }
}

std::vector<TerminalProfile> enabled_terminal_profiles(const std::vector<TerminalProfile>& profiles) {
    std::vector<TerminalProfile> out;
    for (const auto& p : profiles) {
        if (p.enabled) {
            out.push_back(p);
        }
    }
    return out;
}

const TerminalProfile* find_default_terminal_profile(const std::vector<TerminalProfile>& profiles,
                                                    std::string_view preferred_id) {
    if (!preferred_id.empty()) {
        for (const auto& p : profiles) {
            if (p.enabled && p.id == preferred_id) {
                return &p;
            }
        }
    }
    for (const auto& p : profiles) {
        if (p.enabled) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<TerminalProfile> load_custom_terminal_profiles(const std::wstring& path) {
    std::vector<TerminalProfile> out;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(h);
        return out;
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return out;
    }
    const Json& arr = j.at("custom_profiles");
    if (!arr.is_array()) {
        return out;
    }
    for (const auto& item : arr.array_items()) {
        if (!item.is_object()) {
            continue;
        }
        TerminalProfile p;
        p.id = item.at("id").as_string("");
        p.name = utf16(item.at("name").as_string(""));
        p.executable = utf16(item.at("executable").as_string(""));
        p.args = utf16(item.at("args").as_string(""));
        p.source = TerminalProfileSource::Custom;
        p.enabled = item.at("enabled").is_null() ? true : item.at("enabled").as_bool(true);
        p.working_directory_mode = parse_working_directory_mode(item.at("cwd_mode").as_string("project"));
        p.custom_working_directory = utf16(item.at("custom_cwd").as_string(""));
        if (p.id.empty() || p.executable.empty()) {
            continue;
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool save_custom_terminal_profiles(const std::wstring& path, const std::vector<TerminalProfile>& custom) {
    Json root = Json::object();
    Json arr = Json::array();
    for (const auto& p : custom) {
        if (p.source != TerminalProfileSource::Custom) {
            continue;
        }
        Json o = Json::object();
        o["id"] = Json::string(p.id);
        o["name"] = Json::string(utf8(p.name));
        o["executable"] = Json::string(utf8(p.executable));
        o["args"] = Json::string(utf8(p.args));
        o["enabled"] = Json::boolean(p.enabled);
        o["cwd_mode"] = Json::string(working_directory_mode_string(p.working_directory_mode));
        o["custom_cwd"] = Json::string(utf8(p.custom_working_directory));
        arr.push(std::move(o));
    }
    root["custom_profiles"] = std::move(arr);
    const std::string body = root.dump();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

std::vector<TerminalProfile> merge_terminal_profiles(const std::vector<TerminalProfile>& discovered,
                                                    const std::vector<TerminalProfile>& custom,
                                                    const std::map<std::string, bool>& enabled_by_id) {
    std::vector<TerminalProfile> out = discovered;
    for (const auto& c : custom) {
        bool replaced = false;
        for (auto& d : out) {
            if (d.id == c.id) {
                d = c;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out.push_back(c);
        }
    }
    apply_terminal_profile_enablement(&out, enabled_by_id);
    return out;
}

}  // namespace scyllagpt
