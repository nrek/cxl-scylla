#include "session.h"

#include "acl.h"
#include "path.h"

#include <sddl.h>
#include <userenv.h>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <string>

namespace scylla {
namespace {

HANDLE g_stop_event = nullptr;

BOOL WINAPI console_ctrl(DWORD) {
    if (g_stop_event) {
        SetEvent(g_stop_event);
    }
    return TRUE;
}

std::wstring module_dir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return path_dirname(exe);
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string s(static_cast<size_t>(n - 1), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring utf16(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) {
        return {};
    }
    std::wstring w(static_cast<size_t>(n - 1), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

const wchar_t* state_text(SessionState s) {
    switch (s) {
        case SessionState::Clean:
            return L"CLEAN";
        case SessionState::Degraded:
            return L"DEGRADED";
        case SessionState::Refused:
            return L"REFUSED";
        case SessionState::CleanupRequired:
            return L"CLEANUP_REQUIRED";
    }
    return L"UNKNOWN";
}

std::wstring arg_after(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return L"";
}

std::vector<std::wstring> args_all(int argc, wchar_t** argv, const std::wstring& key) {
    std::vector<std::wstring> out;
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            out.push_back(argv[i + 1]);
        }
    }
    return out;
}

int arg_int(int argc, wchar_t** argv, const std::wstring& key, int fallback) {
    const std::wstring v = arg_after(argc, argv, key);
    if (v.empty()) {
        return fallback;
    }
    return _wtoi(v.c_str());
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

std::string json_escape(const std::wstring& w) {
    const std::string u = utf8(w);
    std::string o;
    o.reserve(u.size() + 8);
    for (unsigned char c : u) {
        if (c == '"') {
            o += "\\\"";
        } else if (c == '\\') {
            o += "\\\\";
        } else if (c == '\n') {
            o += "\\n";
        } else if (c == '\r') {
            o += "\\r";
        } else if (c < 0x20) {
            continue;
        } else {
            o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

void emit_json_line(const std::string& s) {
    std::cout << s << "\n" << std::flush;
}

std::string json_related_array(const std::vector<RelatedProcess>& rows, bool external_only) {
    std::string o = "[";
    bool first = true;
    for (const auto& r : rows) {
        if (external_only && r.in_job) {
            continue;
        }
        if (!first) {
            o += ",";
        }
        first = false;
        o += "{\"pid\":" + std::to_string(r.pid);
        o += ",\"image\":\"" + json_escape(r.filename) + "\"";
        o += ",\"path\":\"" + json_escape(r.path) + "\"";
        o += ",\"contained\":";
        o += r.in_job ? "true" : "false";
        o += ",\"appContainer\":";
        o += r.is_appcontainer ? "true" : "false";
        o += "}";
    }
    o += "]";
    return o;
}

void json_refused(const char* code, const std::wstring& message, const std::vector<RelatedProcess>& details) {
    std::string o = "{\"ok\":false,\"state\":\"REFUSED\",\"code\":\"";
    o += code;
    o += "\",\"message\":\"" + json_escape(message) + "\",\"details\":";
    o += json_related_array(details, false);
    o += "}";
    emit_json_line(o);
}

void print_related(const std::vector<RelatedProcess>& rows, bool external_only) {
    for (const auto& r : rows) {
        if (external_only && r.in_job) {
            continue;
        }
        std::wcout << L"    " << r.filename << L"  PID " << r.pid;
        if (!r.path.empty()) {
            std::wcout << L"\n        " << r.path;
        }
        std::wcout << L"\n        contained: " << (r.in_job ? L"YES" : L"NO");
        std::wcout << L"  AppContainer: " << (r.is_appcontainer ? L"YES" : L"NO");
        if (!r.sid.empty()) {
            std::wcout << L"\n        SID: " << r.sid;
        }
        if (!r.note.empty()) {
            std::wcout << L"\n        " << r.note;
        }
        std::wcout << L"\n";
    }
}

SessionState evaluate(const std::vector<RelatedProcess>& related, const std::vector<DWORD>& job_ids,
                      const std::wstring& expected_sid) {
    bool degraded = false;
    for (const auto& r : related) {
        if (!r.in_job) {
            degraded = true;
        } else if (!r.is_appcontainer) {
            degraded = true;
        } else if (!expected_sid.empty() && !r.sid.empty() && path_lower(r.sid) != path_lower(expected_sid)) {
            degraded = true;
        }
    }
    for (DWORD pid : job_ids) {
        const AppContainerInfo ac = query_process_appcontainer(pid);
        if (!ac.queried || !ac.is_appcontainer) {
            degraded = true;
        } else if (!expected_sid.empty() && !ac.sid.empty() && path_lower(ac.sid) != path_lower(expected_sid)) {
            degraded = true;
        }
    }
    return degraded ? SessionState::Degraded : SessionState::Clean;
}

bool pid_alive(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
}

}  // namespace

AppProfile resolve_builtin_profile(const std::wstring& id, const std::wstring& scylla_dir) {
    AppProfile p;
    p.id = path_lower(id);
    if (p.id == L"test-parent") {
        p.display_name = L"Scylla Test Parent";
        p.primary_executable = scylla_dir + L"\\scylla-test-parent.exe";
        p.install_roots.push_back(path_dirname(p.primary_executable));
        p.known_executables.push_back(L"scylla-test-parent.exe");
    }
    return p;
}

AppProfile profile_from_executable(const std::wstring& exe) {
    AppProfile p;
    if (exe.empty()) {
        return p;
    }
    wchar_t full[MAX_PATH * 4]{};
    if (!GetFullPathNameW(exe.c_str(), MAX_PATH * 4, full, nullptr)) {
        return p;
    }
    p.primary_executable = full;
    p.display_name = path_filename(p.primary_executable);
    p.id = path_lower(p.display_name);
    const auto dot = p.id.rfind(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        p.id = p.id.substr(0, dot);
    }
    p.install_roots.push_back(path_dirname(p.primary_executable));
    p.known_executables.push_back(path_filename(p.primary_executable));
    return p;
}

std::wstring session_file_path() {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"scylla-strict-session.txt";
}

bool write_session_file(const ContainedRun& run, const AppProfile& profile, std::wstring& err) {
    std::ofstream f(utf8(session_file_path()).c_str(), std::ios::binary | std::ios::trunc);
    if (!f) {
        err = L"could not write session file";
        return false;
    }
    auto line = [&](const char* k, const std::wstring& v) { f << k << utf8(v) << "\n"; };
    line("session=", run.session_name);
    line("job=", run.job_name);
    line("stop_event=", run.stop_event_name);
    line("sid=", run.sid_string);
    line("profile=", profile.id);
    line("exe=", profile.primary_executable);
    f << "pid=" << run.pid << "\n";
    f << "scylla_pid=" << GetCurrentProcessId() << "\n";
    for (const auto& p : run.granted_rw) {
        line("grant_rw=", p);
    }
    for (const auto& p : run.granted_ro) {
        line("grant_ro=", p);
    }
    for (const auto& p : run.granted_rx) {
        line("grant_rx=", p);
    }
    return true;
}

SessionFile read_session_file() {
    SessionFile s;
    std::ifstream f(utf8(session_file_path()).c_str(), std::ios::binary);
    if (!f) {
        return s;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string k = line.substr(0, eq);
        const std::wstring v = utf16(line.substr(eq + 1));
        if (k == "session") {
            s.session_name = v;
        } else if (k == "job") {
            s.job_name = v;
        } else if (k == "stop_event") {
            s.stop_event_name = v;
        } else if (k == "sid") {
            s.sid = v;
        } else if (k == "profile") {
            s.profile = v;
        } else if (k == "exe") {
            s.exe = v;
        } else if (k == "pid") {
            s.pid = static_cast<DWORD>(_wtoi(v.c_str()));
        } else if (k == "scylla_pid") {
            s.scylla_pid = static_cast<DWORD>(_wtoi(v.c_str()));
        } else if (k == "grant_rw") {
            s.grant_rw.push_back(v);
        } else if (k == "grant_ro") {
            s.grant_ro.push_back(v);
        } else if (k == "grant_rx") {
            s.grant_rx.push_back(v);
        }
    }
    s.present = !s.session_name.empty();
    return s;
}

void remove_session_file() {
    DeleteFileW(session_file_path().c_str());
}

std::vector<std::wstring> list_sibling_executables(const std::wstring& root) {
    std::vector<std::wstring> out;
    const std::wstring glob = root + L"\\*.exe";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        out.push_back(root + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::vector<RelatedProcess> find_related(const AppProfile& profile, HANDLE job, const std::wstring& expected_sid) {
    std::vector<RelatedProcess> out;
    const std::vector<DWORD> job_ids = job ? job_process_ids(job) : std::vector<DWORD>{};
    const DWORD self = GetCurrentProcessId();
    for (const auto& img : snapshot_process_images()) {
        if (img.pid == self || img.pid == 0 || img.pid == 4) {
            continue;
        }
        if (!process_is_related(img, profile.install_roots, profile.known_executables)) {
            continue;
        }
        RelatedProcess r;
        r.pid = img.pid;
        r.path = img.path;
        r.filename = img.filename;
        r.in_job = process_in_list(img.pid, job_ids);
        const AppContainerInfo ac = query_process_appcontainer(img.pid);
        r.is_appcontainer = ac.is_appcontainer;
        r.sid = ac.sid;
        if (r.in_job && !r.is_appcontainer) {
            r.note = L"SCYLLA: CONTAINMENT WARNING — in job but not AppContainer";
        } else if (r.in_job && !expected_sid.empty() && !r.sid.empty() &&
                   path_lower(r.sid) != path_lower(expected_sid)) {
            r.note = L"SCYLLA: CONTAINMENT WARNING — AppContainer SID mismatch";
        } else if (!r.in_job) {
            r.note = L"same application identity, not in this session job";
        }
        out.push_back(std::move(r));
    }
    return out;
}

AppProfile identity_from_session(const SessionFile& s) {
    if (!s.exe.empty()) {
        return profile_from_executable(s.exe);
    }
    return resolve_builtin_profile(s.profile, module_dir());
}

int cmd_audit(int argc, wchar_t** argv) {
    const bool json = has_flag(argc, argv, L"--json");
    std::wstring profile_id = arg_after(argc, argv, L"--app");
    if (profile_id.empty() && argc >= 3 && argv[2][0] != L'-') {
        profile_id = argv[2];
    }
    AppProfile profile;
    if (!arg_after(argc, argv, L"--app").empty()) {
        profile = profile_from_executable(arg_after(argc, argv, L"--app"));
    } else {
        profile = resolve_builtin_profile(profile_id, module_dir());
    }
    if (profile.primary_executable.empty()) {
        if (json) {
            json_refused("UNKNOWN_PROFILE", L"unknown profile or executable", {});
        } else {
            std::wcout << L"SCYLLA: REFUSED\nunknown profile: " << profile_id
                       << L"\nknown: test-parent, or pass --app <exe>\n";
        }
        return 2;
    }
    const DWORD attr = GetFileAttributesW(profile.primary_executable.c_str());
    const bool found = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    std::vector<std::wstring> siblings;
    if (!profile.install_roots.empty()) {
        siblings = list_sibling_executables(profile.install_roots.front());
    }
    const auto related = find_related(profile, nullptr, L"");

    if (json) {
        std::string o = "{\"ok\":true,\"state\":\"AUDITED\",\"product\":\"";
        o += json_escape(profile.display_name);
        o += "\",\"primaryExecutable\":\"" + json_escape(profile.primary_executable) + "\"";
        o += ",\"found\":";
        o += found ? "true" : "false";
        o += ",\"startup\":\"UNVERIFIED\",\"scheduledTasks\":\"UNVERIFIED\",\"services\":\"UNVERIFIED\"";
        o += ",\"protocolHandlers\":\"UNVERIFIED\",\"backgroundRegistrations\":\"UNVERIFIED\"";
        o += ",\"relatedRunning\":" + json_related_array(related, false);
        o += ",\"details\":" + json_related_array(related, false);
        o += "}";
        emit_json_line(o);
        return 0;
    }

    std::wcout << L"SCYLLA APPLICATION AUDIT\n\n";
    std::wcout << L"Product:\n    " << profile.display_name << L"\n\n";
    std::wcout << L"Primary executable:\n    " << (found ? L"FOUND" : L"MISSING") << L"\n    "
               << profile.primary_executable << L"\n\n";
    std::wcout << L"Install roots:\n    " << profile.install_roots.size() << L"\n";
    for (const auto& r : profile.install_roots) {
        std::wcout << L"    " << r << L"\n";
    }
    std::wcout << L"\nKnown executables (identity match):\n";
    for (const auto& k : profile.known_executables) {
        std::wcout << L"    " << k << L"\n";
    }
    std::wcout << L"\nRelated executables on disk (informational):\n    " << siblings.size() << L"\n";
    for (const auto& s : siblings) {
        std::wcout << L"    " << path_filename(s) << L"\n";
    }
    std::wcout << L"\nStartup entries:\n    UNVERIFIED\n";
    std::wcout << L"Scheduled tasks:\n    UNVERIFIED\n";
    std::wcout << L"Services:\n    UNVERIFIED\n";
    std::wcout << L"Protocol handlers:\n    UNVERIFIED\n";
    std::wcout << L"Background registrations:\n    UNVERIFIED\n\n";
    std::wcout << L"Running related processes:\n    " << related.size() << L"\n";
    print_related(related, false);
    std::wcout << L"\nFilesystem grants:\n    NONE (audit is read-only)\n\n";
    std::wcout << L"Status:\n    AUDITED\n";
    return 0;
}

int cmd_strict_launch(int argc, wchar_t** argv) {
    const bool json = has_flag(argc, argv, L"--json");
    if (json) {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    const std::wstring app_path = arg_after(argc, argv, L"--app");
    const std::wstring profile_id = arg_after(argc, argv, L"--profile");
    AppProfile profile;
    if (!app_path.empty()) {
        profile = profile_from_executable(app_path);
    } else if (!profile_id.empty()) {
        profile = resolve_builtin_profile(profile_id, module_dir());
    }
    auto refuse = [&](const char* code, const std::wstring& msg, const std::vector<RelatedProcess>& d = {}) {
        if (json) {
            json_refused(code, msg, d);
        } else {
            std::wcout << L"SCYLLA: REFUSED\n" << msg << L"\n";
        }
        return 2;
    };
    if (profile.primary_executable.empty()) {
        return refuse("MISSING_TARGET", L"strict launch requires --app <exe> or --profile test-parent");
    }
    const DWORD exe_attr = GetFileAttributesW(profile.primary_executable.c_str());
    if (exe_attr == INVALID_FILE_ATTRIBUTES) {
        return refuse("EXECUTABLE_NOT_FOUND", L"primary executable missing: " + profile.primary_executable);
    }

    const SessionFile existing = read_session_file();
    if (existing.present && pid_alive(existing.scylla_pid)) {
        return refuse("SESSION_ALREADY_ACTIVE", L"strict session already active: " + existing.session_name);
    }

    const auto already = find_related(profile, nullptr, L"");
    if (!already.empty()) {
        const bool kill_related = has_flag(argc, argv, L"--kill-related");
        if (!kill_related) {
            if (json) {
                json_refused("RELATED_PROCESS_ALREADY_RUNNING",
                             L"Related application processes are already running outside this Scylla session.", already);
            } else {
                std::wcout << L"SCYLLA: STRICT MODE PREFLIGHT\n\n";
                std::wcout << L"Related processes already running:\n\n";
                print_related(already, false);
                std::wcout << L"\nStrict isolation cannot be asserted while related\n"
                           << L"processes are already active outside this session.\n";
                std::wcout << L"Pass --kill-related to terminate them and continue.\n\n";
                std::wcout << L"Final state:\n    REFUSED\n";
            }
            return 2;
        }
        std::vector<RelatedProcess> killed;
        std::vector<RelatedProcess> failed;
        for (const auto& p : already) {
            if (terminate_process(p.pid)) {
                killed.push_back(p);
            } else {
                failed.push_back(p);
            }
        }
        auto remaining = find_related(profile, nullptr, L"");
        for (int i = 0; i < 40 && !remaining.empty(); ++i) {
            Sleep(100);
            remaining = find_related(profile, nullptr, L"");
        }
        if (!remaining.empty()) {
            return refuse("RELATED_PROCESS_TERMINATE_FAILED",
                          L"Could not terminate related processes still running outside this Scylla session.",
                          remaining);
        }
        if (json) {
            std::string o = "{\"ok\":true,\"event\":\"related_terminated\",\"state\":\"STARTING\",\"code\":\"RELATED_PROCESSES_TERMINATED\",\"message\":\"";
            o += json_escape(L"Terminated related processes that were running outside Scylla.");
            o += "\",\"details\":";
            o += json_related_array(killed, false);
            o += "}";
            emit_json_line(o);
        } else {
            std::wcout << L"Terminated related processes:\n";
            print_related(killed, false);
            if (!failed.empty()) {
                std::wcout << L"Terminate not confirmed for:\n";
                print_related(failed, false);
            }
            std::wcout << L"\n";
        }
    }

    const auto rw = args_all(argc, argv, L"--allow-rw");
    const auto ro = args_all(argc, argv, L"--allow-ro");
    if (rw.empty()) {
        return refuse("MISSING_RW_GRANT", L"strict launch requires at least one --allow-rw");
    }

    LaunchRequest req;
    req.executable = profile.primary_executable;
    req.arguments = arg_after(argc, argv, L"--args");
    req.extra_rw_paths = rw;
    req.extra_ro_paths = ro;
    req.cwd = rw.front();
    req.internet_client = !has_flag(argc, argv, L"--no-internet");
    req.named_lifecycle = true;

    if (!json) {
        std::wcout << L"SCYLLA STRICT SESSION\n\n";
        std::wcout << L"Application:\n    " << profile.display_name << L"\n";
        std::wcout << L"Primary executable:\n    " << profile.primary_executable << L"\n";
        std::wcout << L"FILESYSTEM\n";
        for (const auto& p : rw) {
            std::wcout << L"    RW  " << p << L"\n";
        }
        for (const auto& p : ro) {
            std::wcout << L"    RO  " << p << L"\n";
        }
        std::wcout << L"    Everything else:  not granted\n\n";
        std::wcout << L"NETWORK\n    Internet:     " << (req.internet_client ? L"ALLOW" : L"not granted")
                   << L"\n    Private LAN:  not granted (UNVERIFIED deny)\n\n";
        std::wcout << L"OS INTEGRATION\n    Clipboard:       UNVERIFIED\n    Camera:          UNVERIFIED\n"
                   << L"    Microphone:      UNVERIFIED\n    Screen capture:  UNVERIFIED\n\n";
        std::wcout << std::flush;
    }

    ContainedRun run = start_contained(req);
    if (!run.ok) {
        return refuse("CREATE_PROCESS_FAILED", run.error);
    }

    std::wstring ferr;
    write_session_file(run, profile, ferr);

    g_stop_event = run.stop_event;
    SetConsoleCtrlHandler(console_ctrl, TRUE);

    if (!json) {
        std::wcout << L"session identity: " << run.session_name << L"\n";
        std::wcout << L"AppContainer SID: " << run.sid_string << L"\n";
        std::wcout << L"Job: " << run.job_name << L"\n";
        std::wcout << L"PID: " << run.pid << L"\n";
        std::wcout << L"selected backend: " << run.backend << L"\n\n" << std::flush;
    } else {
        std::string o = "{\"ok\":true,\"event\":\"started\",\"state\":\"RUNNING\",\"sessionId\":\"";
        o += json_escape(run.session_name);
        o += "\",\"pid\":" + std::to_string(run.pid);
        o += ",\"containedProcessCount\":1,\"externalRelatedProcessCount\":0}";
        emit_json_line(o);
    }

    const bool timed = has_flag(argc, argv, L"--seconds");
    const int seconds = timed ? arg_int(argc, argv, L"--seconds", 20) : 0;
    const ULONGLONG deadline =
        timed ? (GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL) : 0;
    SessionState state = SessionState::Clean;
    bool printed_external = false;
    DWORD observed_contained = 0;

    while (true) {
        const DWORD wait_ms = 1000;
        HANDLE waits[2] = {run.process, run.stop_event};
        const DWORD wr = WaitForMultipleObjects(2, waits, FALSE, wait_ms);
        const std::vector<DWORD> job_ids = job_process_ids(run.job);
        if (job_ids.size() > observed_contained) {
            observed_contained = static_cast<DWORD>(job_ids.size());
        }
        auto related = find_related(profile, run.job, run.sid_string);
        SessionState now = evaluate(related, job_ids, run.sid_string);
        if (now == SessionState::Degraded) {
            state = SessionState::Degraded;
            if (!printed_external) {
                int ext = 0;
                for (const auto& r : related) {
                    if (!r.in_job) {
                        ++ext;
                        if (!json) {
                            std::wcout << L"\nSCYLLA: EXTERNAL RELATED PROCESS DETECTED\n\n";
                            std::wcout << L"Executable:\n    " << r.filename << L"\n";
                            std::wcout << L"PID:\n    " << r.pid << L"\n";
                            std::wcout << L"Contained:\n    NO\n";
                            std::wcout << L"Relationship:\n    same application install root + known executable\n";
                            std::wcout << L"Strict session status:\n    DEGRADED\n" << std::flush;
                        }
                    }
                }
                if (ext > 0) {
                    printed_external = true;
                    if (json) {
                        std::string o = "{\"ok\":true,\"event\":\"degraded\",\"state\":\"DEGRADED\",\"sessionId\":\"";
                        o += json_escape(run.session_name);
                        o += "\",\"pid\":" + std::to_string(run.pid);
                        o += ",\"containedProcessCount\":" + std::to_string(job_ids.size());
                        o += ",\"externalRelatedProcessCount\":" + std::to_string(ext);
                        o += ",\"details\":" + json_related_array(related, true) + "}";
                        emit_json_line(o);
                    }
                }
            }
        }
        if (wr == WAIT_OBJECT_0 || wr == WAIT_OBJECT_0 + 1) {
            break;
        }
        if (timed && GetTickCount64() >= deadline) {
            break;
        }
    }

    std::wstring terr;
    terminate_job(run, terr);
    Sleep(400);

    const auto remaining = find_related(profile, nullptr, run.sid_string);
    DWORD remaining_contained = 0;
    const std::vector<DWORD> leftover_job = job_process_ids(run.job);
    remaining_contained = static_cast<DWORD>(leftover_job.size());

    std::wstring cerr;
    const std::wstring closed_session = run.session_name;
    const bool cleaned = cleanup_contained(run, cerr);
    remove_session_file();
    g_stop_event = nullptr;

    if (!remaining.empty()) {
        state = SessionState::Degraded;
    }
    if (!cleaned) {
        state = SessionState::CleanupRequired;
    }
    const char* final_state = "CLEAN";
    if (state == SessionState::Degraded) {
        final_state = "DEGRADED";
    }
    if (state == SessionState::CleanupRequired) {
        final_state = "CLEANUP_REQUIRED";
    }

    if (json) {
        std::string o = "{\"ok\":";
        o += cleaned ? "true" : "false";
        o += ",\"event\":\"closed\",\"state\":\"";
        o += final_state;
        o += "\",\"code\":\"";
        o += cleaned ? (state == SessionState::Degraded ? "EXTERNAL_RELATED_REMAINING" : "OK") : "CLEANUP_FAILED";
        o += "\",\"sessionId\":\"" + json_escape(closed_session) + "\"";
        o += ",\"containedProcessCount\":" + std::to_string(remaining_contained);
        o += ",\"externalRelatedProcessCount\":" + std::to_string(remaining.size());
        o += ",\"details\":" + json_related_array(remaining, true) + "}";
        emit_json_line(o);
    } else {
        std::wcout << L"\nSCYLLA SESSION CLOSED\n\n";
        std::wcout << L"Application:\n    " << profile.display_name << L"\n";
        std::wcout << L"Contained processes observed (peak job size):\n    " << observed_contained << L"\n";
        std::wcout << L"Contained processes remaining:\n    " << remaining_contained << L"\n";
        std::wcout << L"External related processes:\n";
        if (remaining.empty()) {
            std::wcout << L"    NONE\n";
        } else {
            print_related(remaining, true);
        }
        std::wcout << L"\nFilesystem grants revoked:\n";
        for (const auto& p : rw) {
            std::wcout << L"    RW  " << p << L"\n";
        }
        for (const auto& p : ro) {
            std::wcout << L"    RO  " << p << L"\n";
        }
        std::wcout << L"\nAppContainer:\n    " << (cleaned ? L"REMOVED" : L"CLEANUP FAILED") << L"\n";
        std::wcout << L"Cleanup:\n    " << (cleaned ? L"COMPLETE" : L"FAILED") << L"\n";
        if (!cleaned) {
            std::wcout << L"\n" << cerr << L"\n";
        }
        std::wcout << L"\nFinal state:\n    " << state_text(state) << L"\n";
    }
    if (state == SessionState::CleanupRequired) {
        return 4;
    }
    if (state == SessionState::Degraded) {
        return 1;
    }
    return 0;
}

int cmd_strict_status(int argc, wchar_t** argv) {
    const bool json = has_flag(argc, argv, L"--json");
    const SessionFile s = read_session_file();
    if (!s.present) {
        if (json) {
            emit_json_line(
                "{\"ok\":true,\"state\":\"READY\",\"sessionId\":null,\"pid\":0,\"containedProcessCount\":0,"
                "\"externalRelatedProcessCount\":0}");
        } else {
            std::wcout << L"SCYLLA STRICT STATUS\n\nNo active strict session.\n";
        }
        return 0;
    }
    HANDLE job = nullptr;
    if (!s.job_name.empty()) {
        job = OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, s.job_name.c_str());
    }
    const AppProfile profile = identity_from_session(s);
    const auto related = find_related(profile, job, s.sid);
    const auto job_ids = job ? job_process_ids(job) : std::vector<DWORD>{};
    const SessionState st = evaluate(related, job_ids, s.sid);
    int ext = 0;
    for (const auto& r : related) {
        if (!r.in_job) {
            ++ext;
        }
    }
    const char* state = st == SessionState::Degraded ? "DEGRADED" : "RUNNING";
    if (json) {
        std::string o = "{\"ok\":true,\"state\":\"";
        o += state;
        o += "\",\"sessionId\":\"" + json_escape(s.session_name) + "\"";
        o += ",\"pid\":" + std::to_string(s.pid);
        o += ",\"containedProcessCount\":" + std::to_string(job_ids.size());
        o += ",\"externalRelatedProcessCount\":" + std::to_string(ext);
        o += ",\"details\":" + json_related_array(related, false) + "}";
        emit_json_line(o);
    } else {
        std::wcout << L"SCYLLA STRICT STATUS\n\n";
        std::wcout << L"Session:\n    " << s.session_name << L"\n";
        std::wcout << L"Application:\n    " << profile.display_name << L"\n";
        std::wcout << L"Primary PID:\n    " << s.pid << L"\n";
        std::wcout << L"Job processes:\n    " << job_ids.size() << L"\n";
        for (DWORD pid : job_ids) {
            const AppContainerInfo ac = query_process_appcontainer(pid);
            std::wcout << L"    PID " << pid << L"  AppContainer " << (ac.is_appcontainer ? L"YES" : L"NO") << L"\n";
        }
        std::wcout << L"\nRelated processes:\n";
        print_related(related, false);
        std::wcout << L"\nState:\n    " << (st == SessionState::Degraded ? L"DEGRADED" : L"RUNNING") << L"\n";
    }
    if (job) {
        CloseHandle(job);
    }
    return st == SessionState::Degraded ? 1 : 0;
}

int cmd_strict_processes(int argc, wchar_t** argv) {
    return cmd_strict_status(argc, argv);
}

int cmd_strict_stop(int argc, wchar_t** argv) {
    const bool json = has_flag(argc, argv, L"--json");
    SessionFile s = read_session_file();
    if (!s.present) {
        if (json) {
            emit_json_line("{\"ok\":true,\"state\":\"READY\",\"code\":\"NO_SESSION\",\"message\":\"No active strict session.\"}");
        } else {
            std::wcout << L"SCYLLA STRICT STOP\n\nNo active strict session.\n";
        }
        return 0;
    }
    if (!s.stop_event_name.empty()) {
        HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, s.stop_event_name.c_str());
        if (ev) {
            SetEvent(ev);
            CloseHandle(ev);
            if (!json) {
                std::wcout << L"SCYLLA STRICT STOP\n\nStop signaled for " << s.session_name << L"\n";
            }
        } else if (!json) {
            std::wcout << L"SCYLLA STRICT STOP\n\nStop event not found; attempting job terminate.\n";
        }
    }
    for (int i = 0; i < 20; ++i) {
        Sleep(250);
        if (!read_session_file().present) {
            if (json) {
                emit_json_line(
                    "{\"ok\":true,\"state\":\"CLEAN\",\"code\":\"STOP_SIGNALED\",\"message\":\"Launch process completed teardown.\"}");
            } else {
                std::wcout << L"Launch process completed teardown.\n";
            }
            return 0;
        }
    }
    HANDLE job = nullptr;
    if (!s.job_name.empty()) {
        job = OpenJobObjectW(JOB_OBJECT_TERMINATE | JOB_OBJECT_QUERY, FALSE, s.job_name.c_str());
        if (job) {
            TerminateJobObject(job, 1);
            CloseHandle(job);
        }
    }
    PSID sid = nullptr;
    if (!s.sid.empty()) {
        ConvertStringSidToSidW(s.sid.c_str(), &sid);
    }
    std::wstring dummy;
    if (sid) {
        for (const auto& p : s.grant_rw) {
            revoke_sid_access(p, sid, dummy);
        }
        for (const auto& p : s.grant_ro) {
            revoke_sid_access(p, sid, dummy);
        }
        for (const auto& p : s.grant_rx) {
            revoke_sid_access(p, sid, dummy);
        }
        LocalFree(sid);
    }
    if (!s.session_name.empty()) {
        DeleteAppContainerProfile(s.session_name.c_str());
    }
    remove_session_file();
    if (json) {
        emit_json_line(
            "{\"ok\":false,\"state\":\"CLEANUP_REQUIRED\",\"code\":\"LAUNCH_DID_NOT_EXIT\",\"message\":\"Orphan session artifacts removed.\"}");
    } else {
        std::wcout << L"Orphan session artifacts removed.\nFinal state:\n    CLEANUP_REQUIRED (launch process did not exit)\n";
    }
    return 4;
}

}  // namespace scylla
