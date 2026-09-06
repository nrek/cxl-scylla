#include "scyllagpt/runtime.h"

#include "scyllagpt/json.h"
#include "scyllagpt/lockdown.h"
#include "scyllagpt/utf.h"

#include <vector>

namespace scyllagpt {
namespace {

void close_handle(HANDLE& h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        h = nullptr;
    }
}

std::wstring env_block_with_codex_home(const std::wstring& codex_home) {
    LPWCH env = GetEnvironmentStringsW();
    if (!env) {
        return L"";
    }
    std::wstring out;
    bool replaced = false;
    for (wchar_t* p = env; *p;) {
        std::wstring entry = p;
        p += entry.size() + 1;
        if (entry.rfind(L"CODEX_HOME=", 0) == 0) {
            out += L"CODEX_HOME=" + codex_home;
            out.push_back(L'\0');
            replaced = true;
        } else {
            out += entry;
            out.push_back(L'\0');
        }
    }
    FreeEnvironmentStringsW(env);
    if (!replaced) {
        out += L"CODEX_HOME=" + codex_home;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

bool dir_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

Runtime::~Runtime() {
    stop();
}

bool Runtime::start(const std::wstring& exe, const std::wstring& codex_home, const std::wstring& workspace,
                    const std::wstring& stderr_log, HWND notify, UINT msg, bool allow_shell, std::wstring* error) {
    stop();
    notify_ = notify;
    notify_msg_ = msg;

    if (workspace.empty() || !dir_exists(workspace)) {
        if (error) {
            *error = L"Scylla workspace is missing";
        }
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_rd = nullptr;
    HANDLE stdout_wr = nullptr;
    if (!CreatePipe(&stdin_rd, &stdin_wr_, &sa, 0) || !CreatePipe(&stdout_rd_, &stdout_wr, &sa, 0)) {
        if (error) {
            *error = L"CreatePipe failed";
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        return false;
    }
    SetHandleInformation(stdin_wr_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd_, HANDLE_FLAG_INHERIT, 0);

    HANDLE stderr_wr = CreateFileW(stderr_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderr_wr == INVALID_HANDLE_VALUE) {
        stderr_wr = stdout_wr;
    }

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) {
        if (error) {
            *error = L"CreateJobObject failed";
        }
        CloseHandle(stdin_rd);
        CloseHandle(stdout_wr);
        if (stderr_wr != stdout_wr) {
            CloseHandle(stderr_wr);
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        if (error) {
            *error = L"SetInformationJobObject failed";
        }
        CloseHandle(stdin_rd);
        CloseHandle(stdout_wr);
        if (stderr_wr != stdout_wr && stderr_wr != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_wr);
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        close_handle(job_);
        return false;
    }

    HANDLE inherit[3]{};
    DWORD inherit_n = 0;
    inherit[inherit_n++] = stdin_rd;
    inherit[inherit_n++] = stdout_wr;
    if (stderr_wr != stdout_wr && stderr_wr != INVALID_HANDLE_VALUE) {
        inherit[inherit_n++] = stderr_wr;
    }

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    auto* attr = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attr_size));
    if (!attr || !InitializeProcThreadAttributeList(attr, 1, 0, &attr_size)) {
        if (error) {
            *error = L"InitializeProcThreadAttributeList failed";
        }
        if (attr) {
            HeapFree(GetProcessHeap(), 0, attr);
        }
        CloseHandle(stdin_rd);
        CloseHandle(stdout_wr);
        if (stderr_wr != stdout_wr && stderr_wr != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_wr);
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        close_handle(job_);
        return false;
    }
    if (!UpdateProcThreadAttribute(attr, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                   inherit_n * sizeof(HANDLE), nullptr, nullptr)) {
        if (error) {
            *error = L"UpdateProcThreadAttribute HANDLE_LIST failed";
        }
        DeleteProcThreadAttributeList(attr);
        HeapFree(GetProcessHeap(), 0, attr);
        CloseHandle(stdin_rd);
        CloseHandle(stdout_wr);
        if (stderr_wr != stdout_wr && stderr_wr != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_wr);
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        close_handle(job_);
        return false;
    }

    std::wstring cmd = L"\"" + exe + L"\" app-server " + app_server_disable_args(allow_shell);
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);

    std::wstring env = env_block_with_codex_home(codex_home);

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = stdin_rd;
    siex.StartupInfo.hStdOutput = stdout_wr;
    siex.StartupInfo.hStdError = stderr_wr;
    siex.lpAttributeList = attr;

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        exe.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        env.data(), workspace.c_str(), &siex.StartupInfo, &pi);
    const DWORD create_err = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attr);
    HeapFree(GetProcessHeap(), 0, attr);
    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);
    if (stderr_wr != stdout_wr && stderr_wr != INVALID_HANDLE_VALUE) {
        CloseHandle(stderr_wr);
    }
    if (!ok) {
        if (error) {
            *error = L"CreateProcess failed (" + std::to_wstring(create_err) + L")";
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        close_handle(job_);
        return false;
    }
    if (!AssignProcessToJobObject(job_, pi.hProcess)) {
        const DWORD job_err = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (error) {
            *error = L"AssignProcessToJobObject failed (" + std::to_wstring(job_err) + L")";
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        close_handle(job_);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    stop_ = 0;
    reader_ = CreateThread(nullptr, 0, reader_proc, this, 0, nullptr);
    return true;
}

void Runtime::stop() {
    InterlockedExchange(&stop_, 1);
    close_handle(stdin_wr_);
    if (process_) {
        TerminateProcess(process_, 1);
    }
    if (reader_) {
        WaitForSingleObject(reader_, 4000);
        close_handle(reader_);
    }
    close_handle(stdout_rd_);
    close_handle(process_);
    close_handle(job_);
    pid_ = 0;
}

bool Runtime::write_line(const std::string& jsonl) {
    if (!stdin_wr_) {
        return false;
    }
    std::string line = jsonl;
    if (line.empty() || line.back() != '\n') {
        line.push_back('\n');
    }
    DWORD wr = 0;
    return WriteFile(stdin_wr_, line.data(), static_cast<DWORD>(line.size()), &wr, nullptr) != 0;
}

DWORD WINAPI Runtime::reader_proc(LPVOID self) {
    static_cast<Runtime*>(self)->reader_loop();
    return 0;
}

void Runtime::reader_loop() {
    JsonlDecoder dec;
    char buf[4096];
    while (!stop_) {
        DWORD rd = 0;
        if (!ReadFile(stdout_rd_, buf, sizeof(buf), &rd, nullptr) || rd == 0) {
            break;
        }
        std::vector<std::string> lines;
        std::string err;
        if (!dec.feed(buf, rd, lines, &err)) {
            break;
        }
        for (auto& line : lines) {
            if (!notify_) {
                continue;
            }
            auto* heap = new std::string(std::move(line));
            if (!PostMessageW(notify_, notify_msg_, 0, reinterpret_cast<LPARAM>(heap))) {
                delete heap;
            }
        }
    }
}

}  // namespace scyllagpt
