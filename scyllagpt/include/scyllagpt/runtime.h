#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

struct RuntimeStatus {
    bool running = false;
    DWORD pid = 0;
    std::wstring error;
};

class Runtime {
public:
    using LineFn = std::function<void(std::string line)>;

    ~Runtime();

    bool start(const std::wstring& exe, const std::wstring& codex_home, const std::wstring& workspace,
               const std::wstring& stderr_log, HWND notify, UINT msg, bool allow_shell, std::wstring* error);
    bool start(const std::wstring& exe, const std::wstring& codex_home, const std::wstring& workspace,
               const std::wstring& stderr_log, HWND notify, UINT msg, bool allow_shell,
               const std::vector<std::pair<std::wstring, std::wstring>>& environment, std::wstring* error);
    void stop();
    bool write_line(const std::string& jsonl);
    bool running() const { return process_ != nullptr; }
    DWORD pid() const { return pid_; }

    // Called from reader thread: PostMessage copies the line.
    HWND notify() const { return notify_; }
    UINT notify_msg() const { return notify_msg_; }

private:
    static DWORD WINAPI reader_proc(LPVOID self);
    void reader_loop();

    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE stdin_wr_ = nullptr;
    HANDLE stdout_rd_ = nullptr;
    HANDLE reader_ = nullptr;
    DWORD pid_ = 0;
    HWND notify_ = nullptr;
    UINT notify_msg_ = 0;
    volatile LONG stop_ = 0;
};

}  // namespace scyllagpt
