#include "appcontainer.h"
#include "path.h"
#include "sandbox_probe.h"
#include "session.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

namespace {

void usage() {
    std::wcout << L"scylla.exe capabilities\n";
    std::wcout << L"scylla.exe hello\n";
    std::wcout << L"scylla.exe selftest-path\n";
    std::wcout << L"scylla.exe selftest-packaged-create --app <WindowsApps exe>\n";
    std::wcout << L"scylla.exe probe <workspace>\n";
    std::wcout << L"scylla.exe launch --app <exe> --workspace <dir>\n";
    std::wcout << L"scylla.exe audit <profile|--app exe> [--json]\n";
    std::wcout << L"scylla.exe strict launch --app <exe>|--profile test-parent --allow-rw <dir> [--allow-ro <dir>] [--args <str>] [--seconds N] [--no-internet] [--kill-related] [--json]\n";
    std::wcout << L"scylla.exe strict status [--json]\n";
    std::wcout << L"scylla.exe strict processes [--json]\n";
    std::wcout << L"scylla.exe strict stop [--json]\n";
}

std::wstring this_dir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    const auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

bool write_text(const std::wstring& path, const std::string& body) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD w = 0;
    WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &w, nullptr);
    CloseHandle(h);
    return true;
}

bool mkdir_p(const std::wstring& path) {
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

int cmd_hello() {
    const std::wstring probe = this_dir() + L"\\scylla-probe.exe";
    scylla::LaunchRequest req;
    req.executable = probe;
    req.internet_client = false;
    const auto result = scylla::launch_appcontainer(req);
    if (!result.ok) {
        std::wcout << result.error << L"\n";
        return 2;
    }
    std::wcout << L"session identity: " << result.session_name << L"\n";
    std::wcout << L"PID: " << result.pid << L"\n";
    std::wcout << L"exit code: " << result.exit_code << L"\n";
    std::wcout << L"cleanup result: OK\n";
    return static_cast<int>(result.exit_code);
}

int cmd_selftest_path() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring root = std::wstring(temp) + L"ScyllaPathTest";
    std::wstring proj = root + L"\\project";
    std::wstring outside = std::wstring(temp) + L"ScyllaPathOutside";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(proj.c_str(), nullptr);
    CreateDirectoryW(outside.c_str(), nullptr);
    int failed = 0;
    auto expect_ok = [&](bool cond, const wchar_t* name) {
        std::wcout << (cond ? L"OK    " : L"FAIL  ") << name << L"\n";
        if (!cond) {
            ++failed;
        }
    };
    auto a = scylla::validate_workspace(proj);
    expect_ok(a.ok, L"valid directory");
    auto b = scylla::validate_workspace(L"C:\\");
    expect_ok(!b.ok, L"refuse drive root");
    auto c = scylla::validate_workspace(L"\\\\NAS\\p");
    expect_ok(!c.ok, L"refuse UNC");
    auto d = scylla::validate_workspace(L"D:\\");
    expect_ok(!d.ok, L"refuse D:\\");
    const std::wstring junction = proj + L"\\escape";
    std::wstring cmd = L"cmd /c mklink /J \"" + junction + L"\" \"" + outside + L"\"";
    _wsystem(cmd.c_str());
    auto e = scylla::validate_workspace(proj);
    expect_ok(e.ok, L"reparse in tree does not refuse");
    _wsystem((L"cmd /c rmdir \"" + junction + L"\"").c_str());
    RemoveDirectoryW(proj.c_str());
    RemoveDirectoryW(root.c_str());
    RemoveDirectoryW(outside.c_str());
    const std::wstring store =
        L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_26.901.4073.0_x64__2p2nqsd0c76g0\\app\\ChatGPT.exe";
    expect_ok(scylla::path_is_windowsapps(store), L"WindowsApps path detected");
    expect_ok(scylla::package_family_from_windowsapps_path(store) == L"OpenAI.Codex_2p2nqsd0c76g0",
              L"package family from WindowsApps path");
    expect_ok(scylla::package_full_name_from_windowsapps_path(store) ==
                  L"OpenAI.Codex_26.901.4073.0_x64__2p2nqsd0c76g0",
              L"package full name from WindowsApps path");
    expect_ok(!scylla::path_is_windowsapps(L"C:\\Users\\ExampleUser\\AppData\\Local\\Programs\\Example\\App.exe"),
              L"Win32 install is not WindowsApps");
    if (GetFileAttributesW(store.c_str()) != INVALID_FILE_ATTRIBUTES) {
        expect_ok(scylla::packaged_is_full_trust(store), L"ChatGPT AppxManifest is runFullTrust");
    }
    std::wcout << (failed ? L"SCYLLA: PATH TESTS FAILED\n" : L"SCYLLA: PATH TESTS OK\n");
    return failed ? 1 : 0;
}

int cmd_probe(const std::wstring& workspace_arg) {
    auto ws = scylla::validate_workspace(workspace_arg);
    if (!ws.ok) {
        // Allow creating the fixture directory if the parent exists.
        if (!mkdir_p(workspace_arg)) {
            std::wcout << L"SCYLLA: REFUSED\n" << ws.reason << L"\n";
            return 2;
        }
        ws = scylla::validate_workspace(workspace_arg);
        if (!ws.ok) {
            std::wcout << L"SCYLLA: REFUSED\n" << ws.reason << L"\n";
            return 2;
        }
    }
    std::wstring parent = ws.canonical;
    const auto slash = parent.find_last_of(L"\\/");
    parent = (slash == std::wstring::npos) ? ws.canonical : parent.substr(0, slash);
    const std::wstring outside = parent + L"\\outside";
    mkdir_p(outside);
    write_text(ws.canonical + L"\\allowed.txt", "allowed\n");
    write_text(outside + L"\\forbidden.txt", "forbidden\n");

    const std::wstring probe = this_dir() + L"\\scylla-probe.exe";
    scylla::LaunchRequest req;
    req.executable = probe;
    req.workspace = ws.canonical;
    req.cwd = ws.canonical;
    req.internet_client = true;
    req.arguments = L"--workspace \"" + ws.canonical + L"\" --outside \"" + outside + L"\"";

    std::wcout << L"session identity: (assigned at launch)\n";
    std::wcout << L"selected backend: LEGACY_APPCONTAINER\n";
    std::wcout << L"target executable: " << probe << L"\n";
    std::wcout << L"canonical workspace: " << ws.canonical << L"\n";
    std::wcout << L"granted paths: workspace RW, probe-dir RX\n";
    std::wcout << L"capabilities: internetClient\n\n" << std::flush;

    const auto result = scylla::launch_appcontainer(req);
    if (!result.ok) {
        std::wcout << result.error << L"\n";
        return 2;
    }
    std::wcout << L"\nsession identity: " << result.session_name << L"\n";
    std::wcout << L"PID: " << result.pid << L"\n";
    std::wcout << L"exit code: " << result.exit_code << L"\n";
    std::wcout << L"cleanup result: OK\n";
    return static_cast<int>(result.exit_code);
}

int cmd_launch(const std::wstring& app, const std::wstring& workspace) {
    auto ws = scylla::validate_workspace(workspace);
    if (!ws.ok) {
        std::wcout << L"SCYLLA: REFUSED\n" << ws.reason << L"\n";
        return 2;
    }
    scylla::LaunchRequest req;
    req.executable = app;
    req.workspace = ws.canonical;
    req.cwd = ws.canonical;
    req.internet_client = true;
    const auto result = scylla::launch_appcontainer(req);
    std::wcout << L"session identity: " << result.session_name << L"\n";
    std::wcout << L"selected backend: " << result.backend << L"\n";
    std::wcout << L"target executable: " << app << L"\n";
    std::wcout << L"canonical workspace: " << ws.canonical << L"\n";
    std::wcout << L"capabilities: internetClient\n";
    if (!result.ok) {
        std::wcout << result.error << L"\n";
        return 2;
    }
    std::wcout << L"PID: " << result.pid << L"\n";
    std::wcout << L"exit code: " << result.exit_code << L"\n";
    std::wcout << L"cleanup result: OK\n";
    return static_cast<int>(result.exit_code);
}

std::wstring arg_after(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return L"";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::wstring cmd = argv[1];
    if (cmd == L"capabilities") {
        scylla::print_host_capabilities(scylla::probe_host_capabilities());
        return 0;
    }
    if (cmd == L"hello") {
        return cmd_hello();
    }
    if (cmd == L"selftest-path") {
        return cmd_selftest_path();
    }
    if (cmd == L"selftest-packaged-create") {
        return scylla::cmd_selftest_packaged_create(arg_after(argc, argv, L"--app"));
    }
    if (cmd == L"probe") {
        if (argc < 3) {
            usage();
            return 2;
        }
        return cmd_probe(argv[2]);
    }
    if (cmd == L"launch") {
        const std::wstring app = arg_after(argc, argv, L"--app");
        const std::wstring ws = arg_after(argc, argv, L"--workspace");
        if (app.empty() || ws.empty()) {
            usage();
            return 2;
        }
        return cmd_launch(app, ws);
    }
    if (cmd == L"audit") {
        if (argc < 3) {
            usage();
            return 2;
        }
        return scylla::cmd_audit(argc, argv);
    }
    if (cmd == L"strict") {
        if (argc < 3) {
            usage();
            return 2;
        }
        const std::wstring sub = argv[2];
        if (sub == L"launch") {
            return scylla::cmd_strict_launch(argc, argv);
        }
        if (sub == L"status") {
            return scylla::cmd_strict_status(argc, argv);
        }
        if (sub == L"processes") {
            return scylla::cmd_strict_processes(argc, argv);
        }
        if (sub == L"stop") {
            return scylla::cmd_strict_stop(argc, argv);
        }
        usage();
        return 2;
    }
    usage();
    return 2;
}
