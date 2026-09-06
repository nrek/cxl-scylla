#include "identity.h"

#include "acl.h"
#include "path.h"
#include "process_scan.h"

#include <lm.h>
#include <sddl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <userenv.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "netapi32.lib")

namespace scylla {
namespace {

std::wstring arg_after(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 0; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return L"";
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 0; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

std::wstring json_escape(const std::wstring& w) {
    std::wstring o;
    for (wchar_t c : w) {
        if (c == L'\\' || c == L'"') {
            o.push_back(L'\\');
        }
        o.push_back(c);
    }
    return o;
}

bool lookup_user_sid(const std::wstring& name, PSID* out) {
    *out = nullptr;
    DWORD sid_sz = 0;
    DWORD dom_sz = 0;
    SID_NAME_USE use = SidTypeUnknown;
    LookupAccountNameW(nullptr, name.c_str(), nullptr, &sid_sz, nullptr, &dom_sz, &use);
    if (sid_sz == 0) {
        return false;
    }
    std::vector<BYTE> sid_buf(sid_sz);
    std::vector<wchar_t> dom(dom_sz == 0 ? 1 : dom_sz);
    if (!LookupAccountNameW(nullptr, name.c_str(), sid_buf.data(), &sid_sz, dom.data(), &dom_sz, &use)) {
        return false;
    }
    PSID copy = LocalAlloc(LPTR, sid_sz);
    if (!copy) {
        return false;
    }
    if (!CopySid(sid_sz, copy, sid_buf.data())) {
        LocalFree(copy);
        return false;
    }
    *out = copy;
    return true;
}

bool user_in_local_group(const std::wstring& user, const wchar_t* group) {
    LOCALGROUP_MEMBERS_INFO_2* members = nullptr;
    DWORD n = 0;
    DWORD total = 0;
    const NET_API_STATUS st =
        NetLocalGroupGetMembers(nullptr, group, 2, reinterpret_cast<LPBYTE*>(&members), MAX_PREFERRED_LENGTH, &n, &total, nullptr);
    if (st != NERR_Success || members == nullptr) {
        return false;
    }
    bool found = false;
    const std::wstring want = path_lower(user);
    for (DWORD i = 0; i < n; ++i) {
        if (members[i].lgrmi2_domainandname == nullptr) {
            continue;
        }
        std::wstring full = members[i].lgrmi2_domainandname;
        const auto slash = full.find_last_of(L'\\');
        const std::wstring leaf = slash == std::wstring::npos ? full : full.substr(slash + 1);
        if (path_lower(leaf) == want) {
            found = true;
            break;
        }
    }
    NetApiBufferFree(members);
    return found;
}

std::wstring chatgpt_aumid() { return L"OpenAI.Codex_2p2nqsd0c76g0!App"; }

std::wstring chatgpt_exe() {
    const std::wstring known =
        L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_26.901.4073.0_x64__2p2nqsd0c76g0\\app\\ChatGPT.exe";
    if (GetFileAttributesW(known.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return known;
    }
    const std::wstring root = L"C:\\Program Files\\WindowsApps";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"\\OpenAI.Codex_*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return L"";
    }
    std::wstring full;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            const std::wstring cand = root + L"\\" + fd.cFileName + L"\\app\\ChatGPT.exe";
            if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                full = cand;
                break;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return full;
}

std::wstring read_console_password() {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool got = GetConsoleMode(in, &mode) != 0;
    if (got) {
        SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
    }
    std::wcout << L"Password for isolated user (not echoed): " << std::flush;
    std::wstring pw;
    std::getline(std::wcin, pw);
    if (got) {
        SetConsoleMode(in, mode);
    }
    std::wcout << L"\n";
    return pw;
}

bool process_token_user_sid(HANDLE proc, std::wstring& sid_text) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
        return false;
    }
    DWORD need = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
    if (need == 0) {
        CloseHandle(tok);
        return false;
    }
    std::vector<BYTE> buf(need);
    DWORD got = 0;
    if (!GetTokenInformation(tok, TokenUser, buf.data(), need, &got)) {
        CloseHandle(tok);
        return false;
    }
    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
    LPWSTR s = nullptr;
    if (!tu || !tu->User.Sid || !ConvertSidToStringSidW(tu->User.Sid, &s)) {
        CloseHandle(tok);
        return false;
    }
    sid_text = s;
    LocalFree(s);
    CloseHandle(tok);
    return true;
}

bool process_token_has_users(HANDLE proc) {
    BYTE users_buf[SECURITY_MAX_SID_SIZE]{};
    DWORD users_sz = SECURITY_MAX_SID_SIZE;
    if (!CreateWellKnownSid(WinBuiltinUsersSid, nullptr, users_buf, &users_sz)) {
        return false;
    }
    HANDLE tok = nullptr;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
        return false;
    }
    DWORD need = 0;
    GetTokenInformation(tok, TokenGroups, nullptr, 0, &need);
    if (need == 0) {
        CloseHandle(tok);
        return false;
    }
    std::vector<BYTE> buf(need);
    DWORD got = 0;
    if (!GetTokenInformation(tok, TokenGroups, buf.data(), need, &got)) {
        CloseHandle(tok);
        return false;
    }
    auto* groups = reinterpret_cast<TOKEN_GROUPS*>(buf.data());
    bool found = false;
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
        if (EqualSid(groups->Groups[i].Sid, users_buf)) {
            found = true;
            break;
        }
    }
    CloseHandle(tok);
    return found;
}

std::wstring identity_dir() {
    wchar_t pub[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"PUBLIC", pub, MAX_PATH) == 0) {
        return L"C:\\Users\\Public\\Scylla";
    }
    return std::wstring(pub) + L"\\Scylla";
}

std::wstring identity_status_path() { return identity_dir() + L"\\identity-status.json"; }

std::wstring identity_stop_path() { return identity_dir() + L"\\identity-stop"; }

bool stop_requested() {
    return GetFileAttributesW(identity_stop_path().c_str()) != INVALID_FILE_ATTRIBUTES;
}

void write_identity_error(const std::wstring& msg) {
    const std::wstring path = identity_status_path();
    const std::wstring dir = path_dirname(path);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wofstream out(path);
    if (!out) {
        return;
    }
    out << L"{\"ok\":false,\"state\":\"REFUSED\",\"error\":\"" << json_escape(msg)
        << L"\",\"ramBytes\":0,\"tcp\":0,\"pids\":[]}\n";
}

void write_identity_status(const std::wstring& user, const std::vector<DWORD>& pids, ULONGLONG ram, DWORD tcp,
                            const wchar_t* state) {
    const std::wstring path = identity_status_path();
    const std::wstring dir = path_dirname(path);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wofstream out(path);
    if (!out) {
        return;
    }
    out << L"{\"ok\":true,\"user\":\"" << user << L"\",\"state\":\"" << state << L"\",\"ramBytes\":" << ram
        << L",\"tcp\":" << tcp << L",\"pids\":[";
    for (size_t i = 0; i < pids.size(); ++i) {
        if (i) {
            out << L",";
        }
        out << pids[i];
    }
    out << L"]}\n";
}

void clear_identity_status() { DeleteFileW(identity_status_path().c_str()); }

ULONGLONG ram_for_pids(const std::vector<DWORD>& pids) {
    ULONGLONG total = 0;
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            continue;
        }
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
            total += pmc.WorkingSetSize;
        }
        CloseHandle(h);
    }
    return total;
}

DWORD tcp_for_pids(const std::vector<DWORD>& pids) {
    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (sz == 0) {
        return 0;
    }
    std::vector<BYTE> buf(sz);
    if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
        return 0;
    }
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
    DWORD n = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const DWORD owner = table->table[i].dwOwningPid;
        if (process_in_list(owner, pids)) {
            ++n;
        }
    }
    return n;
}

bool token_is_user(HANDLE proc, const std::wstring& expect_sid) {
    std::wstring sid;
    return process_token_user_sid(proc, sid) && path_lower(sid) == path_lower(expect_sid);
}

bool apply_identity_job(HANDLE job, std::wstring& err) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli))) {
        err = L"SetInformationJobObject: " + win32_error(GetLastError());
        return false;
    }
    return true;
}

DWORD activate_aumid_pid(const std::wstring& aumid, std::wstring& err) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IApplicationActivationManager* aam = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IApplicationActivationManager, reinterpret_cast<void**>(&aam));
    if (FAILED(hr) || aam == nullptr) {
        wchar_t buf[80]{};
        swprintf_s(buf, L"CoCreateInstance ApplicationActivationManager HRESULT 0x%08X",
                    static_cast<unsigned>(hr));
        err = buf;
        return 0;
    }
    DWORD pid = 0;
    hr = aam->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &pid);
    aam->Release();
    if (FAILED(hr) || pid == 0) {
        wchar_t buf[80]{};
        swprintf_s(buf, L"ActivateApplication HRESULT 0x%08X pid=%u", static_cast<unsigned>(hr), pid);
        err = buf;
        return 0;
    }
    return pid;
}

bool related_to_target(const ProcessImage& im, const std::wstring& app, const std::wstring& family_file) {
    const std::wstring name = path_lower(im.filename);
    if (!family_file.empty() && name == path_lower(family_file)) {
        return true;
    }
    if (name == L"chatgpt.exe" || name == L"chatbox.exe") {
        return true;
    }
    if (!app.empty() && !im.path.empty()) {
        const std::wstring root = path_dirname(app);
        const std::wstring root_l = path_lower(root);
        if (!root.empty() && root_l != L"c:\\windows" && root_l != L"c:\\windows\\system32"
            && root_l != L"c:\\program files" && root_l != L"c:\\program files (x86)"
            && path_is_under(im.path, root)) {
            return true;
        }
    }
    return false;
}

DWORD start_appsfolder(const std::wstring& aumid, std::wstring& err) {
    wchar_t windir[MAX_PATH]{};
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::wstring explorer = std::wstring(windir) + L"\\explorer.exe";
    std::wstring cmd = L"\"" + explorer + L"\" shell:AppsFolder\\" + aumid;
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    wchar_t cwd[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", cwd, MAX_PATH);
    if (!CreateProcessW(explorer.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                          cwd[0] ? cwd : nullptr, &si, &pi)) {
        err = L"explorer AppsFolder: " + win32_error(GetLastError());
        return 0;
    }
    const DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pid;
}

std::vector<DWORD> related_live_pids(const std::wstring& expect_text, const std::wstring& app,
                                     const std::wstring& family_file, DWORD skip_pid) {
    std::vector<DWORD> out;
    for (const auto& im : snapshot_process_images()) {
        if (im.pid == skip_pid || im.pid == 0 || im.pid == 4) {
            continue;
        }
        if (!related_to_target(im, app, family_file)) {
            continue;
        }
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, im.pid);
        if (!hp) {
            continue;
        }
        if (token_is_user(hp, expect_text)) {
            out.push_back(im.pid);
        }
        CloseHandle(hp);
    }
    return out;
}

void try_assign_related(HANDLE job, const std::wstring& expect_text, const std::wstring& app,
                        const std::wstring& family_file, DWORD skip_pid) {
    auto job_pids = job_process_ids(job);
    for (const auto& im : snapshot_process_images()) {
        if (im.pid == skip_pid || im.pid == 0 || im.pid == 4) {
            continue;
        }
        if (!related_to_target(im, app, family_file)) {
            continue;
        }
        HANDLE hp = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, im.pid);
        if (!hp) {
            continue;
        }
        if (!token_is_user(hp, expect_text)) {
            CloseHandle(hp);
            continue;
        }
        if (!process_in_list(im.pid, job_pids)) {
            AssignProcessToJobObject(job, hp);
        }
        CloseHandle(hp);
    }
}

void kill_related(const std::wstring& expect_text, const std::wstring& app, const std::wstring& family_file,
                  DWORD skip_pid) {
    for (const auto& im : snapshot_process_images()) {
        if (im.pid == skip_pid || im.pid == 0 || im.pid == 4) {
            continue;
        }
        if (!related_to_target(im, app, family_file)) {
            continue;
        }
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, im.pid);
        if (!hp) {
            continue;
        }
        if (token_is_user(hp, expect_text)) {
            TerminateProcess(hp, 1);
        }
        CloseHandle(hp);
    }
}

std::vector<DWORD> merge_pids(const std::vector<DWORD>& a, const std::vector<DWORD>& b) {
    std::vector<DWORD> out = a;
    for (DWORD p : b) {
        if (!process_in_list(p, out)) {
            out.push_back(p);
        }
    }
    return out;
}

int cmd_identity_supervise(int argc, wchar_t** argv) {
    std::wstring user = arg_after(argc, argv, L"--user");
    if (user.empty()) {
        user = L"ScyllaUser";
    }
    std::wstring app = arg_after(argc, argv, L"--app");
    std::wstring aumid = arg_after(argc, argv, L"--aumid");
    const bool harvest_only = has_flag(argc, argv, L"--harvest-only");
    if (path_lower(app) == L"chatgpt") {
        app = chatgpt_exe();
        if (aumid.empty()) {
            aumid = chatgpt_aumid();
        }
    }
    if (aumid.empty() && !app.empty() && path_is_windowsapps(app)) {
        aumid = chatgpt_aumid();
    }

    PSID expect_sid = nullptr;
    if (!lookup_user_sid(user, &expect_sid) || expect_sid == nullptr) {
        write_identity_error(L"could not resolve SID for " + user);
        std::wcout << L"SCYLLA: REFUSED\ncould not resolve SID for " << user << L"\n";
        return 2;
    }
    LPWSTR expect_str = nullptr;
    std::wstring expect_text;
    if (ConvertSidToStringSidW(expect_sid, &expect_str)) {
        expect_text = expect_str;
        LocalFree(expect_str);
    }
    LocalFree(expect_sid);

    std::wstring self_sid;
    if (!process_token_user_sid(GetCurrentProcess(), self_sid) || path_lower(self_sid) != path_lower(expect_text)) {
        write_identity_error(L"supervise token is not ScyllaUser (got " + self_sid + L")");
        std::wcout << L"SCYLLA: REFUSED\nsupervise as the operator is not a cage\n";
        return 2;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        const DWORD e = GetLastError();
        write_identity_error(L"CreateJobObject: " + win32_error(e));
        std::wcout << L"SCYLLA: REFUSED\nCreateJobObject: " << win32_error(e) << L"\n";
        return 2;
    }
    std::wstring jerr;
    if (!apply_identity_job(job, jerr)) {
        write_identity_error(jerr);
        CloseHandle(job);
        std::wcout << L"SCYLLA: REFUSED\n" << jerr << L"\n";
        return 2;
    }

    DeleteFileW(identity_stop_path().c_str());

    const DWORD self_pid = GetCurrentProcessId();
    DWORD pid = 0;
    DWORD explorer_pid = 0;
    std::wstring launch_notes;
    if (!aumid.empty() && !harvest_only) {
        std::wstring err;
        pid = activate_aumid_pid(aumid, err);
        HANDLE app_proc = nullptr;
        if (pid != 0) {
            app_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | PROCESS_SET_QUOTA, FALSE, pid);
        } else {
            launch_notes = err;
        }
        if (app_proc) {
            std::wstring tok;
            if (!process_token_user_sid(app_proc, tok) || path_lower(tok) != path_lower(expect_text)) {
                write_identity_error(L"launched process is not the isolated user; terminated");
                TerminateProcess(app_proc, 1);
                CloseHandle(app_proc);
                CloseHandle(job);
                std::wcout << L"SCYLLA: REFUSED\nlaunched process is not the isolated user; terminated\n";
                return 2;
            }
            AssignProcessToJobObject(job, app_proc);
            CloseHandle(app_proc);
        }
        if (pid == 0 || app_proc == nullptr) {
            std::wstring expl_err;
            explorer_pid = start_appsfolder(aumid, expl_err);
            if (explorer_pid == 0) {
                if (!launch_notes.empty()) {
                    launch_notes += L"; ";
                }
                launch_notes += expl_err;
            }
        }
    } else if (aumid.empty()) {
        if (app.empty() || GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
            write_identity_error(L"executable not found");
            CloseHandle(job);
            std::wcout << L"SCYLLA: REFUSED\nexecutable not found\n";
            return 2;
        }
        std::wstring cmd = L"\"" + app + L"\"";
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        wchar_t cwd[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", cwd, MAX_PATH);
        if (!CreateProcessW(app.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                            cwd[0] ? cwd : nullptr, &si, &pi)) {
            const DWORD e = GetLastError();
            write_identity_error(L"CreateProcess: " + win32_error(e));
            CloseHandle(job);
            std::wcout << L"SCYLLA: REFUSED\nCreateProcess: " << win32_error(e) << L"\n";
            return 2;
        }
        pid = pi.dwProcessId;
        std::wstring tok;
        if (!process_token_user_sid(pi.hProcess, tok) || path_lower(tok) != path_lower(expect_text)) {
            write_identity_error(L"launched process is not the isolated user; terminated");
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(job);
            std::wcout << L"SCYLLA: REFUSED\nlaunched process is not the isolated user; terminated\n";
            return 2;
        }
        AssignProcessToJobObject(job, pi.hProcess);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    const std::wstring family_file = app.empty() ? L"chatgpt.exe" : path_filename(app);
    std::wcout << L"SCYLLA IDENTITY SESSION\nAccount:\n    " << user << L"\nPID:\n    " << pid << L"\n";
    write_identity_status(user, {}, 0, 0, L"STARTING");
    bool seen = false;
    const DWORD t0 = GetTickCount();
    for (;;) {
        try_assign_related(job, expect_text, app, family_file, self_pid);
        const auto job_pids = job_process_ids(job);
        const auto live = related_live_pids(expect_text, app, family_file, self_pid);
        const auto status_pids = merge_pids(job_pids, live);
        write_identity_status(user, status_pids, ram_for_pids(status_pids), tcp_for_pids(status_pids),
                               status_pids.empty() ? (seen ? L"STOPPING" : L"STARTING") : L"RUNNING");
        if (stop_requested()) {
            kill_related(expect_text, app, family_file, self_pid);
            if (explorer_pid != 0) {
                terminate_process(explorer_pid);
            }
            DeleteFileW(identity_stop_path().c_str());
            clear_identity_status();
            CloseHandle(job);
            return 0;
        }
        if (seen && status_pids.empty()) {
            break;
        }
        if (!status_pids.empty()) {
            seen = true;
        }
        if (!seen && GetTickCount() - t0 > 30000) {
            std::wstring msg = harvest_only
                ? L"Store ChatGPT did not start. Windows will not put that package's window on this desktop as ScyllaUser. Switch user to ScyllaUser and stay signed in, then launch again. Win32 apps (VLC) appear here."
                : L"no isolated process tree after 20s";
            if (!launch_notes.empty()) {
                msg += L" (" + launch_notes + L")";
            }
            write_identity_error(msg);
            kill_related(expect_text, app, family_file, self_pid);
            if (explorer_pid != 0) {
                terminate_process(explorer_pid);
            }
            CloseHandle(job);
            std::wcout << L"SCYLLA: REFUSED\n" << msg << L"\n";
            return 2;
        }
        Sleep(750);
    }
    kill_related(expect_text, app, family_file, self_pid);
    if (explorer_pid != 0) {
        terminate_process(explorer_pid);
    }
    clear_identity_status();
    CloseHandle(job);
    return 0;
}

int cmd_identity_activate_aumid(int argc, wchar_t** argv) {
    std::wstring user = arg_after(argc, argv, L"--user");
    if (user.empty()) {
        user = L"ScyllaUser";
    }
    std::wstring aumid;
    for (int i = 3; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--user") {
            ++i;
            continue;
        }
        if (!a.empty() && a[0] != L'-') {
            aumid = a;
            break;
        }
    }
    if (aumid.empty()) {
        std::wcout << L"SCYLLA: REFUSED\nidentity activate-aumid <aumid>\n";
        return 2;
    }

    PSID expect_sid = nullptr;
    if (!lookup_user_sid(user, &expect_sid) || expect_sid == nullptr) {
        std::wcout << L"SCYLLA: REFUSED\ncould not resolve SID for " << user << L"\n";
        return 2;
    }
    LPWSTR expect_str = nullptr;
    std::wstring expect_text;
    if (ConvertSidToStringSidW(expect_sid, &expect_str)) {
        expect_text = expect_str;
        LocalFree(expect_str);
    }
    LocalFree(expect_sid);

    std::wstring self_sid;
    if (!process_token_user_sid(GetCurrentProcess(), self_sid) || path_lower(self_sid) != path_lower(expect_text)) {
        std::wcout << L"SCYLLA: REFUSED\nActivateApplication as the operator is not a cage\n";
        return 2;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IApplicationActivationManager* aam = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IApplicationActivationManager, reinterpret_cast<void**>(&aam));
    if (FAILED(hr) || aam == nullptr) {
        std::wcout << L"SCYLLA: REFUSED\nCoCreateInstance ApplicationActivationManager HRESULT 0x"
                   << std::hex << static_cast<unsigned long>(hr) << std::dec << L"\n";
        return 2;
    }
    DWORD pid = 0;
    hr = aam->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &pid);
    aam->Release();
    if (FAILED(hr) || pid == 0) {
        std::wcout << L"SCYLLA: REFUSED\nActivateApplication HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(hr) << std::dec
                   << L"\nStore ChatGPT Isolated User works from the ScyllaUser interactive desktop (Switch user).\n";
        return 2;
    }

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    std::wstring tok_sid;
    const bool got_sid = proc != nullptr && process_token_user_sid(proc, tok_sid);
    const bool has_users = proc != nullptr && process_token_has_users(proc);
    const bool match = got_sid && path_lower(tok_sid) == path_lower(expect_text);
    std::wcout << L"SCYLLA IDENTITY LAUNCH\n\nAccount:\n    " << user << L"\nAUMID:\n    " << aumid << L"\nPID:\n    "
               << pid << L"\nToken user:\n    " << (got_sid ? tok_sid : L"(query failed)") << L"\nExpected SID:\n    "
               << expect_text << L"\nUsers SID on token:\n    " << (has_users ? L"PRESENT" : L"MISSING")
               << L"\nMatch:\n    " << (match ? L"YES" : L"NO") << L"\n";
    if (!match) {
        if (proc) {
            TerminateProcess(proc, 1);
        }
        if (proc) {
            CloseHandle(proc);
        }
        std::wcout << L"\nSCYLLA: REFUSED\nactivated process is not the isolated user; terminated\n";
        return 2;
    }
    if (proc) {
        CloseHandle(proc);
    }
    return 0;
}

int cmd_identity_launch(int argc, wchar_t** argv) {
    std::wstring user = arg_after(argc, argv, L"--user");
    if (user.empty()) {
        user = L"ScyllaUser";
    }
    const bool app_flag = has_flag(argc, argv, L"--app");
    std::wstring app = arg_after(argc, argv, L"--app");
    std::wstring aumid = arg_after(argc, argv, L"--aumid");
    if (app_flag && (app.empty() || app == L"--user" || app == L"--app" || app == L"--aumid")) {
        std::wcout << L"SCYLLA: REFUSED\n--app needs a path (PowerShell $exe was empty; do not Get-ChildItem WindowsApps)\n";
        std::wcout << L"use: .\\scylla.exe identity launch --app chatgpt\n";
        return 2;
    }
    if (app.empty() && aumid.empty()) {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        app = std::wstring(sys) + L"\\notepad.exe";
    } else if (path_lower(app) == L"chatgpt") {
        app = chatgpt_exe();
        if (app.empty() && aumid.empty()) {
            std::wcout << L"SCYLLA: REFUSED\nChatGPT.exe not found under WindowsApps\n";
            return 2;
        }
        if (aumid.empty()) {
            aumid = chatgpt_aumid();
        }
    }
    if (aumid.empty() && !app.empty() && GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wcout << L"SCYLLA: REFUSED\nexecutable not found: " << app << L"\n";
        return 2;
    }
    if (!app.empty() && path_is_windowsapps(app) && aumid.empty()) {
        aumid = chatgpt_aumid();
    }

    wchar_t profiles[MAX_PATH]{};
    DWORD plen = MAX_PATH;
    std::wstring cwd = L"C:\\Users\\" + user;
    if (GetProfilesDirectoryW(profiles, &plen)) {
        cwd = std::wstring(profiles) + L"\\" + user;
    }
    if (GetFileAttributesW(cwd.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wcout << L"SCYLLA: REFUSED\nprofile not initialized: " << cwd << L"\n";
        return 2;
    }

    PSID expect_sid = nullptr;
    if (!lookup_user_sid(user, &expect_sid) || expect_sid == nullptr) {
        std::wcout << L"SCYLLA: REFUSED\ncould not resolve SID for " << user << L"\n";
        return 2;
    }
    LPWSTR expect_str = nullptr;
    std::wstring expect_text;
    if (ConvertSidToStringSidW(expect_sid, &expect_str)) {
        expect_text = expect_str;
        LocalFree(expect_str);
    }
    LocalFree(expect_sid);

    std::wstring password = read_console_password();
    if (password.empty()) {
        std::wcout << L"SCYLLA: REFUSED\nempty password\n";
        return 2;
    }

    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);

    if (!aumid.empty()) {
        wchar_t windir[MAX_PATH]{};
        GetWindowsDirectoryW(windir, MAX_PATH);
        const std::wstring explorer = std::wstring(windir) + L"\\explorer.exe";
        std::wstring ecmd = L"\"" + explorer + L"\" shell:AppsFolder\\" + aumid;
        std::vector<wchar_t> ebuf(ecmd.begin(), ecmd.end());
        ebuf.push_back(0);
        STARTUPINFOW esi{};
        esi.cb = sizeof(esi);
        PROCESS_INFORMATION epi{};
        if (!CreateProcessWithLogonW(user.c_str(), L".", password.c_str(), LOGON_WITH_PROFILE, explorer.c_str(),
                                    ebuf.data(), CREATE_UNICODE_ENVIRONMENT, nullptr, cwd.c_str(), &esi, &epi)) {
            const DWORD e = GetLastError();
            SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
            std::wcout << L"SCYLLA: REFUSED\nCreateProcessWithLogonW(explorer AppsFolder): " << win32_error(e) << L"\n";
            return 2;
        }
        CloseHandle(epi.hThread);
        CloseHandle(epi.hProcess);
    }

    std::wstring cmd = L"\"" + std::wstring(self) + L"\" identity supervise --user " + user;
    if (!aumid.empty()) {
        cmd += L" --aumid ";
        cmd += aumid;
        cmd += L" --harvest-only";
    }
    if (!app.empty()) {
        cmd += L" --app \"";
        cmd += app;
        cmd += L"\"";
    }
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessWithLogonW(
        user.c_str(),
        L".",
        password.c_str(),
        LOGON_WITH_PROFILE,
        self,
        cmd_buf.data(),
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        nullptr,
        cwd.c_str(),
        &si,
        &pi);
    SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
    password.clear();
    if (!ok) {
        std::wcout << L"SCYLLA: REFUSED\nCreateProcessWithLogonW(supervise): " << win32_error(GetLastError()) << L"\n";
        return 2;
    }
    std::wcout << L"SCYLLA IDENTITY LAUNCH\n\nAccount:\n    " << user << L"\nSupervisor PID:\n    " << pi.dwProcessId
               << L"\nWaiting until the app session ends (STOP kills this tree).\n";
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        std::wcout << L"SCYLLA: REFUSED\nsupervise exit " << code << L"\n";
        return 2;
    }
    std::wcout << L"Session ended.\n";
    return 0;
}

}  // namespace

int cmd_identity(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcout << L"scylla.exe identity check [--user ScyllaUser] [--json]\n";
        std::wcout << L"scylla.exe identity launch [--user ScyllaUser] [--app <exe>|chatgpt]\n";
        return 2;
    }
    const std::wstring sub = argv[2];
    if (sub == L"launch") {
        return cmd_identity_launch(argc, argv);
    }
    if (sub == L"supervise") {
        return cmd_identity_supervise(argc, argv);
    }
    if (sub == L"activate-aumid") {
        return cmd_identity_activate_aumid(argc, argv);
    }
    if (sub != L"check") {
        std::wcout << L"scylla.exe identity check [--user ScyllaUser] [--json]\n";
        std::wcout << L"scylla.exe identity launch [--user ScyllaUser] [--app <exe>|chatgpt]\n";
        return 2;
    }
    const bool json = has_flag(argc, argv, L"--json");
    std::wstring user = arg_after(argc, argv, L"--user");
    if (user.empty()) {
        user = L"ScyllaUser";
    }

    USER_INFO_1* ui = nullptr;
    const NET_API_STATUS ust = NetUserGetInfo(nullptr, user.c_str(), 1, reinterpret_cast<LPBYTE*>(&ui));
    const bool exists = (ust == NERR_Success && ui != nullptr);
    const bool enabled = exists && ((ui->usri1_flags & UF_ACCOUNTDISABLE) == 0);
    const bool password_required = exists && ((ui->usri1_flags & UF_PASSWD_NOTREQD) == 0);
    if (ui) {
        NetApiBufferFree(ui);
    }

    PSID sid = nullptr;
    const bool sid_ok = exists && lookup_user_sid(user, &sid);
    LPWSTR sid_str = nullptr;
    std::wstring sid_text;
    if (sid_ok && ConvertSidToStringSidW(sid, &sid_str)) {
        sid_text = sid_str;
        LocalFree(sid_str);
    }

    const bool admin = exists && user_in_local_group(user, L"Administrators");
    const bool in_users = exists && user_in_local_group(user, L"Users");

    wchar_t profiles[MAX_PATH]{};
    DWORD plen = MAX_PATH;
    std::wstring profile_path = L"C:\\Users\\" + user;
    if (GetProfilesDirectoryW(profiles, &plen)) {
        profile_path = std::wstring(profiles) + L"\\" + user;
    }
    const DWORD pattr = GetFileAttributesW(profile_path.c_str());
    const bool profile_ok = pattr != INVALID_FILE_ATTRIBUTES && (pattr & FILE_ATTRIBUTE_DIRECTORY);

    wchar_t op_profile[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", op_profile, MAX_PATH);
    const bool deny_profile = sid_ok && dacl_has_deny_for_sid(op_profile, sid);

    wchar_t docs[MAX_PATH]{};
    const bool docs_ok = SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs));
    const bool deny_docs = sid_ok && docs_ok && dacl_has_deny_for_sid(docs, sid);

    std::wstring ssh = std::wstring(op_profile) + L"\\.ssh";
    const DWORD ssh_attr = GetFileAttributesW(ssh.c_str());
    const bool ssh_exists = ssh_attr != INVALID_FILE_ATTRIBUTES;
    const bool deny_ssh = sid_ok && ssh_exists && dacl_has_deny_for_sid(ssh, sid);

    const std::wstring gpt = chatgpt_exe();
    const bool gpt_exec = !gpt.empty() && GetFileAttributesW(gpt.c_str()) != INVALID_FILE_ATTRIBUTES;

    std::vector<std::wstring> blockers;
    if (!exists) {
        blockers.push_back(L"account missing");
    }
    if (exists && !enabled) {
        blockers.push_back(L"account disabled");
    }
    if (admin) {
        blockers.push_back(L"Administrator");
    }
    if (!in_users) {
        blockers.push_back(L"not in Users (Store ChatGPT needs Users RX on WindowsApps)");
    }
    if (!profile_ok) {
        blockers.push_back(L"profile not initialized");
    }
    if (!deny_profile) {
        blockers.push_back(L"no ScyllaUser DENY on operator profile");
    }
    if (exists && !password_required) {
        blockers.push_back(L"PasswordRequired=false (set a password before LogonUser)");
    }
    const bool ready = blockers.empty();

    if (sid) {
        LocalFree(sid);
    }

    if (json) {
        std::wcout << L"{\"ok\":" << (ready ? L"true" : L"false") << L",\"account\":\"" << json_escape(user)
                   << L"\",\"exists\":" << (exists ? L"true" : L"false") << L",\"enabled\":"
                   << (enabled ? L"true" : L"false") << L",\"administrator\":" << (admin ? L"true" : L"false")
                   << L",\"inUsers\":" << (in_users ? L"true" : L"false") << L",\"passwordRequired\":"
                   << (password_required ? L"true" : L"false") << L",\"sid\":";
        if (sid_text.empty()) {
            std::wcout << L"null";
        } else {
            std::wcout << L"\"" << json_escape(sid_text) << L"\"";
        }
        std::wcout << L",\"profile\":\"" << json_escape(profile_path) << L"\",\"profileInitialized\":"
                   << (profile_ok ? L"true" : L"false") << L",\"operatorProfileDeny\":"
                   << (deny_profile ? L"true" : L"false") << L",\"operatorDocumentsDeny\":"
                   << (deny_docs ? L"true" : L"false") << L",\"operatorSshDeny\":"
                   << (deny_ssh ? L"true" : L"false") << L",\"windowsAppsExecutable\":"
                   << (gpt_exec ? L"true" : L"false") << L",\"status\":\""
                   << (ready ? L"READY" : L"NOT_READY") << L"\"}\n";
        return ready ? 0 : 1;
    }

    std::wcout << L"SCYLLA IDENTITY CHECK\n\n";
    std::wcout << L"Account:\n    " << user << L"\n\n";
    std::wcout << L"Exists:\n    " << (exists ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Enabled:\n    " << (enabled ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Administrator:\n    " << (admin ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Users group:\n    " << (in_users ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Password required:\n    " << (password_required ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"SID:\n    " << (sid_text.empty() ? L"(none)" : sid_text) << L"\n\n";
    std::wcout << L"Profile:\n    " << profile_path << L"\n\n";
    std::wcout << L"Profile initialized:\n    " << (profile_ok ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Operator profile DENY:\n    " << (deny_profile ? L"YES" : L"NO") << L"    (" << op_profile
               << L")\n\n";
    std::wcout << L"Operator Documents DENY:\n    " << (deny_docs ? L"YES" : L"NO") << L"\n\n";
    std::wcout << L"Operator .ssh DENY:\n    " << (ssh_exists ? (deny_ssh ? L"YES" : L"NO") : L"path missing")
               << L"\n\n";
    std::wcout << L"WindowsApps ChatGPT.exe (Users):\n    " << (gpt_exec ? L"EXECUTABLE" : L"NOT FOUND") << L"\n\n";
    std::wcout << L"Status:\n    " << (ready ? L"READY" : L"NOT_READY") << L"\n";
    if (!ready) {
        std::wcout << L"\nBlockers:\n";
        for (const auto& b : blockers) {
            std::wcout << L"    - " << b << L"\n";
        }
    }
    return ready ? 0 : 1;
}

}  // namespace scylla
