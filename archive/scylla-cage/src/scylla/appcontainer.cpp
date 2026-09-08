#include "appcontainer.h"

#include "acl.h"
#include "path.h"
#include "process_scan.h"

#include <userenv.h>
#include <sddl.h>
#include <rpc.h>
#include <vector>
#include <iostream>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

namespace scylla {
namespace {

std::wstring make_session_name() {
    UUID uuid{};
    UuidCreate(&uuid);
    RPC_WSTR str = nullptr;
    UuidToStringW(&uuid, &str);
    std::wstring hex;
    if (str) {
        hex = reinterpret_cast<wchar_t*>(str);
        RpcStringFreeW(&str);
    }
    std::wstring compact;
    for (wchar_t ch : hex) {
        if ((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F')) {
            compact.push_back(ch);
        }
        if (compact.size() >= 16) {
            break;
        }
    }
    return L"Scylla.S." + compact;
}

std::wstring compact_id(const std::wstring& session_name) {
    const std::wstring prefix = L"Scylla.S.";
    if (session_name.rfind(prefix, 0) == 0) {
        return session_name.substr(prefix.size());
    }
    return session_name;
}

void delete_profile(const std::wstring& name) {
    DeleteAppContainerProfile(name.c_str());
}

bool add_internet_capability(std::vector<SID_AND_ATTRIBUTES>& caps, std::vector<BYTE>& sid_buf, std::wstring& err) {
    sid_buf.resize(SECURITY_MAX_SID_SIZE);
    DWORD sid_size = SECURITY_MAX_SID_SIZE;
    if (!CreateWellKnownSid(WinCapabilityInternetClientSid, nullptr, sid_buf.data(), &sid_size)) {
        err = L"CreateWellKnownSid(internetClient): " + win32_error(GetLastError());
        return false;
    }
    SID_AND_ATTRIBUTES sa{};
    sa.Sid = sid_buf.data();
    sa.Attributes = SE_GROUP_ENABLED;
    caps.push_back(sa);
    return true;
}

bool already_listed(const std::vector<std::wstring>& paths, const std::wstring& p) {
    const std::wstring key = path_lower(p);
    for (const auto& x : paths) {
        if (path_lower(x) == key) {
            return true;
        }
    }
    return false;
}

void close_run_handles(ContainedRun& run) {
    if (run.thread) {
        CloseHandle(run.thread);
        run.thread = nullptr;
    }
    if (run.process) {
        CloseHandle(run.process);
        run.process = nullptr;
    }
    if (run.stop_event) {
        CloseHandle(run.stop_event);
        run.stop_event = nullptr;
    }
    if (run.job) {
        CloseHandle(run.job);
        run.job = nullptr;
    }
}

void fail_start(ContainedRun& run, const std::wstring& msg) {
    std::wstring dummy;
    for (const auto& p : run.granted_rw) {
        if (run.ac_sid) {
            revoke_sid_access(p, run.ac_sid, dummy);
        }
    }
    for (const auto& p : run.granted_ro) {
        if (run.ac_sid) {
            revoke_sid_access(p, run.ac_sid, dummy);
        }
    }
    for (const auto& p : run.granted_rx) {
        if (run.ac_sid) {
            revoke_sid_access(p, run.ac_sid, dummy);
        }
    }
    if (run.process) {
        TerminateProcess(run.process, 1);
    }
    close_run_handles(run);
    if (run.ac_sid) {
        FreeSid(run.ac_sid);
        run.ac_sid = nullptr;
    }
    if (run.owns_appcontainer_profile && !run.session_name.empty()) {
        delete_profile(run.session_name);
    }
    run.ok = false;
    run.error = msg;
}

bool apply_job_limits(HANDLE job, std::wstring& err) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli))) {
        err = L"SetInformationJobObject: " + win32_error(GetLastError());
        return false;
    }
    return true;
}

DWORD create_ac_process(const std::wstring& exe, const std::wstring& cwd, const std::wstring& arguments, PSID ac_sid,
                       SID_AND_ATTRIBUTES* caps, DWORD cap_count, bool use_app_name, DWORD extra_flags,
                       PROCESS_INFORMATION& pi) {
    ZeroMemory(&pi, sizeof(pi));
    SECURITY_CAPABILITIES sc{};
    sc.AppContainerSid = ac_sid;
    sc.Capabilities = cap_count ? caps : nullptr;
    sc.CapabilityCount = cap_count;

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<BYTE> attr_buf(attr_size);
    auto* attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
        return GetLastError();
    }
    if (!UpdateProcThreadAttribute(
            attr_list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &sc, sizeof(sc), nullptr, nullptr)) {
        const DWORD e = GetLastError();
        DeleteProcThreadAttributeList(attr_list);
        return e;
    }

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.lpAttributeList = attr_list;

    std::wstring cmd = L"\"" + exe + L"\"";
    if (!arguments.empty()) {
        cmd += L" ";
        cmd += arguments;
    }
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);

    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | extra_flags;
    const BOOL ok = CreateProcessW(
        use_app_name ? exe.c_str() : nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        FALSE,
        flags,
        nullptr,
        cwd.empty() ? nullptr : cwd.c_str(),
        &siex.StartupInfo,
        &pi);
    const DWORD e = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attr_list);
    return e;
}

bool validate_dir_list(const std::vector<std::wstring>& in, std::vector<std::wstring>& out, std::wstring& err) {
    for (const auto& p : in) {
        if (p.empty()) {
            continue;
        }
        const PathCheck chk = validate_workspace(p);
        if (!chk.ok) {
            err = chk.reason;
            return false;
        }
        if (!already_listed(out, chk.canonical)) {
            out.push_back(chk.canonical);
        }
    }
    return true;
}

}  // namespace

ContainedRun start_contained(const LaunchRequest& req) {
    ContainedRun run;
    run.backend = L"LEGACY_APPCONTAINER";
    run.session_name = make_session_name();
    const std::wstring id = compact_id(run.session_name);
    const bool packaged = path_is_windowsapps(req.executable);
    if (packaged && packaged_is_full_trust(req.executable)) {
        run.backend = L"PACKAGED_FULLTRUST_REFUSED";
        run.error =
            L"SCYLLA: REFUSED\nthis packaged app declares runFullTrust. Windows rejects AppContainer CreateProcess "
            L"(win32 87). Scylla will not ActivateApplication (that launch is unsandboxed).";
        return run;
    }

    HRESULT hr = S_OK;
    if (packaged) {
        run.owns_appcontainer_profile = false;
        run.backend = L"PACKAGED_CREATEPROCESS";
        const std::wstring family = package_family_from_windowsapps_path(req.executable);
        if (family.empty()) {
            run.error = L"SCYLLA: REFUSED\ncould not parse package family from WindowsApps path";
            return run;
        }
        hr = DeriveAppContainerSidFromAppContainerName(family.c_str(), &run.ac_sid);
        if (FAILED(hr) || run.ac_sid == nullptr) {
            run.error = L"SCYLLA: REFUSED\ncould not derive package AppContainer SID for " + family;
            return run;
        }
    } else {
        hr = CreateAppContainerProfile(
            run.session_name.c_str(), run.session_name.c_str(), L"Scylla v0.0.1 session", nullptr, 0, &run.ac_sid);
        if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            delete_profile(run.session_name);
            run.ac_sid = nullptr;
            hr = CreateAppContainerProfile(
                run.session_name.c_str(), run.session_name.c_str(), L"Scylla v0.0.1 session", nullptr, 0, &run.ac_sid);
        }
        if (FAILED(hr)) {
            run.error = L"SCYLLA: REFUSED\nCreateAppContainerProfile failed HRESULT=" +
                        std::to_wstring(static_cast<unsigned long>(hr));
            return run;
        }
        if (run.ac_sid == nullptr) {
            hr = DeriveAppContainerSidFromAppContainerName(run.session_name.c_str(), &run.ac_sid);
            if (FAILED(hr) || run.ac_sid == nullptr) {
                delete_profile(run.session_name);
                run.error = L"SCYLLA: REFUSED\nDeriveAppContainerSidFromAppContainerName failed";
                return run;
            }
        }
    }

    LPWSTR sid_str = nullptr;
    if (ConvertSidToStringSidW(run.ac_sid, &sid_str)) {
        run.sid_string = sid_str;
        LocalFree(sid_str);
    }

    std::vector<std::wstring> rw_in = req.extra_rw_paths;
    if (!req.workspace.empty()) {
        rw_in.insert(rw_in.begin(), req.workspace);
    }
    std::wstring verr;
    if (!validate_dir_list(rw_in, run.granted_rw, verr)) {
        fail_start(run, L"SCYLLA: REFUSED\n" + verr);
        return run;
    }
    if (!validate_dir_list(req.extra_ro_paths, run.granted_ro, verr)) {
        fail_start(run, L"SCYLLA: REFUSED\n" + verr);
        return run;
    }
    for (const auto& extra : req.extra_rx_paths) {
        if (path_is_windowsapps(extra)) {
            continue;
        }
        const PathCheck chk = validate_workspace(extra);
        if (!chk.ok) {
            fail_start(run, L"SCYLLA: REFUSED\n" + chk.reason);
            return run;
        }
        if (!already_listed(run.granted_rx, chk.canonical) && !already_listed(run.granted_rw, chk.canonical)) {
            run.granted_rx.push_back(chk.canonical);
        }
    }

    const DWORD exe_attr = GetFileAttributesW(req.executable.c_str());
    const bool exe_ok = exe_attr != INVALID_FILE_ATTRIBUTES && (exe_attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    if (!exe_ok && !packaged) {
        fail_start(run, L"SCYLLA: REFUSED\nexecutable not found: " + req.executable);
        return run;
    }

    const std::wstring exe_dir = path_dirname(req.executable);
    if (!path_is_windowsapps(exe_dir) && !already_listed(run.granted_rx, exe_dir) &&
        !already_listed(run.granted_rw, exe_dir)) {
        run.granted_rx.push_back(exe_dir);
    }

    const DWORD rw = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
    const DWORD ro = FILE_GENERIC_READ | FILE_LIST_DIRECTORY;
    const DWORD rx = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;

    std::wstring err;
    for (const auto& p : run.granted_rw) {
        if (path_is_windowsapps(p)) {
            fail_start(run, L"SCYLLA: REFUSED\nWindowsApps ACL is forbidden: " + p);
            return run;
        }
        if (!grant_sid_access(p, run.ac_sid, rw, err)) {
            fail_start(run, L"SCYLLA: REFUSED\nrequired ACL cannot be applied to RW path: " + p + L" " + err);
            return run;
        }
    }
    for (const auto& p : run.granted_ro) {
        if (path_is_windowsapps(p)) {
            fail_start(run, L"SCYLLA: REFUSED\nWindowsApps ACL is forbidden: " + p);
            return run;
        }
        if (!grant_sid_access(p, run.ac_sid, ro, err)) {
            fail_start(run, L"SCYLLA: REFUSED\nrequired ACL cannot be applied to RO path: " + p + L" " + err);
            return run;
        }
    }
    for (const auto& p : run.granted_rx) {
        if (path_is_windowsapps(p)) {
            fail_start(run, L"SCYLLA: REFUSED\nWindowsApps ACL is forbidden: " + p);
            return run;
        }
        if (!grant_sid_access(p, run.ac_sid, rx, err)) {
            fail_start(run, L"SCYLLA: REFUSED\nrequired ACL cannot be applied to RX path: " + p + L" " + err);
            return run;
        }
    }

    LPCWSTR job_name = nullptr;
    std::wstring job_storage;
    if (req.named_lifecycle) {
        job_storage = L"ScyllaJob." + id;
        run.job_name = job_storage;
        job_name = job_storage.c_str();
        run.stop_event_name = L"Local\\ScyllaStop." + id;
        run.stop_event = CreateEventW(nullptr, TRUE, FALSE, run.stop_event_name.c_str());
        if (!run.stop_event) {
            fail_start(run, L"SCYLLA: REFUSED\nCreateEvent: " + win32_error(GetLastError()));
            return run;
        }
    }

    run.job = CreateJobObjectW(nullptr, job_name);
    if (!run.job) {
        fail_start(run, L"SCYLLA: REFUSED\nCreateJobObject: " + win32_error(GetLastError()));
        return run;
    }
    if (!apply_job_limits(run.job, err)) {
        fail_start(run, L"SCYLLA: REFUSED\n" + err);
        return run;
    }

    std::vector<BYTE> cap_sid;
    std::vector<SID_AND_ATTRIBUTES> caps;
    if (req.internet_client) {
        if (!add_internet_capability(caps, cap_sid, err)) {
            fail_start(run, L"SCYLLA: REFUSED\n" + err);
            return run;
        }
    }

    std::wstring cwd = req.cwd;
    if (cwd.empty()) {
        cwd = run.granted_rw.empty() ? exe_dir : run.granted_rw.front();
    }
    if (packaged && path_is_windowsapps(cwd)) {
        cwd = run.granted_rw.empty() ? L"" : run.granted_rw.front();
    }
    if (packaged && (cwd.empty() || path_is_windowsapps(cwd))) {
        fail_start(run, L"SCYLLA: REFUSED\npackaged launch requires a non-WindowsApps --allow-rw as cwd");
        return run;
    }

    PROCESS_INFORMATION pi{};
    const DWORD created_err = create_ac_process(
        req.executable, cwd, req.arguments, run.ac_sid, caps.empty() ? nullptr : caps.data(),
        static_cast<DWORD>(caps.size()), true, CREATE_UNICODE_ENVIRONMENT, pi);

    if (created_err != 0) {
        std::wstring msg = L"SCYLLA: REFUSED\nsandbox CreateProcess failed; target was not launched unsandboxed. " +
                           win32_error(created_err);
        if (packaged) {
            msg += L" This Store app is runFullTrust. Windows rejected AppContainer CreateProcess; Scylla will not ActivateApplication.";
        }
        fail_start(run, msg);
        return run;
    }

    run.process = pi.hProcess;
    run.thread = pi.hThread;
    run.pid = pi.dwProcessId;

    if (!AssignProcessToJobObject(run.job, run.process)) {
        fail_start(run, L"SCYLLA: REFUSED\nAssignProcessToJobObject: " + win32_error(GetLastError()));
        return run;
    }
    if (ResumeThread(run.thread) == static_cast<DWORD>(-1)) {
        fail_start(run, L"SCYLLA: REFUSED\nResumeThread: " + win32_error(GetLastError()));
        return run;
    }

    Sleep(50);
    const AppContainerInfo ac = query_process_appcontainer(run.pid);
    if (!ac.queried || !ac.is_appcontainer) {
        fail_start(run, L"SCYLLA: REFUSED\nprimary process is not AppContainer after launch");
        return run;
    }
    if (!run.sid_string.empty() && !ac.sid.empty() && path_lower(ac.sid) != path_lower(run.sid_string)) {
        fail_start(run, L"SCYLLA: REFUSED\nAppContainer SID mismatch on primary process");
        return run;
    }

    run.ok = true;
    return run;
}

bool terminate_job(ContainedRun& run, std::wstring& err) {
    if (!run.job) {
        return true;
    }
    if (!TerminateJobObject(run.job, 1)) {
        const DWORD e = GetLastError();
        if (e != ERROR_ACCESS_DENIED) {
            err = L"TerminateJobObject: " + win32_error(e);
            return false;
        }
    }
    if (run.process) {
        WaitForSingleObject(run.process, 5000);
    }
    return true;
}

bool cleanup_contained(ContainedRun& run, std::wstring& err) {
    bool ok = true;
    std::wstring msg;
    for (const auto& p : run.granted_rw) {
        std::wstring rerr;
        if (run.ac_sid && !revoke_sid_access(p, run.ac_sid, rerr)) {
            ok = false;
            msg += L"revoke RW " + p + L": " + rerr + L"; ";
        }
    }
    for (const auto& p : run.granted_ro) {
        std::wstring rerr;
        if (run.ac_sid && !revoke_sid_access(p, run.ac_sid, rerr)) {
            ok = false;
            msg += L"revoke RO " + p + L": " + rerr + L"; ";
        }
    }
    for (const auto& p : run.granted_rx) {
        std::wstring rerr;
        if (run.ac_sid && !revoke_sid_access(p, run.ac_sid, rerr)) {
            ok = false;
            msg += L"revoke RX " + p + L": " + rerr + L"; ";
        }
    }
    close_run_handles(run);
    if (run.ac_sid) {
        FreeSid(run.ac_sid);
        run.ac_sid = nullptr;
    }
    if (run.owns_appcontainer_profile && !run.session_name.empty()) {
        const HRESULT hr = DeleteAppContainerProfile(run.session_name.c_str());
        if (FAILED(hr)) {
            ok = false;
            msg += L"DeleteAppContainerProfile HRESULT=" + std::to_wstring(static_cast<unsigned long>(hr));
        }
        run.session_name.clear();
    }
    if (!ok) {
        err = L"SCYLLA: CLEANUP FAILED\n" + msg + L" (stale access grant is a security event)";
    }
    return ok;
}

LaunchResult launch_appcontainer(const LaunchRequest& req) {
    LaunchResult out;
    ContainedRun run = start_contained(req);
    out.session_name = run.session_name;
    out.backend = run.backend;
    out.pid = run.pid;
    if (!run.ok) {
        out.error = run.error;
        return out;
    }
    WaitForSingleObject(run.process, INFINITE);
    GetExitCodeProcess(run.process, &out.exit_code);
    std::wstring err;
    if (!cleanup_contained(run, err)) {
        out.ok = false;
        out.error = err;
        return out;
    }
    out.ok = true;
    return out;
}

int cmd_selftest_packaged_create(const std::wstring& exe) {
    if (exe.empty() || !path_is_windowsapps(exe)) {
        std::wcout << L"SCYLLA: selftest-packaged-create requires --app <WindowsApps exe>\n";
        return 2;
    }
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring cwd = temp;
    if (!cwd.empty() && (cwd.back() == L'\\' || cwd.back() == L'/')) {
        cwd.pop_back();
    }

    const std::wstring family = package_family_from_windowsapps_path(exe);
    PSID pkg_sid = nullptr;
    HRESULT hr = DeriveAppContainerSidFromAppContainerName(family.c_str(), &pkg_sid);
    if (FAILED(hr) || pkg_sid == nullptr) {
        std::wcout << L"FAIL derive package SID\n";
        return 1;
    }

    const std::wstring sess = make_session_name();
    PSID scylla_sid = nullptr;
    hr = CreateAppContainerProfile(sess.c_str(), sess.c_str(), L"Scylla packaged probe", nullptr, 0, &scylla_sid);
    if (FAILED(hr) || scylla_sid == nullptr) {
        std::wcout << L"FAIL CreateAppContainerProfile HRESULT=" << static_cast<unsigned long>(hr) << L"\n";
        FreeSid(pkg_sid);
        return 1;
    }

    std::vector<BYTE> cap_sid;
    std::vector<SID_AND_ATTRIBUTES> caps;
    std::wstring cap_err;
    add_internet_capability(caps, cap_sid, cap_err);

    auto run_one = [&](const wchar_t* sid_name, PSID sid, bool use_app, DWORD extra, bool with_caps) {
        PROCESS_INFORMATION pi{};
        SID_AND_ATTRIBUTES* cptr = nullptr;
        DWORD cc = 0;
        if (with_caps && !caps.empty()) {
            cptr = caps.data();
            cc = static_cast<DWORD>(caps.size());
        }
        const DWORD e = create_ac_process(exe, cwd, L"", sid, cptr, cc, use_app, extra, pi);
        std::wcout << sid_name << L" appName=" << (use_app ? L"1" : L"0") << L" extra=" << extra
                   << L" caps=" << (with_caps ? L"1" : L"0") << L" err=" << e;
        if (e == 0) {
            const AppContainerInfo ac = query_process_appcontainer(pi.dwProcessId);
            std::wcout << L" pid=" << pi.dwProcessId << L" queried=" << (ac.queried ? L"1" : L"0")
                       << L" isAC=" << (ac.is_appcontainer ? L"1" : L"0");
            if (pi.hProcess) {
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 2000);
            }
            if (pi.hThread) {
                CloseHandle(pi.hThread);
            }
            if (pi.hProcess) {
                CloseHandle(pi.hProcess);
            }
        }
        std::wcout << L"\n";
    };

    const DWORD extras[] = {CREATE_UNICODE_ENVIRONMENT, 0};
    const bool apps[] = {true, false};
    const bool capflags[] = {true, false};
    std::wcout << L"exe=" << exe << L"\ncwd=" << cwd << L"\nfamily=" << family << L"\n";
    for (PSID sid : {pkg_sid, scylla_sid}) {
        const wchar_t* name = (sid == pkg_sid) ? L"packageSID" : L"scyllaSID";
        for (bool app : apps) {
            for (DWORD extra : extras) {
                for (bool with_caps : capflags) {
                    run_one(name, sid, app, extra, with_caps);
                }
            }
        }
    }

    DeleteAppContainerProfile(sess.c_str());
    if (pkg_sid) {
        FreeSid(pkg_sid);
    }
    if (scylla_sid) {
        FreeSid(scylla_sid);
    }
    return 0;
}

}  // namespace scylla
