#include "scyllagpt/ui_kit.h"
#include "scyllagpt/chat_list.h"
#include <richedit.h>
#include <iostream>

int main() {
    using namespace scyllagpt;
    HWND parent = CreateWindowW(L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND view = ui_kit::create_markdown_view(parent, GetModuleHandleW(nullptr), 100, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    if (!view) return 1;
    MoveWindow(view, 0, 0, 600, 400, FALSE);
    ui_kit::set_markdown(view, L"**Bold**\n[File](D:/notes.md)\n`code`");
    GETTEXTEX gt{}; gt.cb = 512; gt.codepage = 1200; gt.flags = GT_DEFAULT;
    wchar_t text[256]{};
    SendMessageW(view, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(text));
    const std::wstring visible(text);
    const auto link_position = visible.find(L"File");
    if (visible != L"Bold\rFile\rcode\r" || ui_kit::markdown_link_at(view, static_cast<long>(link_position)) != L"D:/notes.md") {
        std::wcerr << L"Markdown native text/link mismatch: " << visible << L'\n'; return 2;
    }
    CHARRANGE range{0, 4}; SendMessageW(view, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    SendMessageW(view, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    if (!(cf.dwEffects & CFE_BOLD)) return 3;
    ui_kit::set_markdown(view, L"Replacement");
    if (!ui_kit::markdown_link_at(view, static_cast<long>(link_position)).empty()) return 4;
    ui_kit::append_markdown(view, L"[Next](other.md)");
    SendMessageW(view, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(text));
    const auto next = std::wstring(text).find(L"Next");
    if (ui_kit::markdown_link_at(view, static_cast<long>(next)) != L"other.md") return 5;
    ui_kit::trim_markdown_links(view, static_cast<long>(next));
    if (!ui_kit::markdown_link_at(view, static_cast<long>(next)).empty()) return 6;
    // The shared skin must service repaint ticks while Windows owns the
    // non-client scrollbar tracking loop, and clean up on cancellation.
    SCROLLINFO scroll{}; scroll.cbSize = sizeof(scroll); scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0; scroll.nMax = 1000; scroll.nPage = 100; scroll.nPos = 200;
    SetScrollInfo(view, SB_VERT, &scroll, TRUE);
    install_thin_scrollbar(view, theme().panel);
    SetWindowTextW(view, L"Changed after scrollbar activation\r\nLine 2\r\nLine 3");
    SendMessageW(view, WM_SETFOCUS, 0, 0);
    SendMessageW(view, WM_KILLFOCUS, 0, 0);
    SendMessageW(view, WM_SHOWWINDOW, TRUE, 0);
    SetPropW(view, L"ScyllaThinScrollbarTracking", reinterpret_cast<HANDLE>(1));
    if (SendMessageW(view, WM_TIMER, 0x53425452, 0) != 0) return 7;
    SendMessageW(view, WM_CANCELMODE, 0, 0);
    if (GetPropW(view, L"ScyllaThinScrollbarTracking")) return 8;
    SetPropW(view, L"ScyllaThinScrollbarHover", reinterpret_cast<HANDLE>(1));
    if (SendMessageW(view, WM_TIMER, 0x53425452, 0) != 0) return 18;
    SendMessageW(view, WM_NCMOUSELEAVE, 0, 0);
    if (GetPropW(view, L"ScyllaThinScrollbarHover")) return 19;
    std::vector<std::wstring> groups{L"Today", L""};
    HWND list = CreateWindowW(L"LISTBOX", L"", WS_CHILD | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOTIFY,
                             0, 0, 300, 300, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!list) return 9;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"First chat"));
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Second chat"));
    const int heading = MulDiv(26, GetDpiForWindow(list), 96);
    SendMessageW(list, LB_SETITEMHEIGHT, 0, heading + 40);
    SendMessageW(list, LB_SETITEMHEIGHT, 1, 40);
    SetWindowSubclass(list, chat_list_mouse_guard, 123, reinterpret_cast<DWORD_PTR>(&groups));
    if (chat_row_at_point(list, {10, heading + 5}, groups) != 0 ||
        chat_row_at_point(list, {10, heading + 45}, groups) != 1 ||
        chat_row_at_point(list, {10, 5}, groups) != -1 ||
        chat_row_at_point(list, {10, 250}, groups) != -1 ||
        chat_row_at_point(list, {-1, 50}, groups) != -1) return 10;
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    for (const auto msg : {WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, WM_MOUSEMOVE, WM_LBUTTONUP})
        SendMessageW(list, msg, MK_LBUTTON, MAKELPARAM(10, 250));
    if (SendMessageW(list, LB_GETCURSEL, 0, 0) != 0) return 11;
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 5));
    SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(10, 5));
    if (SendMessageW(list, LB_GETCURSEL, 0, 0) != 1) return 12;
    SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, heading + 5));
    SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(10, heading + 5));
    if (SendMessageW(list, LB_GETCURSEL, 0, 0) != 0) return 13;
    SendMessageW(list, WM_KEYDOWN, VK_DOWN, 0);
    if (SendMessageW(list, LB_GETCURSEL, 0, 0) != 1) return 14;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (chat_row_at_point(list, {10, 5}, groups) != -1) return 15;
    DestroyWindow(parent);
    std::cout << "Markdown and chat hit-testing native control tests passed\n";
    return 0;
}
