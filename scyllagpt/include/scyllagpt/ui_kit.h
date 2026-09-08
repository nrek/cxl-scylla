#pragma once

// Shared Scylla UI kit — one component system; no feature-local OS chrome.
// Factories never use WS_EX_CLIENTEDGE or default push-button chrome.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "scyllagpt/theme.h"

#include <string>
#include <vector>

namespace scyllagpt {
namespace ui_kit {

// Custom messages for ScyllaSelect face HWNDs.
constexpr UINT WM_SK_GETSEL = WM_USER + 0x510;
constexpr UINT WM_SK_SETSEL = WM_USER + 0x511;
constexpr UINT WM_SK_GETDATA = WM_USER + 0x512;  // returns item user data (LPARAM)
constexpr UINT WM_SK_REBUILDLIST = WM_USER + 0x513;

// Posted to a kit field's parent when Enter / Esc is pressed inside it.
// wParam = control id of the field, lParam = field HWND.
constexpr UINT WM_SK_FIELD_SUBMIT = WM_USER + 0x514;
constexpr UINT WM_SK_FIELD_CANCEL = WM_USER + 0x515;

enum class ButtonKind { Primary, Secondary, Ghost, Danger, Icon };

struct SelectItem {
    std::wstring label;
    LPARAM data = 0;
};

HWND create_button(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, ButtonKind kind, HFONT font);
HWND create_text_field(HWND parent, HINSTANCE inst, UINT id, HFONT font, bool password = false);
// Scrollable, read-only multiline document body. Geometry is inset on resize.
HWND create_document_view(HWND parent, HINSTANCE inst, UINT id, HFONT font);
HWND create_markdown_view(HWND parent, HINSTANCE inst, UINT id, HFONT font);
void append_markdown(HWND view, const std::wstring& text);
void set_markdown(HWND view, const std::wstring& text);
std::wstring markdown_link_at(HWND view, long position);
void trim_markdown_links(HWND view, long position);
HWND create_path_field(HWND parent, HINSTANCE inst, UINT id, HFONT font);

// Re-apply single-line field formatting rect (vertical center + edge inset). Call after MoveWindow.
void center_field_text(HWND edit);

// Cue-banner text shown while a field is empty. Use where a visible label would not fit; a bare
// unlabeled box is never acceptable.
void set_placeholder(HWND edit, const wchar_t* text);

// Put |controls| in this z-order so Tab walks them in visual order. Win32 tab order is z-order,
// which otherwise follows creation order and jumps around a screen that was built out of sequence.
void set_tab_order(const std::vector<HWND>& controls);
HWND create_static(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font, bool muted = false);
HWND create_checkbox(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font);
HWND create_switch(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font);
HWND create_radio(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font, bool first_in_group);
HWND create_list(HWND parent, HINSTANCE inst, UINT id, HFONT font, bool owner_draw = true);
HWND create_entity_list(HWND parent, HINSTANCE inst, UINT id, HFONT font);

// Custom Select: closed face is owner-draw button; popup is WS_POPUP list.
HWND create_select(HWND parent, HINSTANCE inst, UINT id, HFONT font);
void select_set_items(HWND select, const std::vector<SelectItem>& items);
int select_get_index(HWND select);
LPARAM select_get_data(HWND select);
void select_set_index(HWND select, int index);
void select_set_by_data(HWND select, LPARAM data);
bool select_handle_command(HWND select, WORD notify);  // BN_CLICKED → toggle popup
bool select_draw_item(const DRAWITEMSTRUCT* di);       // face + popup list items
void select_close_all();

void style_scroll_host(HWND hwnd, COLORREF track);
void paint_field_border(HWND hwnd, HDC dc, bool focus, bool error = false);
void paint_empty_state(HDC dc, const RECT& r, HFONT title_font, HFONT body_font, const wchar_t* title,
                       const wchar_t* body);
void paint_page_header(HDC dc, const RECT& r, HFONT title_font, HFONT body_font, const wchar_t* title,
                       const wchar_t* desc);
void paint_status_badge(HDC dc, const RECT& r, HFONT font, const wchar_t* text, COLORREF fg, COLORREF bg);
void paint_nav_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected, bool hot);
void paint_entity_row(HDC dc, const RECT& r, HFONT font, const wchar_t* primary, const wchar_t* secondary,
                      bool selected, bool hot);
void draw_checkbox_glyph(HDC dc, int x, int y, int s, bool checked, bool hot, bool disabled);
void draw_switch_glyph(HDC dc, int x, int y, int w, int h, bool on, bool hot, bool disabled);

// Owner-draw routing for kit-created controls (buttons, checkbox, switch, radio, select face).
bool draw_kit_item(const DRAWITEMSTRUCT* di, HFONT font);

// One destructive-confirmation style for the whole app: Yes/No, warning icon, No as default.
// |object| names what is being acted on so the prompt is never ambiguous.
bool confirm_destructive(HWND parent, const wchar_t* verb, const wchar_t* object, const wchar_t* consequence);

// Report a failed persist so changes never appear to apply and then vanish on restart.
void report_save_failure(HWND parent, const wchar_t* what, const std::wstring& path);

// Modal host: dark child overlay with title + body area. Caller creates children on host.
HWND create_modal_host(HWND parent, HINSTANCE inst, UINT id, const wchar_t* title, HFONT font, int width_dip,
                       int height_dip);
void destroy_modal_host(HWND modal);

// Apply kit chrome after create (font, dark theme, no client edge).
void apply_control_chrome(HWND hwnd, HFONT font);

}  // namespace ui_kit
}  // namespace scyllagpt
