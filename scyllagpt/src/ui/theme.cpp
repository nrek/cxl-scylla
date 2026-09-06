#include "scyllagpt/theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <unordered_set>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace scyllagpt {
namespace {

const Theme kTheme{};

const std::unordered_set<std::wstring> kKeywords = {
    L"alignas",    L"alignof",     L"and",         L"and_eq",     L"asm",       L"auto",
    L"bitand",     L"bitor",       L"bool",        L"break",      L"case",      L"catch",
    L"char",       L"char8_t",     L"char16_t",    L"char32_t",   L"class",     L"compl",
    L"concept",    L"const",       L"consteval",   L"constexpr",  L"constinit", L"const_cast",
    L"continue",   L"co_await",    L"co_return",    L"co_yield",   L"decltype",  L"default",
    L"delete",     L"do",          L"double",      L"dynamic_cast", L"else",    L"enum",
    L"explicit",   L"export",      L"extern",      L"false",      L"float",     L"for",
    L"friend",     L"goto",        L"if",          L"inline",     L"int",       L"long",
    L"mutable",    L"namespace",    L"new",         L"noexcept",   L"not",       L"not_eq",
    L"nullptr",    L"operator",    L"or",          L"or_eq",      L"private",   L"protected",
    L"public",     L"register",    L"reinterpret_cast", L"requires", L"return",  L"short",
    L"signed",     L"sizeof",      L"static",      L"static_assert", L"static_cast", L"struct",
    L"switch",     L"template",    L"this",        L"thread_local", L"throw",    L"true",
    L"try",        L"typedef",     L"typeid",      L"typename",   L"union",     L"unsigned",
    L"using",      L"virtual",     L"void",        L"volatile",   L"wchar_t",   L"while",
    L"xor",        L"xor_eq",      L"override",     L"final",
};

void draw_icon_folder(HDC dc, int x, int y, int s, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH br = CreateSolidBrush(c);
    HPEN oldp = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldb = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    RoundRect(dc, x, y + s / 4, x + s, y + s, 2, 2);
    SelectObject(dc, br);
    RoundRect(dc, x, y, x + s * 2 / 3, y + s / 2, 2, 2);
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
    DeleteObject(pen);
    DeleteObject(br);
}

void draw_icon_gear(HDC dc, int x, int y, int s, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    Ellipse(dc, x + s / 4, y + s / 4, x + s * 3 / 4, y + s * 3 / 4);
    MoveToEx(dc, x + s / 2, y, nullptr);
    LineTo(dc, x + s / 2, y + s / 5);
    MoveToEx(dc, x + s / 2, y + s * 4 / 5, nullptr);
    LineTo(dc, x + s / 2, y + s);
    MoveToEx(dc, x, y + s / 2, nullptr);
    LineTo(dc, x + s / 5, y + s / 2);
    MoveToEx(dc, x + s * 4 / 5, y + s / 2, nullptr);
    LineTo(dc, x + s, y + s / 2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_icon_send(HDC dc, int x, int y, int s, COLORREF c) {
    // White up-arrow CTA (not a right-facing play triangle).
    const int cx = x + s / 2;
    const int top = y + s / 5;
    const int bottom = y + s * 4 / 5;
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, cx, bottom, nullptr);
    LineTo(dc, cx, top + 2);
    MoveToEx(dc, cx - s / 4, top + s / 3, nullptr);
    LineTo(dc, cx, top);
    LineTo(dc, cx + s / 4, top + s / 3);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_icon_paperclip(HDC dc, int x, int y, int s, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, x + s / 5, y + s / 6, x + s * 3 / 5, y + s * 5 / 6, s / 3, s / 3);
    RoundRect(dc, x + s * 2 / 5, y + s / 3, x + s * 4 / 5, y + s * 5 / 6, s / 4, s / 4);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_icon_stop(HDC dc, int x, int y, int s, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    RECT r{x + s / 4, y + s / 4, x + s * 3 / 4, y + s * 3 / 4};
    FillRect(dc, &r, br);
    DeleteObject(br);
}

void draw_icon_save(HDC dc, int x, int y, int s, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH br = CreateSolidBrush(c);
    HPEN oldp = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldb = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, x, y, x + s, y + s);
    MoveToEx(dc, x + s / 4, y + s * 2 / 3, nullptr);
    LineTo(dc, x + s * 3 / 4, y + s * 2 / 3);
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
    DeleteObject(pen);
    DeleteObject(br);
}

void draw_icon_find(HDC dc, int x, int y, int s, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldb = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    const int d = s * 10 / 16;
    Ellipse(dc, x + 1, y + 1, x + 1 + d, y + 1 + d);
    MoveToEx(dc, x + s * 10 / 16, y + s * 10 / 16, nullptr);
    LineTo(dc, x + s - 1, y + s - 1);
    SelectObject(dc, old);
    SelectObject(dc, oldb);
    DeleteObject(pen);
}

COLORREF token_color(CppKind k) {
    const Theme& t = kTheme;
    switch (k) {
        case CppKind::Keyword:
            return t.keyword;
        case CppKind::String:
            return t.string_lit;
        case CppKind::Comment:
            return t.comment;
        case CppKind::Number:
            return t.number;
        case CppKind::Preproc:
            return t.preproc;
        case CppKind::Punct:
            return t.punct;
        case CppKind::Ident:
            return t.ident;
        default:
            return t.ident;
    }
}

}  // namespace

const Theme& theme() {
    return kTheme;
}

bool high_contrast_on() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
    return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

void apply_dark_caption(HWND hwnd) {
    if (high_contrast_on()) {
        return;
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF cap = theme().shell;
    COLORREF txt = theme().text;
    COLORREF brd = theme().divider;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &txt, sizeof(txt));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &brd, sizeof(brd));
    apply_dark_menus(hwnd);
}

void apply_dark_menus(HWND hwnd) {
    if (high_contrast_on()) {
        return;
    }
    // Undocumented uxtheme dark-mode hooks (Win10 1809+ / Win11). Same path Explorer uses.
    using FnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);
    using FnSetPreferredAppMode = int(WINAPI*)(int);  // 0 default, 1 allow, 2 force dark, 3 force light
    using FnFlushMenuThemes = void(WINAPI*)();
    using FnRefreshImmersiveColorPolicyState = void(WINAPI*)();

    static HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) {
        return;
    }
    static auto allow_dark =
        reinterpret_cast<FnAllowDarkModeForWindow>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
    static auto set_mode =
        reinterpret_cast<FnSetPreferredAppMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    static auto flush_menus = reinterpret_cast<FnFlushMenuThemes>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    static auto refresh_policy =
        reinterpret_cast<FnRefreshImmersiveColorPolicyState>(GetProcAddress(ux, MAKEINTRESOURCEA(104)));

    if (set_mode) {
        set_mode(2);  // ForceDark
    }
    if (hwnd && allow_dark) {
        allow_dark(hwnd, TRUE);
    }
    if (refresh_policy) {
        refresh_policy();
    }
    if (flush_menus) {
        flush_menus();
    }
    if (hwnd) {
        DrawMenuBar(hwnd);
    }
}

namespace {

// Undocumented menubar draw messages (uxtheme). ForceDark paints popups but not the bar strip.
constexpr UINT WM_UAHDRAWMENU = 0x0091;
constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;
constexpr UINT WM_UAHMEASUREMENUITEM = 0x0094;

union UAHMENUITEMMETRICS {
    struct {
        DWORD cx;
        DWORD cy;
    } rgsizeBar[2];
    struct {
        DWORD cx;
        DWORD cy;
    } rgsizePopup[4];
};

struct UAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
};

struct UAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
};

struct UAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
};

struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
};

struct UAHMEASUREMENUITEM {
    MEASUREITEMSTRUCT mis;
    UAHMENU um;
    UAHMENUITEM umi;
};

HTHEME g_menu_theme = nullptr;

void draw_uah_menu_nc_line(HWND hwnd) {
    MENUBARINFO mbi{sizeof(mbi)};
    if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
        return;
    }
    RECT rc_client{};
    GetClientRect(hwnd, &rc_client);
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&rc_client), 2);
    RECT rc_window{};
    GetWindowRect(hwnd, &rc_window);
    OffsetRect(&rc_client, -rc_window.left, -rc_window.top);
    RECT line = rc_client;
    line.bottom = line.top;
    line.top -= 1;
    HDC hdc = GetWindowDC(hwnd);
    HBRUSH br = CreateSolidBrush(theme().shell);
    FillRect(hdc, &line, br);
    DeleteObject(br);
    ReleaseDC(hwnd, hdc);
}

}  // namespace

bool handle_dark_menubar_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT* result) {
    if (!result || high_contrast_on()) {
        return false;
    }
    switch (msg) {
        case WM_UAHDRAWMENU: {
            auto* udm = reinterpret_cast<UAHMENU*>(lparam);
            MENUBARINFO mbi{sizeof(mbi)};
            GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi);
            RECT rc_window{};
            GetWindowRect(hwnd, &rc_window);
            RECT rc = mbi.rcBar;
            OffsetRect(&rc, -rc_window.left, -rc_window.top);
            HBRUSH br = CreateSolidBrush(theme().shell);
            FillRect(udm->hdc, &rc, br);
            DeleteObject(br);
            *result = 0;
            return true;
        }
        case WM_UAHDRAWMENUITEM: {
            auto* udmi = reinterpret_cast<UAHDRAWMENUITEM*>(lparam);
            const Theme& t = theme();
            const bool hot = (udmi->dis.itemState & ODS_HOTLIGHT) != 0;
            const bool selected = (udmi->dis.itemState & ODS_SELECTED) != 0;
            const bool disabled =
                (udmi->dis.itemState & ODS_GRAYED) != 0 || (udmi->dis.itemState & ODS_DISABLED) != 0;

            COLORREF bg = t.shell;
            if (selected) {
                bg = t.selected;
            } else if (hot) {
                bg = t.hover;
            }
            HBRUSH br = CreateSolidBrush(bg);
            FillRect(udmi->um.hdc, &udmi->dis.rcItem, br);
            DeleteObject(br);

            wchar_t label[256]{};
            MENUITEMINFOW mii{sizeof(mii)};
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = label;
            mii.cch = 255;
            GetMenuItemInfoW(udmi->um.hmenu, static_cast<UINT>(udmi->umi.iPosition), TRUE, &mii);

            DWORD flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
            if (udmi->dis.itemState & ODS_NOACCEL) {
                flags |= DT_HIDEPREFIX;
            }
            if (!g_menu_theme) {
                g_menu_theme = OpenThemeData(hwnd, L"Menu");
            }
            DTTOPTS opts{};
            opts.dwSize = sizeof(opts);
            opts.dwFlags = DTT_TEXTCOLOR;
            opts.crText = disabled ? t.muted : t.text;
            if (g_menu_theme) {
                DrawThemeTextEx(g_menu_theme, udmi->um.hdc, MENU_BARITEM, MBI_NORMAL, label, -1, flags,
                                &udmi->dis.rcItem, &opts);
            } else {
                SetBkMode(udmi->um.hdc, TRANSPARENT);
                SetTextColor(udmi->um.hdc, opts.crText);
                DrawTextW(udmi->um.hdc, label, -1, &udmi->dis.rcItem, flags);
            }
            *result = 0;
            return true;
        }
        case WM_UAHMEASUREMENUITEM: {
            *result = DefWindowProcW(hwnd, msg, wparam, lparam);
            return true;
        }
        case WM_THEMECHANGED: {
            if (g_menu_theme) {
                CloseThemeData(g_menu_theme);
                g_menu_theme = nullptr;
            }
            return false;
        }
        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            *result = DefWindowProcW(hwnd, msg, wparam, lparam);
            draw_uah_menu_nc_line(hwnd);
            return true;
        }
        default:
            return false;
    }
}

void apply_dark_child(HWND hwnd) {
    if (!hwnd || high_contrast_on()) {
        return;
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

namespace {

constexpr UINT_PTR kThinSbSubclassId = 0x5342594C;  // 'SBYL'
constexpr COLORREF kThumb = RGB(0x3E, 0x3E, 0x3E);
constexpr COLORREF kThumbHot = RGB(0x52, 0x52, 0x52);

int sb_thumb_width(HWND hwnd) {
    const int dpi = static_cast<int>(GetDpiForWindow(hwnd));
    return (std::max)(6, MulDiv(8, dpi, 96));
}

bool paint_thin_vscrollbar(HWND hwnd, COLORREF track, bool hot) {
    if (!hwnd || high_contrast_on()) {
        return false;
    }
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & WS_VSCROLL) == 0) {
        return false;
    }
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(hwnd, SB_VERT, &si)) {
        return false;
    }
    const int span = si.nMax - si.nMin + 1;
    if (span <= 0 || static_cast<int>(si.nPage) >= span) {
        return false;
    }

    RECT wr{};
    RECT cr{};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT pt_tl{0, 0};
    POINT pt_br{cr.right, cr.bottom};
    ClientToScreen(hwnd, &pt_tl);
    ClientToScreen(hwnd, &pt_br);

    const int sb_w = GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(hwnd));
    const int sb_left = pt_tl.x - wr.left + cr.right;
    const int sb_right = sb_left + sb_w;
    const int top = pt_tl.y - wr.top;
    const int bottom = pt_br.y - wr.top;
    if (sb_right <= sb_left || bottom <= top) {
        return false;
    }

    HDC dc = GetWindowDC(hwnd);
    if (!dc) {
        return false;
    }
    RECT track_r{sb_left, top, sb_right, bottom};
    fill_rect(dc, track_r, track);

    const int track_h = bottom - top;
    int thumb_h = MulDiv(static_cast<int>(si.nPage), track_h, span);
    const int min_thumb = (std::max)(16, MulDiv(24, static_cast<int>(GetDpiForWindow(hwnd)), 96));
    if (thumb_h < min_thumb) {
        thumb_h = min_thumb;
    }
    if (thumb_h > track_h) {
        thumb_h = track_h;
    }
    const int travel = track_h - thumb_h;
    int thumb_y = top;
    const int scrollable = span - static_cast<int>(si.nPage);
    if (travel > 0 && scrollable > 0) {
        thumb_y += MulDiv(si.nPos - si.nMin, travel, scrollable);
    }

    const int tw = sb_thumb_width(hwnd);
    const int mid = (sb_left + sb_right) / 2;
    RECT thumb{mid - tw / 2, thumb_y, mid + (tw + 1) / 2, thumb_y + thumb_h};
    if (thumb.left < sb_left + 1) {
        thumb.left = sb_left + 1;
        thumb.right = thumb.left + tw;
    }
    if (thumb.right > sb_right - 1) {
        thumb.right = sb_right - 1;
        thumb.left = thumb.right - tw;
    }
    fill_rect(dc, thumb, hot ? kThumbHot : kThumb);
    ReleaseDC(hwnd, dc);
    return true;
}

bool paint_thin_hscrollbar(HWND hwnd, COLORREF track, bool hot) {
    if (!hwnd || high_contrast_on()) {
        return false;
    }
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & WS_HSCROLL) == 0) {
        return false;
    }
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(hwnd, SB_HORZ, &si)) {
        return false;
    }
    const int span = si.nMax - si.nMin + 1;
    if (span <= 0 || static_cast<int>(si.nPage) >= span) {
        return false;
    }

    RECT wr{};
    RECT cr{};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT pt_tl{0, 0};
    POINT pt_br{cr.right, cr.bottom};
    ClientToScreen(hwnd, &pt_tl);
    ClientToScreen(hwnd, &pt_br);

    const int sb_h = GetSystemMetricsForDpi(SM_CYHSCROLL, GetDpiForWindow(hwnd));
    const int left = pt_tl.x - wr.left;
    const int right = pt_br.x - wr.left;
    const int sb_top = pt_tl.y - wr.top + cr.bottom;
    const int sb_bottom = sb_top + sb_h;
    if (right <= left || sb_bottom <= sb_top) {
        return false;
    }

    HDC dc = GetWindowDC(hwnd);
    if (!dc) {
        return false;
    }
    RECT track_r{left, sb_top, right, sb_bottom};
    fill_rect(dc, track_r, track);

    const int track_w = right - left;
    int thumb_w = MulDiv(static_cast<int>(si.nPage), track_w, span);
    const int min_thumb = (std::max)(16, MulDiv(24, static_cast<int>(GetDpiForWindow(hwnd)), 96));
    if (thumb_w < min_thumb) {
        thumb_w = min_thumb;
    }
    if (thumb_w > track_w) {
        thumb_w = track_w;
    }
    const int travel = track_w - thumb_w;
    int thumb_x = left;
    const int scrollable = span - static_cast<int>(si.nPage);
    if (travel > 0 && scrollable > 0) {
        thumb_x += MulDiv(si.nPos - si.nMin, travel, scrollable);
    }

    const int th = sb_thumb_width(hwnd);
    const int mid = (sb_top + sb_bottom) / 2;
    RECT thumb{thumb_x, mid - th / 2, thumb_x + thumb_w, mid + (th + 1) / 2};
    fill_rect(dc, thumb, hot ? kThumbHot : kThumb);
    ReleaseDC(hwnd, dc);
    return true;
}

void paint_thin_scrollbars(HWND hwnd, COLORREF track) {
    paint_thin_vscrollbar(hwnd, track, false);
    paint_thin_hscrollbar(hwnd, track, false);
}

LRESULT CALLBACK thin_sb_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref) {
    const COLORREF track = static_cast<COLORREF>(ref);
    switch (msg) {
        case WM_NCPAINT:
        case WM_PAINT:
        case WM_NCCALCSIZE:
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            const LRESULT r = DefSubclassProc(hwnd, msg, wparam, lparam);
            paint_thin_scrollbars(hwnd, track);
            return r;
        }
        case WM_NCMOUSEMOVE:
        case WM_MOUSEMOVE: {
            const LRESULT r = DefSubclassProc(hwnd, msg, wparam, lparam);
            paint_thin_scrollbars(hwnd, track);
            return r;
        }
        case WM_SIZE:
        case WM_STYLECHANGED: {
            const LRESULT r = DefSubclassProc(hwnd, msg, wparam, lparam);
            paint_thin_scrollbars(hwnd, track);
            return r;
        }
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

void install_thin_scrollbar(HWND hwnd, COLORREF track) {
    if (!hwnd || high_contrast_on()) {
        return;
    }
    // Strip themed arrows so our flat overlay covers a plain bar.
    SetWindowTheme(hwnd, L"", L"");
    RemoveWindowSubclass(hwnd, thin_sb_subclass, kThinSbSubclassId);
    SetWindowSubclass(hwnd, thin_sb_subclass, kThinSbSubclassId, static_cast<DWORD_PTR>(track));
    refresh_thin_scrollbar(hwnd);
}

void refresh_thin_scrollbar(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
}

void fill_rect(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

void round_fill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF edge) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HBRUSH oldb = static_cast<HBRUSH>(SelectObject(dc, br));
    HPEN oldp = static_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(br);
    DeleteObject(pen);
}

void draw_focus_ring(HDC dc, const RECT& r, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_chevron(HDC dc, int cx, int cy, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, cx - 4, cy - 2, nullptr);
    LineTo(dc, cx, cy + 2);
    LineTo(dc, cx + 4, cy - 2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_themed_button(const DRAWITEMSTRUCT* di, HFONT font, BtnVisual vis, bool toggled, bool generating) {
    const Theme& t = theme();
    RECT r = di->rcItem;
    const bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
    const bool down = (di->itemState & ODS_SELECTED) != 0;
    const bool focus = (di->itemState & ODS_FOCUS) != 0;
    const bool disabled = (di->itemState & ODS_DISABLED) != 0;
    COLORREF fill = t.raised;
    COLORREF fg = t.text;
    COLORREF edge = t.divider;
    COLORREF backdrop = t.shell;
    if (vis == BtnVisual::Primary) {
        if (generating) {
            fill = t.hover;
            fg = t.text;
        } else if (disabled) {
            fill = t.input;
            fg = t.muted;
        } else if (down) {
            fill = t.amber_pressed;
            fg = RGB(0xFF, 0xFF, 0xFF);
        } else if (hot) {
            fill = t.amber_hover;
            fg = RGB(0xFF, 0xFF, 0xFF);
        } else {
            fill = t.amber;
            fg = RGB(0xFF, 0xFF, 0xFF);
        }
        edge = fill;
        backdrop = t.input;
    } else if (vis == BtnVisual::Toggle) {
        fill = toggled ? t.selected : (down ? t.selected : (hot ? t.hover : t.shell));
        fg = t.text;
        edge = toggled ? t.divider : fill;
        backdrop = t.shell;
    } else if (vis == BtnVisual::Option) {
        // Settings boolean: amber fill when on, charcoal when off.
        if (toggled) {
            fill = down ? t.amber_pressed : (hot ? t.amber_hover : t.amber);
            fg = RGB(0xFF, 0xFF, 0xFF);
            edge = fill;
        } else {
            fill = down ? t.selected : (hot ? t.hover : t.raised);
            fg = t.text;
            edge = t.divider;
        }
        backdrop = t.editor;
    } else if (vis == BtnVisual::Icon) {
        fill = hot || down || toggled ? t.hover : t.shell;
        fg = disabled ? t.muted : t.secondary;
        edge = fill;
        backdrop = t.shell;
    } else if (vis == BtnVisual::Selector) {
        fill = down ? t.selected : (hot ? t.hover : t.input);
        fg = disabled ? t.muted : t.text;
        edge = t.edge;
        backdrop = t.shell;
    } else if (vis == BtnVisual::Quiet) {
        fill = down ? t.selected : (hot ? t.hover : t.input);
        fg = disabled ? t.muted : t.secondary;
        edge = fill;
        backdrop = t.input;
    } else {
        fill = down ? t.selected : (hot ? t.hover : t.raised);
        fg = disabled ? t.muted : t.text;
        backdrop = t.work;
    }
    fill_rect(di->hDC, r, backdrop);
    const int rad = vis == BtnVisual::Primary ? (r.right - r.left) : 6;
    RECT inner = r;
    InflateRect(&inner, -1, -1);
    if (vis != BtnVisual::Quiet || hot || down || focus) {
        round_fill(di->hDC, inner, rad, fill, edge);
    } else if (vis == BtnVisual::Quiet) {
        fill_rect(di->hDC, r, t.input);
    }
    wchar_t text[128]{};
    GetWindowTextW(di->hwndItem, text, 128);
    if (generating && vis == BtnVisual::Primary) {
        draw_icon_stop(di->hDC, r.left + (r.right - r.left - 16) / 2, r.top + (r.bottom - r.top - 16) / 2, 16, fg);
    } else if (vis == BtnVisual::Selector) {
        RECT tr = r;
        tr.left += 10;
        tr.right -= 22;
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        SelectObject(di->hDC, font);
        DrawTextW(di->hDC, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_chevron(di->hDC, r.right - 12, (r.top + r.bottom) / 2, t.muted);
    } else if (vis == BtnVisual::Icon || vis == BtnVisual::Primary) {
        const int s = 16;
        const int x = r.left + (r.right - r.left - s) / 2;
        const int y = r.top + (r.bottom - r.top - s) / 2;
        if (vis == BtnVisual::Primary) {
            draw_icon_send(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"Attach") || wcsstr(text, L"paperclip") || wcsstr(text, L"Add file")) {
            draw_icon_paperclip(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"Save")) {
            draw_icon_save(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"Find")) {
            draw_icon_find(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"Open") || wcsstr(text, L"folder")) {
            draw_icon_folder(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"Settings")) {
            draw_icon_gear(di->hDC, x, y, s, fg);
        } else if (wcsstr(text, L"New")) {
            HPEN pen = CreatePen(PS_SOLID, 2, fg);
            HPEN old = static_cast<HPEN>(SelectObject(di->hDC, pen));
            const int cx = (r.left + r.right) / 2;
            const int cy = (r.top + r.bottom) / 2;
            MoveToEx(di->hDC, cx - 5, cy, nullptr);
            LineTo(di->hDC, cx + 5, cy);
            MoveToEx(di->hDC, cx, cy - 5, nullptr);
            LineTo(di->hDC, cx, cy + 5);
            SelectObject(di->hDC, old);
            DeleteObject(pen);
        } else {
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, fg);
            SelectObject(di->hDC, font);
            DrawTextW(di->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else if (vis == BtnVisual::Quiet &&
               (wcsstr(text, L"Attach") || wcsstr(text, L"paperclip") || wcsstr(text, L"Add file"))) {
        const int s = 16;
        const int x = r.left + (r.right - r.left - s) / 2;
        const int y = r.top + (r.bottom - r.top - s) / 2;
        draw_icon_paperclip(di->hDC, x, y, s, fg);
    } else if (vis == BtnVisual::Option) {
        RECT tr = r;
        tr.left += 20;
        tr.right -= 20;
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        SelectObject(di->hDC, font);
        DrawTextW(di->hDC, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (focus && !disabled && !toggled) {
            // Off + focused: thin amber edge only (on-state already amber-filled).
            draw_focus_ring(di->hDC, r, t.amber);
        }
        return;  // skip default focus ring below (on-state shouldn't get double ring)
    } else {
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, fg);
        SelectObject(di->hDC, font);
        DrawTextW(di->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (focus && !disabled) {
        draw_focus_ring(di->hDC, r, t.amber);
    }
}

void draw_combo_item(const DRAWITEMSTRUCT* di, HFONT font, bool closed_face) {
    const Theme& t = theme();
    const bool sel = (di->itemState & ODS_SELECTED) != 0;
    const bool focus = (di->itemState & ODS_FOCUS) != 0;
    COLORREF fill = closed_face ? t.input : (sel ? t.selected : t.menu);
    fill_rect(di->hDC, di->rcItem, fill);
    if (closed_face) {
        round_fill(di->hDC, di->rcItem, 6, fill, t.edge);
    }
    wchar_t buf[256]{};
    if (di->itemID != (UINT)-1) {
        SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, reinterpret_cast<LPARAM>(buf));
    }
    RECT tr = di->rcItem;
    InflateRect(&tr, -12, 0);
    if (closed_face) {
        tr.right -= 10;
    }
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, t.text);
    SelectObject(di->hDC, font);
    DrawTextW(di->hDC, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (closed_face) {
        draw_chevron(di->hDC, di->rcItem.right - 12, (di->rcItem.top + di->rcItem.bottom) / 2, t.muted);
    }
    if (focus) {
        draw_focus_ring(di->hDC, di->rcItem, t.amber);
    }
}

void draw_tab_item(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool active, bool hot, bool closable) {
    const Theme& t = theme();
    // Match agent chat tabs: flat shell fill, amber underline when active (no etched/white edges).
    COLORREF fill = active ? t.raised : (hot ? t.hover : t.shell);
    fill_rect(dc, r, fill);
    if (active) {
        RECT bar{r.left + 8, r.bottom - 2, r.right - (closable ? 22 : 8), r.bottom};
        fill_rect(dc, bar, t.amber);
    }
    RECT tr = r;
    tr.left += 10;
    tr.right -= closable ? 20 : 10;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? t.text : t.secondary);
    SelectObject(dc, font);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (closable) {
        SetTextColor(dc, (active || hot) ? t.secondary : t.muted);
        RECT xr{r.right - 18, r.top, r.right - 4, r.bottom};
        DrawTextW(dc, L"×", -1, &xr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void draw_list_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected, bool empty_hint) {
    const Theme& t = theme();
    fill_rect(dc, r, selected ? t.selected : t.navigation);
    if (selected) {
        RECT mark{r.left, r.top + 8, r.left + 3, r.bottom - 8};
        fill_rect(dc, mark, t.amber);
    }
    RECT tr = r;
    InflateRect(&tr, -12, 0);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, empty_hint ? t.muted : t.text);
    SelectObject(dc, font);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void draw_menu_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected) {
    const Theme& t = theme();
    fill_rect(dc, r, selected ? t.selected : t.menu);
    RECT tr = r;
    InflateRect(&tr, -12, 0);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.text);
    SelectObject(dc, font);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void paint_gutter(HWND gutter, HWND editor, HFONT font) {
    if (!gutter || !editor) {
        return;
    }
    RECT rc{};
    GetClientRect(gutter, &rc);
    HDC dc = GetDC(gutter);
    fill_rect(dc, rc, theme().gutter);
    const int first = static_cast<int>(SendMessageW(editor, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int count = static_cast<int>(SendMessageW(editor, EM_GETLINECOUNT, 0, 0));
    TEXTMETRICW tm{};
    SelectObject(dc, font);
    GetTextMetricsW(dc, &tm);
    const int lh = tm.tmHeight;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme().line_no);
    int y = 4;
    for (int i = first; i < count && y < rc.bottom; ++i) {
        wchar_t buf[16]{};
        wsprintfW(buf, L"%d", i + 1);
        RECT tr{0, y, rc.right - 6, y + lh};
        DrawTextW(dc, buf, -1, &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        y += lh;
    }
    ReleaseDC(gutter, dc);
}

void paint_empty_agent(HDC dc, const RECT& r, HFONT title, HFONT body) {
    const Theme& t = theme();
    fill_rect(dc, r, t.shell);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    auto s = [&](int v) { return MulDiv(v, dpi, 96); };
    const int title_h = s(22);
    const int gap = s(8);
    const int body_h = s(40);
    const int content_h = title_h + gap + body_h;
    const int y0 = r.top + (std::max)(0, static_cast<int>(r.bottom - r.top) - content_h) / 2;
    const int cx = (r.left + r.right) / 2;
    const int w = s(280);
    RECT box{cx - w / 2, y0, cx + w / 2, y0 + title_h};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.text);
    SelectObject(dc, title);
    DrawTextW(dc, L"Work through it here.", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    box.top = box.bottom + gap;
    box.bottom = box.top + body_h;
    SetTextColor(dc, t.muted);
    SelectObject(dc, body);
    DrawTextW(dc, L"Ask in the composer, or attach a file or selection.", -1, &box, DT_CENTER | DT_TOP | DT_WORDBREAK);
}

void paint_empty_chats(HDC dc, const RECT& r, HFONT title, HFONT body) {
    const Theme& t = theme();
    fill_rect(dc, r, t.navigation);
    RECT box = r;
    InflateRect(&box, -16, 0);
    box.top += (r.bottom - r.top) / 5;
    box.bottom = box.top + 22;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.secondary);
    SelectObject(dc, title);
    DrawTextW(dc, L"No chats yet", -1, &box, DT_CENTER | DT_TOP | DT_SINGLELINE);
    box.top += 22;
    box.bottom = box.top + 22;
    SetTextColor(dc, t.muted);
    SelectObject(dc, body);
    DrawTextW(dc, L"Start one in the Agent pane.", -1, &box, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

void paint_empty_editor(HDC dc, const RECT& r, HFONT title, HFONT body) {
    const Theme& t = theme();
    fill_rect(dc, r, t.work);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    auto s = [&](int v) { return MulDiv(v, dpi, 96); };
    // Stack must match empty-editor button placement in window.cpp layout.
    const int mark_h = s(12);
    const int mark_gap = s(12);
    const int title_h = s(28);
    const int title_gap = s(8);
    const int sub_h = s(20);
    const int sub_to_btn = s(16);
    const int btn_h = s(28);
    const int content_h = mark_h + mark_gap + title_h + title_gap + sub_h + sub_to_btn + btn_h;
    const int y0 = r.top + (std::max)(0, static_cast<int>(r.bottom - r.top) - content_h) / 2;
    const int cx = (r.left + r.right) / 2;
    HBRUSH br = CreateSolidBrush(t.amber);
    RECT mark{cx - s(6), y0, cx + s(6), y0 + mark_h};
    FillRect(dc, &mark, br);
    DeleteObject(br);
    RECT box{cx - s(160), y0 + mark_h + mark_gap, cx + s(160), y0 + mark_h + mark_gap + title_h};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.text);
    SelectObject(dc, title);
    DrawTextW(dc, L"Open a file to begin", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    box.top = box.bottom + title_gap;
    box.bottom = box.top + sub_h;
    SetTextColor(dc, t.muted);
    SelectObject(dc, body);
    DrawTextW(dc, L"Editing stays local until you attach context.", -1, &box, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

void paint_composer_chrome(HDC dc, const RECT& r, int radius) {
    const Theme& t = theme();
    if (r.right <= r.left || r.bottom <= r.top) {
        return;
    }
    // Erase corner squares first — GDI RoundRect leaves them as light/white pixels.
    fill_rect(dc, r, t.shell);
    round_fill(dc, r, radius, t.input, t.divider);
}

void paint_image_thumbs(HDC dc, const RECT& r, const std::vector<HBITMAP>& thumbs, int thumb_size, int gap) {
    if (thumbs.empty() || r.right <= r.left) {
        return;
    }
    int x = r.left;
    const int y = r.top + (r.bottom - r.top - thumb_size) / 2;
    for (HBITMAP bmp : thumbs) {
        if (!bmp || x + thumb_size > r.right) {
            break;
        }
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, bmp);
        BITMAP bm{};
        GetObjectW(bmp, sizeof(bm), &bm);
        // Rounded-ish clip via StretchBlt into a filled frame.
        RECT frame{x, y, x + thumb_size, y + thumb_size};
        round_fill(dc, frame, 6, theme().hover, theme().divider);
        const int inset = 2;
        StretchBlt(dc, x + inset, y + inset, thumb_size - inset * 2, thumb_size - inset * 2, mem, 0, 0, bm.bmWidth,
                   bm.bmHeight, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        // Close mark.
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme().muted);
        RECT xr{x + thumb_size - 12, y, x + thumb_size, y + 12};
        DrawTextW(dc, L"×", -1, &xr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += thumb_size + gap;
    }
}

std::wstring breadcrumbs(const std::wstring& path, int max_parts) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (wchar_t ch : path) {
        if (ch == L'\\' || ch == L'/') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    if (parts.empty()) {
        return {};
    }
    const int start = std::max(0, static_cast<int>(parts.size()) - max_parts);
    std::wstring out;
    for (int i = start; i < static_cast<int>(parts.size()); ++i) {
        if (!out.empty()) {
            out += L"  ›  ";
        }
        out += parts[i];
    }
    return out;
}

bool looks_like_cpp(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring e = path.substr(dot);
    CharLowerBuffW(e.data(), static_cast<DWORD>(e.size()));
    return e == L".cpp" || e == L".cc" || e == L".cxx" || e == L".c" || e == L".h" || e == L".hpp" || e == L".hh" ||
           e == L".hxx" || e == L".inl";
}

std::vector<CppSpan> lex_cpp(std::wstring_view text) {
    std::vector<CppSpan> out;
    const std::size_t n = text.size();
    std::size_t i = 0;
    auto push = [&](std::size_t a, std::size_t b, CppKind k) {
        if (b > a) {
            out.push_back({a, b, k});
        }
    };
    while (i < n) {
        const wchar_t ch = text[i];
        if (ch == L'/' && i + 1 < n && text[i + 1] == L'/') {
            std::size_t j = i + 2;
            while (j < n && text[j] != L'\n') {
                ++j;
            }
            push(i, j, CppKind::Comment);
            i = j;
            continue;
        }
        if (ch == L'/' && i + 1 < n && text[i + 1] == L'*') {
            std::size_t j = i + 2;
            while (j + 1 < n && !(text[j] == L'*' && text[j + 1] == L'/')) {
                ++j;
            }
            j = std::min(n, j + 2);
            push(i, j, CppKind::Comment);
            i = j;
            continue;
        }
        if (ch == L'"' || ch == L'\'') {
            const wchar_t q = ch;
            std::size_t j = i + 1;
            while (j < n && text[j] != q) {
                if (text[j] == L'\\' && j + 1 < n) {
                    j += 2;
                } else {
                    ++j;
                }
            }
            if (j < n) {
                ++j;
            }
            push(i, j, CppKind::String);
            i = j;
            continue;
        }
        if (ch == L'#' && (i == 0 || text[i - 1] == L'\n')) {
            std::size_t j = i + 1;
            while (j < n && text[j] != L'\n') {
                ++j;
            }
            push(i, j, CppKind::Preproc);
            i = j;
            continue;
        }
        if (iswdigit(ch)) {
            std::size_t j = i + 1;
            while (j < n && (iswxdigit(text[j]) || text[j] == L'.' || text[j] == L'x' || text[j] == L'X')) {
                ++j;
            }
            push(i, j, CppKind::Number);
            i = j;
            continue;
        }
        if (iswalpha(ch) || ch == L'_') {
            std::size_t j = i + 1;
            while (j < n && (iswalnum(text[j]) || text[j] == L'_')) {
                ++j;
            }
            const std::wstring word(text.substr(i, j - i));
            push(i, j, kKeywords.count(word) ? CppKind::Keyword : CppKind::Ident);
            i = j;
            continue;
        }
        if (iswspace(ch)) {
            ++i;
            continue;
        }
        push(i, i + 1, CppKind::Punct);
        ++i;
    }
    return out;
}

void colorize_cpp(HWND edit, HFONT mono) {
    if (!edit) {
        return;
    }
    GETTEXTLENGTHEX gtl{};
    gtl.flags = GTL_DEFAULT | GTL_PRECISE;
    gtl.codepage = 1200;
    const LRESULT n = SendMessageW(edit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0);
    if (n <= 0 || n > 400000) {
        return;
    }
    std::wstring text(static_cast<std::size_t>(n) + 1, 0);
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>((text.size()) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    SendMessageW(edit, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(text.data()));
    text.resize(wcsnlen(text.c_str(), text.size()));
    CHARRANGE keep{};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&keep));
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_BOLD;
    cf.crTextColor = theme().ident;
    lstrcpynW(cf.szFaceName, L"Cascadia Mono", LF_FACESIZE);
    cf.yHeight = 210;
    CHARRANGE all{0, -1};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    (void)mono;
    for (const auto& sp : lex_cpp(text)) {
        CHARRANGE cr{static_cast<LONG>(sp.begin), static_cast<LONG>(sp.end)};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        cf.crTextColor = token_color(sp.kind);
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&keep));
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, FALSE);
}

}  // namespace scyllagpt
