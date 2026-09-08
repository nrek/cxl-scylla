#pragma once

#include "scyllagpt/terminal_profiles.h"
#include "scyllagpt/terminal_screen.h"

#include <cstddef>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

// ConPTY-backed terminal session with a child RICHEDIT view.
// window.cpp owns layout/visibility; this class owns process + HWND lifecycle.
//
// Incremental VT screen rendered through a read-only text view. Input goes only to the PTY.
class TerminalHost {
public:
    TerminalHost() = default;
    ~TerminalHost();

    TerminalHost(const TerminalHost&) = delete;
    TerminalHost& operator=(const TerminalHost&) = delete;

    // Create RICHEDIT child under |parent| and start shell via CreatePseudoConsole.
    // |notify| receives WM_APP+40 when output is ready (must be the main frame, not content_).
    // Falls back to anonymous pipes if ConPTY is unavailable (labeled in .cpp).
    // |environment| is an optional Unicode env block (NAME=value\0...\0\0). nullptr = inherit.
    bool create(HWND parent, HWND notify, int control_id, HINSTANCE inst, const TerminalProfile& profile,
                const std::wstring& cwd, const std::wstring* environment = nullptr,
                std::wstring* error = nullptr);

    void destroy();

    bool resize_chars(SHORT cols, SHORT rows);
    // Estimates cols/rows from current font metrics, then ResizePseudoConsole.
    bool resize_pixels(int width_px, int height_px);

    bool write_utf8(std::string_view utf8);
    bool write_wide(std::wstring_view utf16);

    // Non-blocking drain of output already painted (or pending) into |out|.
    // Also pumps the read pipe into the RICHEDIT when called from the UI thread.
    std::size_t poll(std::string* out);

    void move(int x, int y, int w, int h);
    void set_visible(bool visible);

    bool running() const;
    // True once the shell process has terminated, writing its exit code to |exit_code|.
    // False while the process is alive or when no process was ever started.
    bool exited(DWORD* exit_code) const;
    HWND hwnd() const { return hwnd_; }
    DWORD pid() const { return pid_; }
    bool using_conpty() const { return hpc_ != nullptr; }

private:
    static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclass_id, DWORD_PTR ref_data);
    static DWORD WINAPI reader_proc(LPVOID self);

    bool start_conpty(const TerminalProfile& profile, const std::wstring& cwd,
                      const std::wstring* environment, std::wstring* error);
    bool start_pipes_fallback(const TerminalProfile& profile, const std::wstring& cwd,
                              const std::wstring* environment, std::wstring* error);
    void reader_loop();
    void append_output_utf8(std::string_view chunk);
    void clear_view();
    void scroll_to_end();


    void forward_key(WPARAM vk, bool ctrl, bool alt);
    void estimate_console_size(int width_px, int height_px, SHORT* cols, SHORT* rows) const;
    void close_session_handles();
    void notify_output_ready();

    TerminalScreen screen_;
    LONG cursor_position_ = 0;
    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HWND notify_ = nullptr;  // main frame for WM_APP+40 (never content_ STATIC)
    HFONT font_ = nullptr;

    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE hpc_ = nullptr;  // HPCON when ConPTY path used
    HANDLE pipe_in_ = nullptr;   // write end → console input
    HANDLE pipe_out_ = nullptr;  // read end ← console output
    HANDLE reader_ = nullptr;
    DWORD pid_ = 0;
    volatile LONG stop_ = 0;

    SHORT cols_ = 80;
    SHORT rows_ = 24;

    CRITICAL_SECTION output_lock_{};
    bool lock_ready_ = false;
    std::string pending_output_;
    std::string poll_accum_;
};

}  // namespace scyllagpt
