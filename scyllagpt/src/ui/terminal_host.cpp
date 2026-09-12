#include "scyllagpt/terminal_host.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/utf.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "uxtheme.lib")

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

// ConPTY (Windows 10 1809+). Prefer these over pipes; pipes remain as fallback.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace scyllagpt {
namespace {

using HPCON = void*;
using CreatePseudoConsole_fn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsole_fn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsole_fn = VOID(WINAPI*)(HPCON);

struct ConPtyApi {
    CreatePseudoConsole_fn create = nullptr;
    ResizePseudoConsole_fn resize = nullptr;
    ClosePseudoConsole_fn close = nullptr;
    bool loaded = false;
};

ConPtyApi& conpty_api() {
    static ConPtyApi api = [] {
        ConPtyApi a;
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        if (!k) {
            return a;
        }
        a.create = reinterpret_cast<CreatePseudoConsole_fn>(GetProcAddress(k, "CreatePseudoConsole"));
        a.resize = reinterpret_cast<ResizePseudoConsole_fn>(GetProcAddress(k, "ResizePseudoConsole"));
        a.close = reinterpret_cast<ClosePseudoConsole_fn>(GetProcAddress(k, "ClosePseudoConsole"));
        a.loaded = a.create && a.resize && a.close;
        return a;
    }();
    return api;
}

void close_handle(HANDLE& h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    h = nullptr;
}

std::wstring quote_arg(const std::wstring& s) {
    if (s.find_first_of(L" \t\"") == std::wstring::npos) {
        return s;
    }
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') {
            out += L"\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring build_cmdline(const TerminalProfile& profile) {
    std::wstring cmd = quote_arg(profile.executable);
    if (!profile.args.empty()) {
        cmd.push_back(L' ');
        cmd += profile.args;
    }
    return cmd;
}

bool ensure_msftedit() {
    static HMODULE mod = LoadLibraryW(L"Msftedit.dll");
    return mod != nullptr;
}

constexpr UINT_PTR kSubclassId = 0x53435954;  // 'SCYT'
constexpr UINT kTerminalOutMsg = WM_APP + 40;

}  // namespace

TerminalHost::~TerminalHost() {
    destroy();
}

bool TerminalHost::create(HWND parent, HWND notify, int control_id, HINSTANCE inst,
                          const TerminalProfile& profile, const std::wstring& cwd,
                          const std::wstring* environment, std::wstring* error) {
    destroy();
    if (!parent) {
        if (error) {
            *error = L"parent HWND required";
        }
        return false;
    }
    if (!ensure_msftedit()) {
        if (error) {
            *error = L"Msftedit.dll unavailable";
        }
        return false;
    }

    InitializeCriticalSection(&output_lock_);
    lock_ready_ = true;
    parent_ = parent;
    notify_ = notify ? notify : GetAncestor(parent, GA_ROOT);
    if (!notify_) {
        notify_ = parent;
    }

    // Size from parent client when known; otherwise a real 80×24 cell guess — never 100×100,
    // which under-sizes ConPTY and triggers a tall redraw once layout runs.
    RECT prc{};
    GetClientRect(parent, &prc);
    int iw = static_cast<int>(prc.right - prc.left);
    int ih = static_cast<int>(prc.bottom - prc.top);
    if (iw < 80) {
        iw = 640;
    }
    if (ih < 40) {
        ih = 320;
    }

    // ES_READONLY: keystrokes must not insert into the control. Echo comes only from the PTY.
    hwnd_ = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                                ES_NOHIDESEL,
                            0, 0, iw, ih, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), inst,
                            nullptr);
    if (!hwnd_) {
        if (error) {
            *error = L"CreateWindow RICHEDIT failed";
        }
        destroy();
        return false;
    }
    // Do not apply DarkMode_Explorer to RICHEDIT50W — it leaves transparent/ghost strips on the
    // left edge when the host is parented under an owner-draw content HWND.
    SetWindowTheme(hwnd_, L"", L"");
    install_thin_scrollbar(hwnd_, theme().app_bg);

    font_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
    if (font_) {
        SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    SendMessageW(hwnd_, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO, 0);
    SendMessageW(hwnd_, EM_SETLIMITTEXT, 0, 4 * 1024 * 1024);
    const COLORREF bg = theme().app_bg;
    SendMessageW(hwnd_, EM_SETBKGNDCOLOR, 0, bg);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    cf.crTextColor = RGB(0xE6, 0xE8, 0xEB);
    cf.crBackColor = bg;
    SendMessageW(hwnd_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    // Kill the RICHEDIT indent/gutter that reads as a vertical “artifact column”.
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_OFFSET | PFM_RIGHTINDENT | PFM_SPACEBEFORE | PFM_SPACEAFTER;
    SendMessageW(hwnd_, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

    SetWindowSubclass(hwnd_, edit_subclass_proc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));

    estimate_console_size(iw, ih, &cols_, &rows_);
    screen_.resize(cols_, rows_);
    SendMessageW(hwnd_, EM_SETTARGETDEVICE, 0, 1); // No widget word wrapping; VT owns the grid.

    if (conpty_api().loaded) {
        if (start_conpty(profile, cwd, environment, error)) {
            return true;
        }
        // Fall through to pipes fallback if ConPTY spawn failed.
    }

    // Fallback: CreateProcess with redirected pipes (no true PTY / resize / VT fidelity).
    // Prefer CreatePseudoConsole when available; this path keeps a basic shell usable.
    if (!start_pipes_fallback(profile, cwd, environment, error)) {
        destroy();
        return false;
    }
    return true;
}

void TerminalHost::destroy() {
    // Keep the output reader draining until ClosePseudoConsole finishes (it can block
    // writing its final frame). Only then stop/join it before releasing its state.
    close_handle(job_);
    if (process_) TerminateProcess(process_, 1);
    if (hpc_ && conpty_api().close) {
        conpty_api().close(hpc_);
        hpc_ = nullptr;
    }
    InterlockedExchange(&stop_, 1);
    if (reader_) {
        // Cancellation can race the next ReadFile; repeat until the reader is joined.
        do { CancelSynchronousIo(reader_); } while (WaitForSingleObject(reader_, 20) == WAIT_TIMEOUT);
        close_handle(reader_);
    }
    close_session_handles();
    screen_ = TerminalScreen();
    cursor_position_ = 0;
    if (hwnd_) {
        RemoveWindowSubclass(hwnd_, edit_subclass_proc, kSubclassId);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (lock_ready_) {
        DeleteCriticalSection(&output_lock_);
        lock_ready_ = false;
    }
    pending_output_.clear();
    poll_accum_.clear();
    parent_ = nullptr;
    notify_ = nullptr;
    pid_ = 0;
    cols_ = 80;
    rows_ = 24;
    stop_ = 0;
}

bool TerminalHost::resize_chars(SHORT cols, SHORT rows) {
    if (cols < 2) {
        cols = 2;
    }
    if (rows < 1) {
        rows = 1;
    }
    if (cols == cols_ && rows == rows_) return true;
    cols_ = cols;
    rows_ = rows;
    screen_.resize(cols_, rows_);
    if (hpc_ && conpty_api().resize) {
        COORD size{};
        size.X = cols_;
        size.Y = rows_;
        return SUCCEEDED(conpty_api().resize(hpc_, size));
    }
    return true;
}

bool TerminalHost::resize_pixels(int width_px, int height_px) {
    SHORT c = cols_;
    SHORT r = rows_;
    estimate_console_size(width_px, height_px, &c, &r);
    return resize_chars(c, r);
}

bool TerminalHost::write_utf8(std::string_view utf8) {
    if (!pipe_in_ || utf8.empty()) {
        return false;
    }
    DWORD wr = 0;
    return WriteFile(pipe_in_, utf8.data(), static_cast<DWORD>(utf8.size()), &wr, nullptr) != 0;
}

bool TerminalHost::write_wide(std::wstring_view utf16) {
    return write_utf8(utf8(utf16));
}

std::size_t TerminalHost::poll(std::string* out) {
    std::string chunk;
    if (lock_ready_) {
        EnterCriticalSection(&output_lock_);
        chunk.swap(pending_output_);
        LeaveCriticalSection(&output_lock_);
    }
    if (!chunk.empty()) {
        append_output_utf8(chunk);
        poll_accum_.append(chunk);
    }
    if (out) {
        *out = poll_accum_;
        poll_accum_.clear();
        return out->size();
    }
    const std::size_t n = poll_accum_.size();
    poll_accum_.clear();
    return n;
}

void TerminalHost::move(int x, int y, int w, int h) {
    if (!hwnd_) {
        return;
    }
    MoveWindow(hwnd_, x, y, w, h, TRUE);
    resize_pixels(w, h);
}

void TerminalHost::set_visible(bool visible) {
    if (hwnd_) {
        // Never HWND_TOP / activating ShowWindow here: raising the ConPTY RICHEDIT
        // sibling over WinUI's DesktopChildSiteBridge has STOW-crashed Fluent on
        // load (0xc000027b / E_POINTER) when LayoutUpdated repositions repeatedly.
        // Visibility comes from the shared island cutout; SW_SHOWNA keeps z-order.
        ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
    }
}

bool TerminalHost::running() const {
    if (!process_) {
        return false;
    }
    const DWORD w = WaitForSingleObject(process_, 0);
    return w == WAIT_TIMEOUT;
}

bool TerminalHost::exited(DWORD* exit_code) const {
    if (!process_) {
        return false;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process_, &code) || code == STILL_ACTIVE) {
        return false;
    }
    if (exit_code) {
        *exit_code = code;
    }
    return true;
}

void TerminalHost::close_session_handles() {
    // Closing the job with KILL_ON_JOB_CLOSE terminates the process tree.
    close_handle(job_);
    if (hpc_ && conpty_api().close) {
        conpty_api().close(hpc_);
        hpc_ = nullptr;
    }
    close_handle(pipe_in_);
    close_handle(pipe_out_);
    if (process_) {
        // Job close should have killed it; ensure handle release.
        TerminateProcess(process_, 1);
        close_handle(process_);
    }
}

bool TerminalHost::start_conpty(const TerminalProfile& profile, const std::wstring& cwd,
                                const std::wstring* environment, std::wstring* error) {
    auto& api = conpty_api();
    if (!api.loaded) {
        if (error) {
            *error = L"ConPTY APIs not available";
        }
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE input_read = nullptr;   // ConPTY reads shell input from here
    HANDLE input_write = nullptr;  // we write keystrokes here
    HANDLE output_read = nullptr;  // we read shell output here
    HANDLE output_write = nullptr; // ConPTY writes shell output here
    if (!CreatePipe(&input_read, &input_write, &sa, 0) || !CreatePipe(&output_read, &output_write, &sa, 0)) {
        if (error) {
            *error = L"CreatePipe failed";
        }
        close_handle(input_read);
        close_handle(input_write);
        close_handle(output_read);
        close_handle(output_write);
        return false;
    }
    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

    COORD size{};
    size.X = cols_;
    size.Y = rows_;
    HPCON hpc = nullptr;
    const HRESULT hr = api.create(size, input_read, output_write, 0, &hpc);
    // ConPTY duplicates these; close our copies of the ends we handed it.
    CloseHandle(input_read);
    CloseHandle(output_write);
    if (FAILED(hr) || !hpc) {
        if (error) {
            *error = L"CreatePseudoConsole failed (" + std::to_wstring(static_cast<unsigned long>(hr)) + L")";
        }
        CloseHandle(input_write);
        CloseHandle(output_read);
        return false;
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
        api.close(hpc);
        CloseHandle(input_write);
        CloseHandle(output_read);
        return false;
    }
    if (!UpdateProcThreadAttribute(attr, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc), nullptr, nullptr)) {
        if (error) {
            *error = L"UpdateProcThreadAttribute PSEUDOCONSOLE failed";
        }
        DeleteProcThreadAttributeList(attr);
        HeapFree(GetProcessHeap(), 0, attr);
        api.close(hpc);
        CloseHandle(input_write);
        CloseHandle(output_read);
        return false;
    }

    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    // Do not let redirected standard handles from the parent override the PTY.
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.lpAttributeList = attr;

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) close_handle(job_);
    }

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = build_cmdline(profile);
    std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(0);
    const wchar_t* cwd_ptr = cwd.empty() ? nullptr : cwd.c_str();

    const BOOL ok = CreateProcessW(profile.executable.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                                   environment && !environment->empty()
                                       ? const_cast<wchar_t*>(environment->data())
                                       : nullptr,
                                   cwd_ptr, &siex.StartupInfo, &pi);
    const DWORD create_err = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attr);
    HeapFree(GetProcessHeap(), 0, attr);

    if (!ok) {
        if (error) {
            *error = L"CreateProcess (ConPTY) failed (" + std::to_wstring(create_err) + L")";
        }
        api.close(hpc);
        CloseHandle(input_write);
        CloseHandle(output_read);
        close_handle(job_);
        return false;
    }

    if (!job_ || !AssignProcessToJobObject(job_, pi.hProcess)) {
        if (error) *error = L"Cannot contain terminal process tree in a job.";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        close_handle(job_);
        // The child was suspended, so it cannot have spawned descendants.
        api.close(hpc); CloseHandle(input_write); CloseHandle(output_read);
        return false;
    }

    hpc_ = hpc;
    pipe_in_ = input_write;
    pipe_out_ = output_read;
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    stop_ = 0;
    reader_ = CreateThread(nullptr, 0, reader_proc, this, 0, nullptr);
    if (!reader_) {
        if (error) *error = L"Cannot start terminal output reader.";
        CloseHandle(pi.hThread);
        close_session_handles();
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return true;
}

bool TerminalHost::start_pipes_fallback(const TerminalProfile& profile, const std::wstring& cwd,
                                        const std::wstring* environment, std::wstring* error) {
    // Fallback path: no ConPTY — redirected stdin/stdout pipes only.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_rd = nullptr;
    HANDLE stdin_wr = nullptr;
    HANDLE stdout_rd = nullptr;
    HANDLE stdout_wr = nullptr;
    if (!CreatePipe(&stdin_rd, &stdin_wr, &sa, 0) || !CreatePipe(&stdout_rd, &stdout_wr, &sa, 0)) {
        if (error) {
            *error = L"CreatePipe (fallback) failed";
        }
        close_handle(stdin_rd);
        close_handle(stdin_wr);
        close_handle(stdout_rd);
        close_handle(stdout_wr);
        return false;
    }
    SetHandleInformation(stdin_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) close_handle(job_);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdin_rd;
    si.hStdOutput = stdout_wr;
    si.hStdError = stdout_wr;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = build_cmdline(profile);
    std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(0);
    const wchar_t* cwd_ptr = cwd.empty() ? nullptr : cwd.c_str();

    const BOOL ok =
        CreateProcessW(profile.executable.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                       environment && !environment->empty() ? const_cast<wchar_t*>(environment->data())
                                                            : nullptr,
                       cwd_ptr, &si, &pi);
    const DWORD create_err = ok ? 0 : GetLastError();
    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);
    if (!ok) {
        if (error) {
            *error = L"CreateProcess (pipes fallback) failed (" + std::to_wstring(create_err) + L")";
        }
        CloseHandle(stdin_wr);
        CloseHandle(stdout_rd);
        close_handle(job_);
        return false;
    }
    if (!job_ || !AssignProcessToJobObject(job_, pi.hProcess)) {
        if (error) *error = L"Cannot contain terminal process tree in a job.";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        close_handle(job_);
        // The child was suspended, so it cannot have spawned descendants.
        CloseHandle(stdin_wr); CloseHandle(stdout_rd);
        return false;
    }

    hpc_ = nullptr;
    pipe_in_ = stdin_wr;
    pipe_out_ = stdout_rd;
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    stop_ = 0;
    reader_ = CreateThread(nullptr, 0, reader_proc, this, 0, nullptr);
    if (!reader_) {
        if (error) *error = L"Cannot start terminal output reader.";
        CloseHandle(pi.hThread);
        close_session_handles();
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return true;
}

DWORD WINAPI TerminalHost::reader_proc(LPVOID self) {
    static_cast<TerminalHost*>(self)->reader_loop();
    return 0;
}

void TerminalHost::reader_loop() {
    char buf[4096];
    while (InterlockedCompareExchange(&stop_, 0, 0) == 0) {
        DWORD got = 0;
        if (!ReadFile(pipe_out_, buf, sizeof(buf), &got, nullptr) || got == 0) {
            break;
        }
        if (lock_ready_) {
            EnterCriticalSection(&output_lock_);
            pending_output_.append(buf, buf + got);
            LeaveCriticalSection(&output_lock_);
        }
        notify_output_ready();
    }
}

void TerminalHost::notify_output_ready() {
    HWND target = notify_;
    if (!target || !IsWindow(target)) {
        target = parent_;
    }
    if (target && IsWindow(target)) {
        PostMessageW(target, kTerminalOutMsg, 0, reinterpret_cast<LPARAM>(hwnd_));
    }
}

void TerminalHost::clear_view() {
    if (!hwnd_) {
        return;
    }
    SetWindowTextW(hwnd_, L"");
}

void TerminalHost::scroll_to_end() {
    if (!hwnd_) {
        return;
    }
    SendMessageW(hwnd_, EM_SETSEL, cursor_position_, cursor_position_);
    SendMessageW(hwnd_, EM_SCROLLCARET, 0, 0);
}

void TerminalHost::append_output_utf8(std::string_view chunk) {
    if (!hwnd_) return;
    screen_.feed(chunk);
    const auto reply = screen_.take_reply();
    if (!reply.empty()) write_utf8(reply);
    std::size_t cursor = 0;
    const auto text = screen_.text(&cursor);
    CHARRANGE selection{};
    SendMessageW(hwnd_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    POINT scroll{};
    SendMessageW(hwnd_, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    const bool selecting = selection.cpMin != selection.cpMax;
    // WM_SETREDRAW(TRUE) makes a hidden window visible: never use it on hidden tabs.
    const bool visible = IsWindowVisible(hwnd_) != FALSE;
    if (visible) SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
    SETTEXTEX st{ST_DEFAULT, 1200};
    SendMessageW(hwnd_, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(text.c_str()));
    cursor_position_ = static_cast<LONG>(cursor);
    if (selecting) {
        SendMessageW(hwnd_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(hwnd_, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    } else scroll_to_end();
    if (visible) {
        SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}
void TerminalHost::forward_key(WPARAM vk, bool ctrl, bool /*alt*/) {
    std::string seq;
    if (vk == VK_RETURN) {
        seq = "\r";
    } else if (vk == VK_BACK) {
        // DEL is the VT backspace key; the screen interprets the shell redraw.
        seq = "\x7f";
    } else if (vk == VK_TAB) {
        seq = "\t";
    } else if (vk == VK_ESCAPE) {
        seq = "\x1b";
    } else if (vk == VK_UP) {
        seq = "\x1b[A";
    } else if (vk == VK_DOWN) {
        seq = "\x1b[B";
    } else if (vk == VK_RIGHT) {
        seq = "\x1b[C";
    } else if (vk == VK_LEFT) {
        seq = "\x1b[D";
    } else if (vk == VK_HOME) {
        seq = "\x1b[H";
    } else if (vk == VK_END) {
        seq = "\x1b[F";
    } else if (vk == VK_DELETE) {
        seq = "\x1b[3~";
    } else if (vk == VK_PRIOR) {
        seq = "\x1b[5~";
    } else if (vk == VK_NEXT) {
        seq = "\x1b[6~";
    } else if (ctrl && vk >= 'A' && vk <= 'Z') {
        seq.push_back(static_cast<char>(vk - 'A' + 1));
    } else {
        return;
    }
    if (screen_.application_cursor() && seq.size() == 3 && seq[1] == '[') seq[1] = 'O';
    write_utf8(seq);
}

void TerminalHost::estimate_console_size(int width_px, int height_px, SHORT* cols, SHORT* rows) const {
    int cw = 8;
    int ch = 16;
    if (hwnd_) {
        HDC dc = GetDC(hwnd_);
        if (dc) {
            HFONT old = font_ ? static_cast<HFONT>(SelectObject(dc, font_)) : nullptr;
            TEXTMETRICW tm{};
            if (GetTextMetricsW(dc, &tm)) {
                cw = (std::max)(1, static_cast<int>(tm.tmAveCharWidth));
                ch = (std::max)(1, static_cast<int>(tm.tmHeight));
            }
            if (old) {
                SelectObject(dc, old);
            }
            ReleaseDC(hwnd_, dc);
        }
    }
    SHORT c = static_cast<SHORT>((std::max)(2, (width_px - GetSystemMetrics(SM_CXVSCROLL) - 4) / cw));
    SHORT r = static_cast<SHORT>((std::max)(1, (height_px - 4) / ch));
    if (cols) {
        *cols = c;
    }
    if (rows) {
        *rows = r;
    }
}

LRESULT CALLBACK TerminalHost::edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR /*subclass_id*/, DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<TerminalHost*>(ref_data);
    if (!self) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
            self->scroll_to_end();
            break;
        case WM_LBUTTONUP: {
            // Allow drag-select for copy; a plain click must not leave the caret on a history
            // line the user cannot type into — snap back to the input end.
            const LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            CHARRANGE cr{};
            SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            if (cr.cpMin == cr.cpMax) {
                self->scroll_to_end();
            }
            return r;
        }
        case WM_CHAR: {
            // Return/Back/Tab/Esc are forwarded on WM_KEYDOWN (ES_READONLY often skips WM_CHAR
            // for Backspace). Eat duplicates here so TranslateMessage cannot double-send.
            if (wParam == VK_RETURN || wParam == VK_BACK || wParam == VK_TAB || wParam == VK_ESCAPE) {
                return 0;
            }
            if (wParam >= 32) {
                self->scroll_to_end();
                wchar_t ch = static_cast<wchar_t>(wParam);
                self->write_wide(std::wstring_view(&ch, 1));
                return 0;
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrl && wParam == 'V') { SendMessageW(hwnd, WM_PASTE, 0, 0); return 0; }
            if (ctrl && wParam == 'C') {
                CHARRANGE range{};
                SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
                if (range.cpMin != range.cpMax) { SendMessageW(hwnd, WM_COPY, 0, 0); return 0; }
            }
            if (wParam == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000)) { SendMessageW(hwnd, WM_PASTE, 0, 0); return 0; }
            switch (wParam) {
                case VK_RETURN:
                case VK_BACK:
                case VK_TAB:
                case VK_ESCAPE:
                    self->scroll_to_end();
                    self->forward_key(wParam, false, false);
                    return 0;
                case VK_UP:
                case VK_DOWN:
                case VK_LEFT:
                case VK_RIGHT:
                case VK_HOME:
                case VK_END:
                case VK_DELETE:
                case VK_PRIOR:
                case VK_NEXT:
                    // Arrow/nav keys go to the PTY (readline), not the RICHEDIT caret — that was
                    // the "cursor on a line I can't type into" behavior.
                    self->scroll_to_end();
                    self->forward_key(wParam, ctrl, alt);
                    return 0;
                default:
                    if (ctrl && wParam >= 'A' && wParam <= 'Z') {
                        self->scroll_to_end();
                        self->forward_key(wParam, true, alt);
                        return 0;
                    }
                    break;
            }
            break;
        }
        case WM_PASTE: {
            // Paste into the PTY, not the read-only buffer.
            self->scroll_to_end();
            if (OpenClipboard(hwnd)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                        if (self->screen_.bracketed_paste()) self->write_utf8("\x1b[200~");
                        self->write_wide(p);
                        if (self->screen_.bracketed_paste()) self->write_utf8("\x1b[201~");
                        GlobalUnlock(h);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, edit_subclass_proc, kSubclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace scyllagpt
