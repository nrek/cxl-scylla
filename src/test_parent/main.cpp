#include <windows.h>
#include <string>
#include <vector>
#include <iostream>

namespace {

int arg_int(int argc, wchar_t** argv, const wchar_t* key, int fallback) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::wstring(argv[i]) == key) {
            return _wtoi(argv[i + 1]);
        }
    }
    return fallback;
}

bool has_flag(int argc, wchar_t** argv, const wchar_t* key) {
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == key) {
            return true;
        }
    }
    return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const int seconds = arg_int(argc, argv, L"--seconds", 20);
    const bool orphan = has_flag(argc, argv, L"--orphan");
    const bool child = has_flag(argc, argv, L"--child");
    const DWORD pid = GetCurrentProcessId();

    if (orphan) {
        std::wcout << L"scylla-test-parent orphan PID " << pid
                   << L" (unsandboxed related process)\n"
                   << std::flush;
        Sleep(static_cast<DWORD>(seconds) * 1000);
        return 0;
    }

    if (!child) {
        wchar_t self[MAX_PATH]{};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = std::wstring(L"\"") + self + L"\" --child --seconds " + std::to_wstring(seconds);
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(self, buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si,
                             &pi)) {
            std::wcout << L"scylla-test-parent failed to spawn child: " << GetLastError() << L"\n";
            return 1;
        }
        std::wcout << L"scylla-test-parent parent PID " << pid << L" spawned child PID " << pi.dwProcessId << L"\n"
                   << std::flush;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        std::wcout << L"scylla-test-parent child PID " << pid << L"\n" << std::flush;
    }

    Sleep(static_cast<DWORD>(seconds) * 1000);
    return 0;
}
