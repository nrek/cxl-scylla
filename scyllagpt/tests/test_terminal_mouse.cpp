#include "scyllagpt/terminal_host.h"
#include <commctrl.h>
#include <richedit.h>
#include <iostream>
#include <stdexcept>

// Probe clipboard messages without reading or replacing the user's clipboard.
namespace {
int copies = 0, pastes = 0;
LRESULT CALLBACK probe(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_COPY) { ++copies; return 0; }
    if (msg == WM_PASTE) { ++pastes; return 0; }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
void check(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(label);
    std::cout << "PASS: " << label << '\n';
}
void select(HWND hwnd) {
    SETTEXTEX text{ST_DEFAULT, 1200};
    SendMessageW(hwnd, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&text), reinterpret_cast<LPARAM>(L"selected terminal output"));
    SendMessageW(hwnd, EM_SETSEL, 0, 8);
}
}

int main() {
    HWND parent = CreateWindowExW(0, L"STATIC", L"Terminal mouse tests", 0, 0, 0, 640, 320,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    scyllagpt::TerminalHost terminal;
    try {
        check(parent != nullptr, "hidden parent created");
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        scyllagpt::TerminalProfile profile;
        profile.executable = std::wstring(system) + L"\\cmd.exe";
        profile.args = L"/d /q /k";
        std::wstring error;
        check(terminal.create(parent, parent, 9001, GetModuleHandleW(nullptr), profile, L"", nullptr, &error), "terminal created");
        HWND hwnd = terminal.hwnd();
        SetWindowSubclass(hwnd, probe, 99, 0);
        select(hwnd);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
        check(copies == 1, "default highlight copies");
        SendMessageW(hwnd, WM_RBUTTONUP, 0, 0);
        check(pastes == 1 && copies == 1, "highlight mode right-click pastes");

        terminal.set_mouse_behavior(1);
        select(hwnd);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
        check(copies == 1, "right-click copy mode does not copy on selection");
        SendMessageW(hwnd, WM_RBUTTONDOWN, 0, MAKELPARAM(500, 200));
        SendMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(500, 200));
        check(copies == 2 && pastes == 1, "first right-click copies preserved selection");
        CHARRANGE range{};
        SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
        check(range.cpMin == range.cpMax, "copy consumes selection");
        SendMessageW(hwnd, WM_RBUTTONUP, 0, 0);
        check(pastes == 2, "second right-click pastes");

        terminal.set_mouse_behavior(2);
        select(hwnd);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
        check(copies == 2, "menu mode does not copy on selection");
        check(!terminal.take_new_terminal_request(), "new-terminal request is initially empty");
        terminal.set_mouse_behavior(99);
        select(hwnd);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
        check(copies == 3, "invalid mode falls back to highlight copy");
        RemoveWindowSubclass(hwnd, probe, 99);
        terminal.destroy();
        DestroyWindow(parent);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        terminal.destroy();
        if (parent) DestroyWindow(parent);
        return 1;
    }
}
