#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

struct Theme {
    COLORREF shell = RGB(0x14, 0x16, 0x18);
    COLORREF navigation = RGB(0x18, 0x1A, 0x1D);
    COLORREF work = RGB(0x1C, 0x1E, 0x21);
    COLORREF editor = RGB(0x1C, 0x1E, 0x21);
    COLORREF agent = RGB(0x1C, 0x1E, 0x21);
    COLORREF input = RGB(0x23, 0x26, 0x2A);
    COLORREF raised = RGB(0x23, 0x26, 0x2A);
    COLORREF hover = RGB(0x2B, 0x2E, 0x33);
    COLORREF selected = RGB(0x33, 0x37, 0x3D);
    COLORREF menu = RGB(0x22, 0x25, 0x29);
    COLORREF divider = RGB(0x30, 0x33, 0x38);
    COLORREF edge = RGB(0x72, 0x7C, 0x88);
    COLORREF text = RGB(0xE4, 0xE7, 0xEB);
    COLORREF secondary = RGB(0xB1, 0xB7, 0xC0);
    COLORREF muted = RGB(0x94, 0x9D, 0xA8);
    COLORREF amber = RGB(0xE6, 0x94, 0x05);
    COLORREF amber_hover = RGB(0xF0, 0xA8, 0x20);
    COLORREF amber_pressed = RGB(0xC7, 0x7F, 0x04);
    COLORREF amber_text = RGB(0xEF, 0xB6, 0x5B);
    COLORREF on_amber = RGB(0x17, 0x13, 0x0B);
    COLORREF success = RGB(0x89, 0xBC, 0x9D);
    COLORREF error = RGB(0xEF, 0x96, 0x96);
    COLORREF ident = RGB(0xC8, 0xCD, 0xD4);
    COLORREF keyword = RGB(0xA8, 0x9C, 0xC8);
    COLORREF string_lit = RGB(0x92, 0xB0, 0x86);
    COLORREF comment = RGB(0x7E, 0x87, 0x94);
    COLORREF number = RGB(0xC9, 0xA4, 0x80);
    COLORREF preproc = RGB(0xC4, 0xB0, 0x7A);
    COLORREF punct = RGB(0xA8, 0xB0, 0xBA);
    COLORREF gutter = RGB(0x1C, 0x1E, 0x21);
    COLORREF line_no = RGB(0x7A, 0x82, 0x8C);
    COLORREF current_line = RGB(0x22, 0x25, 0x29);
    COLORREF sel_bg = RGB(0x3A, 0x48, 0x58);
};

const Theme& theme();
bool high_contrast_on();
void apply_dark_caption(HWND hwnd);
void apply_dark_menus(HWND hwnd);  // native HMENU popups (Win10+)
// Custom-draw the menubar strip (ForceDark does not recolor the bar itself).
bool handle_dark_menubar_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT* result);
void apply_dark_child(HWND hwnd);
// Flat thin overlay scrollbars (VS Code–like thumb). track = panel fill behind the bar.
void install_thin_scrollbar(HWND hwnd, COLORREF track);
void refresh_thin_scrollbar(HWND hwnd);
void fill_rect(HDC dc, const RECT& r, COLORREF c);
void round_fill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF edge);
void draw_focus_ring(HDC dc, const RECT& r, COLORREF c);
void draw_chevron(HDC dc, int cx, int cy, COLORREF c);

enum class BtnVisual { Secondary, Icon, Primary, Toggle, Selector, Quiet, Option };

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
