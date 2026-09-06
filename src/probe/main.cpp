#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring env(const wchar_t* name) {
    wchar_t buf[32768]{};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) {
        return L"";
    }
    return buf;
}

bool token_is_appcontainer() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        return false;
    }
    DWORD is_ac = 0;
    DWORD ret = 0;
    const BOOL ok = GetTokenInformation(tok, TokenIsAppContainer, &is_ac, sizeof(is_ac), &ret);
    CloseHandle(tok);
    return ok && is_ac != 0;
}

bool can_read(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(h);
    return true;
}

bool can_write_new(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(h);
    return true;
}

bool https_example() {
    HINTERNET s = WinHttpOpen(L"scylla-probe/0.0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        return false;
    }
    HINTERNET c = WinHttpConnect(s, L"example.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) {
        WinHttpCloseHandle(s);
        return false;
    }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", L"/", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    BOOL sent = FALSE;
    if (r) {
        sent = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (sent) {
            sent = WinHttpReceiveResponse(r, nullptr);
        }
        WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return sent == TRUE;
}

std::wstring arg_value(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return L"";
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

void print_access(const wchar_t* label, bool ok, bool expect_allow) {
    std::wcout << label;
    if (expect_allow) {
        std::wcout << (ok ? L"PASS\n" : L"FAIL\n");
    } else {
        std::wcout << (ok ? L"LEAK\n" : L"DENIED\n");
    }
}

int run_child_self(const std::wstring& workspace, const std::wstring& outside) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" --child --workspace \"";
    cmd += workspace;
    cmd += L"\" --outside \"";
    cmd += outside;
    cmd += L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, workspace.c_str(), &si, &pi)) {
        std::wcout << L"Child launch: FAIL " << GetLastError() << L"\n";
        return 2;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::wcout << L"Child exit: " << code << L"\n";
    return static_cast<int>(code);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const bool child = has_flag(argc, argv, L"--child");
    const std::wstring workspace = arg_value(argc, argv, L"--workspace");
    const std::wstring outside = arg_value(argc, argv, L"--outside");

    std::wcout << L"SCYLLA PROBE\n\n";
    std::wcout << L"AppContainer: " << (token_is_appcontainer() ? L"YES" : L"NO") << L"\n";
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wcout << L"executable: " << exe << L"\n";
    std::wcout << L"LOCALAPPDATA: " << env(L"LOCALAPPDATA") << L"\n";
    std::wcout << L"TEMP: " << env(L"TEMP") << L"\n";
    std::wcout << L"TMP: " << env(L"TMP") << L"\n";

    if (workspace.empty()) {
        return token_is_appcontainer() ? 0 : 3;
    }

    const std::wstring allowed = workspace + L"\\allowed.txt";
    const std::wstring created = workspace + L"\\created.txt";
    const std::wstring forbidden = outside + L"\\forbidden.txt";

    std::wcout << L"Workspace read: ";
    print_access(L"", can_read(allowed), true);
    std::wcout << L"Workspace write: ";
    print_access(L"", can_write_new(created), true);
    if (!outside.empty()) {
        std::wcout << L"Outside read: ";
        print_access(L"", can_read(forbidden), false);
    }

    const std::wstring profile = env(L"USERPROFILE");
    if (!profile.empty()) {
        std::wcout << L"Documents read: ";
        print_access(L"", can_read(profile + L"\\Documents"), false);
        std::wcout << L"Downloads read: ";
        print_access(L"", can_read(profile + L"\\Downloads"), false);
        std::wcout << L"Desktop read: ";
        print_access(L"", can_read(profile + L"\\Desktop"), false);
        std::wcout << L".ssh read: ";
        print_access(L"", can_read(profile + L"\\.ssh"), false);
    }

    std::wcout << L"Internet: " << (https_example() ? L"PASS\n" : L"FAIL\n");

    if (!child && !workspace.empty()) {
        std::wcout << L"--- child ---\n" << std::flush;
        return run_child_self(workspace, outside);
    }
    return token_is_appcontainer() ? 0 : 3;
}
