#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

// Central design tokens (authority: universal UI rebuild plan). Feature UIs must not hard-code RGB.
struct Theme {
    // Surfaces
    COLORREF app_bg = RGB(0x0C, 0x0F, 0x12);
    COLORREF panel = RGB(0x10, 0x13, 0x17);
    COLORREF surface = RGB(0x14, 0x18, 0x1D);
    COLORREF surface_hover = RGB(0x19, 0x1E, 0x24);
    COLORREF surface_active = RGB(0x1D, 0x23, 0x2A);
    COLORREF input_bg = RGB(0x0F, 0x13, 0x17);
    COLORREF overlay = RGB(0x17, 0x1B, 0x20);

    // Legacy aliases (map onto token roles so existing call sites stay coherent)
    COLORREF shell = RGB(0x0C, 0x0F, 0x12);
    COLORREF navigation = RGB(0x10, 0x13, 0x17);
    COLORREF work = RGB(0x14, 0x18, 0x1D);
    COLORREF editor = RGB(0x14, 0x18, 0x1D);
    COLORREF agent = RGB(0x14, 0x18, 0x1D);
    COLORREF input = RGB(0x0F, 0x13, 0x17);
    COLORREF raised = RGB(0x19, 0x1E, 0x24);
    COLORREF hover = RGB(0x19, 0x1E, 0x24);
    COLORREF selected = RGB(0x1D, 0x23, 0x2A);
    COLORREF menu = RGB(0x17, 0x1B, 0x20);

    // Borders
    COLORREF border_subtle = RGB(0x26, 0x2C, 0x33);
    COLORREF border_default = RGB(0x30, 0x37, 0x40);
    COLORREF border_strong = RGB(0x41, 0x4A, 0x55);
    COLORREF divider = RGB(0x30, 0x37, 0x40);
    COLORREF edge = RGB(0x30, 0x37, 0x40);

    // Text
    COLORREF text = RGB(0xD7, 0xDC, 0xE2);
    COLORREF secondary = RGB(0xA2, 0xAB, 0xB5);
    COLORREF muted = RGB(0x72, 0x7C, 0x87);
    COLORREF disabled_text = RGB(0x50, 0x59, 0x63);

    // Accent
    COLORREF amber = RGB(0xE6, 0x94, 0x05);
    COLORREF amber_hover = RGB(0xF0, 0xA1, 0x16);
    COLORREF amber_pressed = RGB(0xC7, 0x7F, 0x04);
    COLORREF amber_muted = RGB(0x6E, 0x4C, 0x0A);
    COLORREF amber_text = RGB(0xEF, 0xB6, 0x5B);
    COLORREF on_amber = RGB(0x17, 0x13, 0x0B);

    // Semantic
    COLORREF success = RGB(0x5F, 0xBF, 0x8A);
    COLORREF warning = RGB(0xD6, 0xA8, 0x4F);
    COLORREF danger = RGB(0xD8, 0x6C, 0x72);
    COLORREF info = RGB(0x6F, 0xA9, 0xD8);
    COLORREF error = RGB(0xD8, 0x6C, 0x72);

    // Scrollbar
    COLORREF scroll_thumb = RGB(0x34, 0x3B, 0x44);
    COLORREF scroll_thumb_hot = RGB(0x46, 0x50, 0x5B);
    COLORREF scroll_thumb_active = RGB(0x5A, 0x65, 0x71);

    // Syntax (editor)
    COLORREF ident = RGB(0xC8, 0xCD, 0xD4);
    COLORREF keyword = RGB(0xA8, 0x9C, 0xC8);
    COLORREF string_lit = RGB(0x92, 0xB0, 0x86);
    COLORREF comment = RGB(0x7E, 0x87, 0x94);
    COLORREF number = RGB(0xC9, 0xA4, 0x80);
    COLORREF preproc = RGB(0xC4, 0xB0, 0x7A);
    COLORREF punct = RGB(0xA8, 0xB0, 0xBA);
    COLORREF gutter = RGB(0x14, 0x18, 0x1D);
    COLORREF line_no = RGB(0x72, 0x7C, 0x87);
    COLORREF current_line = RGB(0x19, 0x1E, 0x24);
    COLORREF sel_bg = RGB(0x3A, 0x48, 0x58);
};

const Theme& theme();
bool high_contrast_on();
void apply_dark_caption(HWND hwnd);
void apply_dark_menus(HWND hwnd);  // native HMENU popups (Win10+)
// Custom-draw the menubar strip (ForceDark does not recolor the bar itself).
bool handle_dark_menubar_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT* result);
void apply_dark_child(HWND hwnd);
// Shared owner-drawn Scylla scrollbars. The controller owns hover, drag, paging,
// and paint while the host control retains its native content scroll range.
void install_thin_scrollbar(HWND hwnd, COLORREF track);
void refresh_thin_scrollbar(HWND hwnd);
void fill_rect(HDC dc, const RECT& r, COLORREF c);
void round_fill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF edge);
void draw_focus_ring(HDC dc, const RECT& r, COLORREF c);
void draw_chevron(HDC dc, int cx, int cy, COLORREF c);

enum class BtnVisual { Secondary, Icon, Primary, Toggle, Selector, Quiet, Option, Ghost, Danger };

void draw_themed_button(const DRAWITEMSTRUCT* di, HFONT font, BtnVisual vis, bool toggled, bool generating);
void draw_combo_item(const DRAWITEMSTRUCT* di, HFONT font, bool closed_face);
void draw_tab_item(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool active, bool hot,
                   bool closable = true);
void draw_list_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected, bool empty_hint);
void draw_menu_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected);
void paint_gutter(HWND gutter, HWND editor, HFONT font);
void paint_empty_agent(HDC dc, const RECT& r, HFONT title, HFONT body);
void paint_empty_chats(HDC dc, const RECT& r, HFONT title, HFONT body);
void paint_empty_editor(HDC dc, const RECT& r, HFONT title, HFONT body);
void paint_composer_chrome(HDC dc, const RECT& r, int radius);
void paint_image_thumbs(HDC dc, const RECT& r, const std::vector<HBITMAP>& thumbs, int thumb_size, int gap);

std::wstring breadcrumbs(const std::wstring& path, int max_parts = 4);
bool looks_like_cpp(const std::wstring& path);

enum class CppKind { Ident, Keyword, String, Comment, Number, Preproc, Punct, Other };

struct CppSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    CppKind kind = CppKind::Other;
};

std::vector<CppSpan> lex_cpp(std::wstring_view text);
void colorize_cpp(HWND edit, HFONT mono);

}  // namespace scyllagpt
