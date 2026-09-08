#include "scyllagpt/ui_kit.h"

#include "scyllagpt/ui_space.h"
#include "scyllagpt/markdown.h"
#include <richedit.h>

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace scyllagpt {
namespace ui_kit {
namespace {
struct MarkdownLink { long first, last; std::wstring target; };
std::map<HWND, std::vector<MarkdownLink>> markdown_links;
LRESULT CALLBACK markdown_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_SIZE) {
        const auto result = DefSubclassProc(h, msg, wp, lp);
        RECT r{}; GetClientRect(h, &r);
        const int inset = MulDiv(14, GetDpiForWindow(h), 96);
        InflateRect(&r, -inset, -inset);
        SendMessageW(h, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&r));
        return result;
    }
    if (msg == WM_NCDESTROY) { markdown_links.erase(h); RemoveWindowSubclass(h, markdown_subclass, id); }
    if (msg == WM_SETTEXT) markdown_links[h].clear();
    return DefSubclassProc(h, msg, wp, lp);
}
}
void trim_markdown_links(HWND view, long position) {
    auto& links = markdown_links[view];
    std::erase_if(links, [&](const auto& link) { return link.last > position; });
}
std::wstring markdown_link_at(HWND view, long position) {
    const auto found = markdown_links.find(view);
    if (found != markdown_links.end()) for (const auto& link : found->second)
        if (position >= link.first && position < link.last) return link.target;
    return {};
}
void append_markdown(HWND view, const std::wstring& text) {
    SetWindowSubclass(view, markdown_subclass, 0x534D4456, 0);
    SendMessageW(view, EM_SETEVENTMASK, 0, SendMessageW(view, EM_GETEVENTMASK, 0, 0) | ENM_LINK);
    const auto runs = parse_markdown(text);
    CHARRANGE end{-1, -1}; SendMessageW(view, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
    for (const auto& run : runs) {
        CHARRANGE range{}; SendMessageW(view, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC | CFM_STRIKEOUT | CFM_LINK | CFM_UNDERLINE | CFM_FACE | CFM_SIZE;
        cf.crTextColor = run.link.empty() ? theme().text : theme().amber_text;
        cf.crBackColor = theme().input;
        cf.dwEffects = (run.bold ? CFE_BOLD : 0) | (run.italic ? CFE_ITALIC : 0) | (run.strike ? CFE_STRIKEOUT : 0) |
            (run.link.empty() ? 0 : CFE_LINK | CFE_UNDERLINE) | (run.code ? 0 : CFE_AUTOBACKCOLOR);
        cf.yHeight = (run.heading ? 22 - run.heading * 2 : 11) * 20;
        lstrcpynW(cf.szFaceName, run.code ? L"Consolas" : L"Segoe UI", LF_FACESIZE);
        SendMessageW(view, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
        SendMessageW(view, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(run.text.c_str()));
        CHARRANGE after{}; SendMessageW(view, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
        if (!run.link.empty()) markdown_links[view].push_back({range.cpMin, after.cpMin, run.link});
    }
}
void set_markdown(HWND view, const std::wstring& text) {
    SendMessageW(view, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(view, L""); markdown_links[view].clear();
    append_markdown(view, text);
    SendMessageW(view, EM_SETSEL, 0, 0);
    SendMessageW(view, WM_VSCROLL, SB_TOP, 0);
    SendMessageW(view, WM_SETREDRAW, TRUE, 0); InvalidateRect(view, nullptr, TRUE);
}
HWND create_markdown_view(HWND parent, HINSTANCE inst, UINT id, HFONT font) {
    LoadLibraryW(L"Msftedit.dll");
    HWND h = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font); style_scroll_host(h, theme().panel);
    SendMessageW(h, EM_SETBKGNDCOLOR, 0, theme().panel);
    SendMessageW(h, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
    SetWindowSubclass(h, markdown_subclass, 0x534D4456, 0);
    return h;
}

void set_placeholder(HWND edit, const wchar_t* text) {
    if (!edit || !IsWindow(edit) || !text) {
        return;
    }
    // TRUE = keep the banner visible while the field has focus, so the hint survives the click that
    // put the caret there.
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text));
}

void set_tab_order(const std::vector<HWND>& controls) {
    HWND prev = nullptr;
    for (HWND h : controls) {
        if (!h || !IsWindow(h)) {
            continue;
        }
        if (prev) {
            // Insert h directly behind prev; repeating this walks the list into visual order.
            SetWindowPos(h, prev, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        prev = h;
    }
}

void center_field_text(HWND edit) {
    if (!edit || !IsWindow(edit)) {
        return;
    }
    // Only kit single-line fields (ES_MULTILINE used solely so EM_SETRECT applies).
    const LONG style = GetWindowLongW(edit, GWL_STYLE);
    if ((style & ES_MULTILINE) == 0) {
        return;
    }
    // True multi-line editors (WantReturn / AutovScroll) keep natural top padding inset only.
    if ((style & ES_WANTRETURN) != 0 || (style & ES_AUTOVSCROLL) != 0) {
        RECT cr{};
        GetClientRect(edit, &cr);
        if (cr.right <= cr.left || cr.bottom <= cr.top) {
            return;
        }
        const int pad_x = ui_space::dip(edit, 8);
        const int pad_y = ui_space::dip(edit, 4);
        RECT fr{pad_x, pad_y, cr.right - pad_x, cr.bottom - pad_y};
        if (fr.right > fr.left && fr.bottom > fr.top) {
            SendMessageW(edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&fr));
        }
        return;
    }

    RECT cr{};
    GetClientRect(edit, &cr);
    if (cr.right <= cr.left || cr.bottom <= cr.top) {
        return;
    }
    HDC dc = GetDC(edit);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    HFONT old = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) {
        SelectObject(dc, old);
    }
    ReleaseDC(edit, dc);

    const int pad_x = ui_space::dip(edit, 8);       // never touch left/right border
    const int min_pad_y = ui_space::dip(edit, 2);   // never touch top/bottom border
    const int text_h = tm.tmHeight;
    const int box_h = cr.bottom - cr.top;
    int top = (box_h - text_h) / 2;
    if (top < min_pad_y) {
        top = min_pad_y;
    }
    // Symmetric bottom inset (= top) so caret sits on the optical mid-line.
    RECT fr{pad_x, top, cr.right - pad_x, box_h - top};
    if (fr.bottom <= fr.top + text_h / 2) {
        fr.top = min_pad_y;
        fr.bottom = (std::max)(fr.top + 1, static_cast<LONG>(box_h - min_pad_y));
    }
    SendMessageW(edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&fr));
}

namespace {

constexpr UINT_PTR kFieldSubclassId = 0x534B4644;  // 'SKFD'
constexpr UINT_PTR kCheckSubclassId = 0x534B434B;  // 'SKCK'
constexpr wchar_t kSelectProp[] = L"ScyllaSelectPtr";
constexpr wchar_t kModalProp[] = L"ScyllaModal";

struct SelectState {
    HWND face = nullptr;
    HWND popup = nullptr;
    HWND parent = nullptr;
    HFONT font = nullptr;
    std::vector<SelectItem> items;
    int sel = -1;
    bool open = false;
};

std::map<HWND, std::unique_ptr<SelectState>> g_selects;
HWND g_open_select = nullptr;

BtnVisual button_kind_to_visual(ButtonKind k) {
    switch (k) {
        case ButtonKind::Primary:
            return BtnVisual::Primary;
        case ButtonKind::Ghost:
            return BtnVisual::Ghost;
        case ButtonKind::Danger:
            return BtnVisual::Danger;
        case ButtonKind::Icon:
            return BtnVisual::Icon;
        case ButtonKind::Secondary:
        default:
            return BtnVisual::Secondary;
    }
}

LRESULT CALLBACK field_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_SIZE:
        case WM_SETFONT: {
            const LRESULT r = DefSubclassProc(hwnd, msg, wparam, lparam);
            center_field_text(hwnd);
            return r;
        }
        case WM_CHAR:
            // Single-line field (ES_MULTILINE only for EM_SETRECT centering) — never insert CR,
            // and swallow Esc so the control does not beep.
            if (wparam == VK_RETURN || wparam == VK_ESCAPE) {
                return 0;
            }
            break;
        case WM_KEYDOWN:
            // Enter submits the surrounding form, Esc cancels it. The host turns these into the
            // owning panel's default / cancel command; previously both keys did nothing at all.
            if (wparam == VK_RETURN || wparam == VK_ESCAPE) {
                HWND parent = GetParent(hwnd);
                if (parent) {
                    const UINT note = (wparam == VK_RETURN) ? WM_SK_FIELD_SUBMIT : WM_SK_FIELD_CANCEL;
                    PostMessageW(parent, note, static_cast<WPARAM>(GetDlgCtrlID(hwnd)),
                                 reinterpret_cast<LPARAM>(hwnd));
                }
                return 0;
            }
            break;
        case WM_GETDLGCODE: {
            const LRESULT base = DefSubclassProc(hwnd, msg, wparam, lparam);
            // WANTALLKEYS so Esc/Enter reach this subclass instead of the accelerator table.
            return base | DLGC_HASSETSEL | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTALLKEYS;
        }
        case WM_NCPAINT:
        case WM_PAINT: {
            const LRESULT r = DefSubclassProc(hwnd, msg, wparam, lparam);
            HDC dc = GetWindowDC(hwnd);
            if (dc) {
                const bool focus = GetFocus() == hwnd;
                paint_field_border(hwnd, dc, focus, false);
                ReleaseDC(hwnd, dc);
            }
            return r;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, field_subclass, kFieldSubclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK document_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_SIZE || msg == WM_SETFONT) {
        const auto result = DefSubclassProc(hwnd, msg, wparam, lparam);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const int pad = MulDiv(10, static_cast<int>(GetDpiForWindow(hwnd)), 96);
        InflateRect(&rect, -pad, -pad);
        SendMessageW(hwnd, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rect));
        return result;
    }
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessageW(GetParent(hwnd), WM_SK_FIELD_CANCEL, GetDlgCtrlID(hwnd), reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, document_subclass, id);
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

bool check_prop_checked(HWND hwnd) {
    return GetPropW(hwnd, L"ScyllaChecked") != nullptr;
}

void check_prop_set(HWND hwnd, bool checked) {
    if (checked) {
        SetPropW(hwnd, L"ScyllaChecked", reinterpret_cast<HANDLE>(1));
    } else {
        RemovePropW(hwnd, L"ScyllaChecked");
    }
}

void check_notify_clicked(HWND hwnd) {
    // Post — never Send. Sync WM_COMMAND often re-enters layout/hide_all while still inside
    // WM_LBUTTONDOWN and drops the rest of the click on Settings panels.
    HWND parent = GetParent(hwnd);
    if (parent) {
        PostMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
    }
}

void paint_check_control(HWND hwnd, HDC dc, DWORD_PTR kind) {
    RECT r{};
    GetClientRect(hwnd, &r);
    if (r.right <= r.left || r.bottom <= r.top) {
        return;
    }
    const Theme& t = theme();
    // Match Settings panel — no raised label strip.
    fill_rect(dc, r, t.panel);
    const bool checked = check_prop_checked(hwnd);
    const bool hot = false;
    const bool disabled = !IsWindowEnabled(hwnd);
    wchar_t text[256]{};
    GetWindowTextW(hwnd, text, 256);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    if (font) {
        SelectObject(dc, font);
    }
    const int s = ui_space::dip(hwnd, 16);
    const int box_h = r.bottom - r.top;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? t.disabled_text : t.text);
    if (kind == 1) {
        const int sw = ui_space::dip(hwnd, 36);
        const int sh = ui_space::dip(hwnd, 18);
        const int gy = r.top + (std::max)(0, (box_h - sh) / 2);
        draw_switch_glyph(dc, r.left, gy, sw, sh, checked, hot, disabled);
        RECT tr{r.left + sw + ui_space::dip(hwnd, 8), r.top, r.right, r.bottom};
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        const int gy = r.top + (std::max)(0, (box_h - s) / 2);
        draw_checkbox_glyph(dc, r.left, gy, s, checked, hot, disabled);
        RECT tr{r.left + s + ui_space::dip(hwnd, 8), r.top, r.right, r.bottom};
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

LRESULT CALLBACK check_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR kind) {
    // kind: 0 checkbox, 1 switch, 2 radio
    // BS_OWNERDRAW buttons do not keep BM_* check state — we own it in ScyllaChecked.
    switch (msg) {
        case BM_SETCHECK:
            check_prop_set(hwnd, wparam == BST_CHECKED);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case BM_GETCHECK:
            return check_prop_checked(hwnd) ? BST_CHECKED : BST_UNCHECKED;
        case WM_ERASEBKGND:
            // Paint here too — owner-draw often gets erase without a follow-up WM_PAINT, which left
            // Knowledge form checkboxes blank until the first click forced InvalidateRect.
            paint_check_control(hwnd, reinterpret_cast<HDC>(wparam), kind);
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paint_check_control(hwnd, dc, kind);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            paint_check_control(hwnd, reinterpret_cast<HDC>(wparam), kind);
            return 0;
        case WM_SHOWWINDOW:
            if (wparam) {
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        case WM_WINDOWPOSCHANGED:
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            if (!IsWindowEnabled(hwnd)) {
                break;
            }
            if (kind == 2) {
                // Radio: check this, clear others in the same WS_GROUP run only.
                check_prop_set(hwnd, true);
                InvalidateRect(hwnd, nullptr, TRUE);
                HWND start = hwnd;
                for (HWND p = GetWindow(hwnd, GW_HWNDPREV); p; p = GetWindow(p, GW_HWNDPREV)) {
                    if (reinterpret_cast<INT_PTR>(GetPropW(p, L"ScyllaCheckKind")) != 2) {
                        continue;
                    }
                    start = p;
                    if ((GetWindowLongW(p, GWL_STYLE) & WS_GROUP) != 0) {
                        break;
                    }
                }
                for (HWND s = start; s; s = GetWindow(s, GW_HWNDNEXT)) {
                    if (s != start && (GetWindowLongW(s, GWL_STYLE) & WS_GROUP) != 0) {
                        break;
                    }
                    if (s == hwnd || reinterpret_cast<INT_PTR>(GetPropW(s, L"ScyllaCheckKind")) != 2) {
                        continue;
                    }
                    check_prop_set(s, false);
                    InvalidateRect(s, nullptr, TRUE);
                }
                check_notify_clicked(hwnd);
                return 0;
            }
            check_prop_set(hwnd, !check_prop_checked(hwnd));
            InvalidateRect(hwnd, nullptr, TRUE);
            check_notify_clicked(hwnd);
            return 0;
        }
        case WM_KEYDOWN:
            if ((wparam == VK_SPACE || wparam == VK_RETURN) && IsWindowEnabled(hwnd) && kind != 2) {
                check_prop_set(hwnd, !check_prop_checked(hwnd));
                InvalidateRect(hwnd, nullptr, TRUE);
                check_notify_clicked(hwnd);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemovePropW(hwnd, L"ScyllaChecked");
            RemoveWindowSubclass(hwnd, check_subclass, kCheckSubclassId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

HWND select_popup_list(HWND popup) {
    return popup ? GetWindow(popup, GW_CHILD) : nullptr;
}

void select_commit_from_list(SelectState* st, HWND list) {
    if (!st || !list) {
        return;
    }
    const int idx = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (idx < 0) {
        return;
    }
    st->sel = idx;
    InvalidateRect(st->face, nullptr, TRUE);
    HWND parent = st->parent;
    const UINT id = static_cast<UINT>(GetDlgCtrlID(st->face));
    select_close_all();
    if (parent) {
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(st->face));
    }
}

void paint_select_popup_frame(HWND hwnd, HDC dc) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const Theme& t = theme();
    fill_rect(dc, r, t.overlay);
    HPEN pen = CreatePen(PS_SOLID, 1, t.border_default);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    // Manual edges — avoid GDI Rectangle bottom/right quirks and DWM light fringe.
    MoveToEx(dc, r.left, r.top, nullptr);
    LineTo(dc, r.right - 1, r.top);
    LineTo(dc, r.right - 1, r.bottom - 1);
    LineTo(dc, r.left, r.bottom - 1);
    LineTo(dc, r.left, r.top);
    SelectObject(dc, old);
    DeleteObject(pen);
}

LRESULT CALLBACK select_popup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    SelectState* st = reinterpret_cast<SelectState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCPAINT:
        case WM_NCCALCSIZE:
            // Client-only popup — no system non-client chrome (avoids Win11 white bottom edge).
            if (msg == WM_NCCALCSIZE) {
                return 0;
            }
            return 0;
        case WM_ERASEBKGND: {
            paint_select_popup_frame(hwnd, reinterpret_cast<HDC>(wparam));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paint_select_popup_frame(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            const auto* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (!di || !st) {
                break;
            }
            const Theme& t = theme();
            const bool hot = (di->itemState & ODS_SELECTED) != 0;
            fill_rect(di->hDC, di->rcItem, hot ? t.surface_active : t.overlay);
            if (di->itemID < st->items.size()) {
                const bool is_current = static_cast<int>(di->itemID) == st->sel;
                const int s = ui_space::dip(hwnd, 14);
                const int gx = di->rcItem.left + ui_space::dip(hwnd, 10);
                const int gy = di->rcItem.top + (di->rcItem.bottom - di->rcItem.top - s) / 2;
                draw_checkbox_glyph(di->hDC, gx, gy, s, is_current, false, false);

                RECT tr = di->rcItem;
                tr.left = gx + s + ui_space::dip(hwnd, 8);
                tr.right -= ui_space::dip(hwnd, 10);
                SetBkMode(di->hDC, TRANSPARENT);
                SetTextColor(di->hDC, t.text);
                if (st->font) {
                    SelectObject(di->hDC, st->font);
                }
                DrawTextW(di->hDC, st->items[di->itemID].label.c_str(), -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (mi) {
                mi->itemHeight = ui_space::dip(hwnd, ui_space::kRowHDip);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wparam) == LBN_SELCHANGE && st) {
                HWND list = reinterpret_cast<HWND>(lparam);
                if (!list || !IsWindow(list)) {
                    list = select_popup_list(hwnd);
                }
                select_commit_from_list(st, list);
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            // Close only when focus leaves the popup tree (not when moving to the list child).
            HWND next = reinterpret_cast<HWND>(wparam);
            if (st && next && (next == st->popup || IsChild(st->popup, next) || next == st->face)) {
                return 0;
            }
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        case WM_CLOSE:
            select_close_all();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE) {
                HWND next = reinterpret_cast<HWND>(lparam);
                if (st && next && (next == st->face || IsChild(st->popup, next))) {
                    return 0;
                }
                select_close_all();
            }
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                select_close_all();
                if (st && st->face) {
                    SetFocus(st->face);
                }
                return 0;
            }
            if (wparam == VK_RETURN && st) {
                select_commit_from_list(st, select_popup_list(hwnd));
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ensure_select_popup_class(HINSTANCE) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = select_popup_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ScyllaSelectPopup";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);
    registered = true;
}

void open_select(SelectState* st) {
    if (!st || !st->face || !IsWindowEnabled(st->face)) {
        return;
    }
    select_close_all();
    ensure_select_popup_class(nullptr);
    RECT fr{};
    GetWindowRect(st->face, &fr);
    const int row_h = ui_space::dip(st->face, ui_space::kRowHDip);
    const int max_rows = (std::min)(8, (std::max)(1, static_cast<int>(st->items.size())));
    const int pop_h = row_h * max_rows + 4;
    const int pop_w = (std::max)(static_cast<int>(fr.right - fr.left), ui_space::dip(st->face, 160));

    st->popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ScyllaSelectPopup", L"", WS_POPUP | WS_CLIPCHILDREN,
                                fr.left, fr.bottom + 2, pop_w, pop_h, st->parent, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
    SetWindowLongPtrW(st->popup, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    SetWindowTheme(st->popup, L"", L"");
    {
        // Kill Win11 DWM light window border (white bottom edge on popups).
        const COLORREF none = static_cast<COLORREF>(DWMWA_COLOR_NONE);
        DwmSetWindowAttribute(st->popup, DWMWA_BORDER_COLOR, &none, sizeof(none));
    }

    const bool need_scroll = static_cast<int>(st->items.size()) > max_rows;
    DWORD list_style = WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT;
    if (need_scroll) {
        list_style |= WS_VSCROLL;
    }
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", list_style, 1, 1, pop_w - 2, pop_h - 2, st->popup,
                                reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(list, L"", L"");
    if (st->font) {
        SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    }
    for (const auto& it : st->items) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it.label.c_str()));
    }
    if (st->sel >= 0) {
        SendMessageW(list, LB_SETCURSEL, st->sel, 0);
    }
    style_scroll_host(list, theme().overlay);
    st->open = true;
    g_open_select = st->face;
    ShowWindow(st->popup, SW_SHOWNORMAL);
    SetForegroundWindow(st->popup);
    SetFocus(list);
    SetWindowLongPtrW(list, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    SetWindowSubclass(
        list,
        [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) -> LRESULT {
            if (msg == WM_LBUTTONUP) {
                const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam));
                if (HIWORD(hit) == 0) {
                    SendMessageW(hwnd, LB_SETCURSEL, LOWORD(hit), 0);
                    return select_popup_proc(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(1, LBN_SELCHANGE),
                                             reinterpret_cast<LPARAM>(hwnd));
                }
            }
            if (msg == WM_KEYDOWN) {
                return select_popup_proc(GetParent(hwnd), msg, wparam, lparam);
            }
            return DefSubclassProc(hwnd, msg, wparam, lparam);
        },
        0x534B4C53, 0);
}

LRESULT CALLBACK select_face_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    auto it = g_selects.find(hwnd);
    SelectState* st = it != g_selects.end() ? it->second.get() : nullptr;
    switch (msg) {
        case WM_SK_GETSEL:
            return st ? st->sel : -1;
        case WM_SK_SETSEL:
            if (st) {
                st->sel = static_cast<int>(wparam);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_SK_GETDATA:
            if (st && st->sel >= 0 && st->sel < static_cast<int>(st->items.size())) {
                return st->items[st->sel].data;
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (st && IsWindowEnabled(hwnd)) {
                if (st->open) {
                    select_close_all();
                } else {
                    open_select(st);
                }
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wparam == VK_SPACE || wparam == VK_RETURN || wparam == VK_DOWN || wparam == VK_F4) {
                if (st) {
                    open_select(st);
                }
                return 0;
            }
            break;
        case WM_NCDESTROY:
            select_close_all();
            g_selects.erase(hwnd);
            RemoveWindowSubclass(hwnd, select_face_proc, 0x534B5345);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

HWND create_button(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, ButtonKind kind, HFONT font) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    SetPropW(h, L"ScyllaBtnKind", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(kind)));
    apply_control_chrome(h, font);
    return h;
}

HWND create_text_field(HWND parent, HINSTANCE inst, UINT id, HFONT font, bool password) {
    // ES_MULTILINE (without WANTRETURN/AUTOVSCROLL) enables EM_SETRECT so we can
    // vertically center single-line text with equal top/bottom inset.
    DWORD style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT | ES_MULTILINE;
    if (password) {
        style |= ES_PASSWORD;
    }
    HWND h = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    SetWindowSubclass(h, field_subclass, kFieldSubclassId, 0);
    center_field_text(h);
    return h;
}

  HWND create_document_view(HWND parent, HINSTANCE inst, UINT id, HFONT font) {
      HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_MULTILINE |
          ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 0, 0, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
      apply_control_chrome(h, font);
      style_scroll_host(h, theme().panel);
      SetWindowSubclass(h, document_subclass, 0x534B444F, 0);
      SendMessageW(h, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);
      return h;
  }

  HWND create_path_field(HWND parent, HINSTANCE inst, UINT id, HFONT font) {
    return create_text_field(parent, inst, id, font, false);
}

HWND create_static(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font, bool muted) {
    // SS_LEFT top-aligned text; no DarkMode theme (avoids opaque label boxes).
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    if (font) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    SetPropW(h, L"ScyllaStatic", reinterpret_cast<HANDLE>(1));
    if (muted) {
        SetPropW(h, L"ScyllaMuted", reinterpret_cast<HANDLE>(1));
    }
    return h;
}

HWND create_checkbox(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font) {
    // BS_OWNERDRAW + a paint subclass often skips the first WM_PAINT (blank until hover/click).
    // Paint exclusively through check_subclass; keep a plain button style for reliable show paint.
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    SetPropW(h, L"ScyllaCheckKind", reinterpret_cast<HANDLE>(0));
    SetWindowSubclass(h, check_subclass, kCheckSubclassId, 0);
    return h;
}

HWND create_switch(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    SetPropW(h, L"ScyllaCheckKind", reinterpret_cast<HANDLE>(1));
    SetWindowSubclass(h, check_subclass, kCheckSubclassId, 1);
    return h;
}

HWND create_radio(HWND parent, HINSTANCE inst, UINT id, const wchar_t* text, HFONT font, bool first_in_group) {
    DWORD style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON;
    if (first_in_group) {
        style |= WS_GROUP;
    }
    HWND h = CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    SetPropW(h, L"ScyllaCheckKind", reinterpret_cast<HANDLE>(2));
    SetWindowSubclass(h, check_subclass, kCheckSubclassId, 2);
    return h;
}

HWND create_list(HWND parent, HINSTANCE inst, UINT id, HFONT font, bool owner_draw) {
    DWORD style = WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | WS_TABSTOP | LBS_NOINTEGRALHEIGHT;
    if (owner_draw) {
        style |= LBS_OWNERDRAWFIXED;
    }
    HWND h = CreateWindowExW(0, L"LISTBOX", L"", style, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    style_scroll_host(h, theme().panel);
    return h;
}

HWND create_entity_list(HWND parent, HINSTANCE inst, UINT id, HFONT font) {
    return create_list(parent, inst, id, font, true);
}

HWND create_select(HWND parent, HINSTANCE inst, UINT id, HFONT font) {
    HWND h = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(h, font);
    auto st = std::make_unique<SelectState>();
    st->face = h;
    st->parent = parent;
    st->font = font;
    SetWindowSubclass(h, select_face_proc, 0x534B5345, 0);
    g_selects[h] = std::move(st);
    return h;
}

void select_set_items(HWND select, const std::vector<SelectItem>& items) {
    auto it = g_selects.find(select);
    if (it == g_selects.end()) {
        return;
    }
    it->second->items = items;
    if (it->second->sel >= static_cast<int>(items.size())) {
        it->second->sel = items.empty() ? -1 : 0;
    }
    InvalidateRect(select, nullptr, TRUE);
}

int select_get_index(HWND select) {
    return static_cast<int>(SendMessageW(select, WM_SK_GETSEL, 0, 0));
}

LPARAM select_get_data(HWND select) {
    return SendMessageW(select, WM_SK_GETDATA, 0, 0);
}

void select_set_index(HWND select, int index) {
    SendMessageW(select, WM_SK_SETSEL, static_cast<WPARAM>(index), 0);
}

void select_set_by_data(HWND select, LPARAM data) {
    auto it = g_selects.find(select);
    if (it == g_selects.end()) {
        return;
    }
    for (int i = 0; i < static_cast<int>(it->second->items.size()); ++i) {
        if (it->second->items[i].data == data) {
            it->second->sel = i;
            InvalidateRect(select, nullptr, TRUE);
            return;
        }
    }
}

bool select_handle_command(HWND select, WORD notify) {
    if (notify != BN_CLICKED) {
        return false;
    }
    auto it = g_selects.find(select);
    if (it == g_selects.end()) {
        return false;
    }
    if (it->second->open) {
        select_close_all();
    } else {
        open_select(it->second.get());
    }
    return true;
}

bool select_draw_item(const DRAWITEMSTRUCT* di) {
    if (!di) {
        return false;
    }
    auto it = g_selects.find(di->hwndItem);
    if (it == g_selects.end()) {
        return false;
    }
    const Theme& t = theme();
    RECT r = di->rcItem;
    const bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
    const bool down = (di->itemState & ODS_SELECTED) != 0;
    const bool disabled = (di->itemState & ODS_DISABLED) != 0;
    const bool focus = (di->itemState & ODS_FOCUS) != 0;
    fill_rect(di->hDC, r, t.panel);
    COLORREF fill = down ? t.surface_active : (hot ? t.surface_hover : t.input_bg);
    COLORREF edge = focus ? t.amber : (hot ? t.border_default : t.border_subtle);
    round_fill(di->hDC, r, 4, fill, edge);
    const int pad_x = ui_space::dip(di->hwndItem, 10);
    const int pad_y = ui_space::dip(di->hwndItem, 2);
    RECT tr = r;
    tr.left += pad_x;
    tr.right -= ui_space::dip(di->hwndItem, 22);
    tr.top += pad_y;
    tr.bottom -= pad_y;
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, disabled ? t.disabled_text : t.text);
    if (it->second->font) {
        SelectObject(di->hDC, it->second->font);
    }
    const wchar_t* label = L"(none)";
    std::wstring owned;
    if (it->second->sel >= 0 && it->second->sel < static_cast<int>(it->second->items.size())) {
        owned = it->second->items[it->second->sel].label;
        label = owned.c_str();
    }
    // Vertically center label + chevron (equal top/bottom inset; text never touches border).
    DrawTextW(di->hDC, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_chevron(di->hDC, r.right - 12, (r.top + r.bottom) / 2, t.muted);
    return true;
}

void select_close_all() {
    if (g_open_select) {
        auto it = g_selects.find(g_open_select);
        if (it != g_selects.end() && it->second->popup) {
            DestroyWindow(it->second->popup);
            it->second->popup = nullptr;
            it->second->open = false;
        }
        g_open_select = nullptr;
    }
}

void style_scroll_host(HWND hwnd, COLORREF track) {
    if (!hwnd) {
        return;
    }
    apply_dark_child(hwnd);
    install_thin_scrollbar(hwnd, track);
}

void paint_field_border(HWND hwnd, HDC dc, bool focus, bool error) {
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    POINT tl{0, 0};
    ClientToScreen(hwnd, &tl);
    const int ox = tl.x - wr.left;
    const int oy = tl.y - wr.top;
    RECT border{0, 0, wr.right - wr.left, wr.bottom - wr.top};
    // Fill non-client frame
    const Theme& t = theme();
    HBRUSH bg = CreateSolidBrush(t.input_bg);
    FrameRect(dc, &border, bg);
    DeleteObject(bg);
    COLORREF edge = error ? t.danger : (focus ? t.amber : t.border_subtle);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, border.left, border.top, border.right, border.bottom);
    SelectObject(dc, old);
    DeleteObject(pen);
    (void)ox;
    (void)oy;
    (void)cr;
}

namespace {

// These painters only receive an HDC, and ui_space::dip(nullptr, …) silently resolves to 96 DPI —
// so every inset below used to be a physically fixed pixel count that shrank optically at 150%/200%.
// Recovering the owning HWND from the DC makes them scale like the rest of the kit.
int dc_dip(HDC dc, int v) {
    return ui_space::dip(WindowFromDC(dc), v);
}

}  // namespace

void paint_empty_state(HDC dc, const RECT& r, HFONT title_font, HFONT body_font, const wchar_t* title,
                       const wchar_t* body) {
    const Theme& t = theme();
    fill_rect(dc, r, t.panel);
    RECT tr = r;
    InflateRect(&tr, -dc_dip(dc, 24), -dc_dip(dc, 24));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.secondary);
    if (title_font) {
        SelectObject(dc, title_font);
    }
    DrawTextW(dc, title, -1, &tr, DT_CENTER | DT_WORDBREAK);
    tr.top += dc_dip(dc, 28);
    SetTextColor(dc, t.muted);
    if (body_font) {
        SelectObject(dc, body_font);
    }
    DrawTextW(dc, body, -1, &tr, DT_CENTER | DT_WORDBREAK);
}

void paint_page_header(HDC dc, const RECT& r, HFONT title_font, HFONT body_font, const wchar_t* title,
                       const wchar_t* desc) {
    const Theme& t = theme();
    fill_rect(dc, r, t.panel);
    // Inset both sides: the title used to start exactly on r.left and the description could run
    // into r.right, which the universal geometry rule forbids.
    RECT tr = r;
    const int pad_x = dc_dip(dc, 2);
    tr.left += pad_x;
    tr.right -= pad_x;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.text);
    if (title_font) {
        SelectObject(dc, title_font);
    }
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    tr.top += dc_dip(dc, ui_space::kPageHeaderTitleHDip);
    SetTextColor(dc, t.muted);
    if (body_font) {
        SelectObject(dc, body_font);
    }
    DrawTextW(dc, desc, -1, &tr, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void paint_status_badge(HDC dc, const RECT& r, HFONT font, const wchar_t* text, COLORREF fg, COLORREF bg) {
    round_fill(dc, r, 4, bg, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    if (font) {
        SelectObject(dc, font);
    }
    // Horizontal breathing room inside the pill; text was drawn edge-to-edge on the fill.
    RECT tr = r;
    const int pad_x = dc_dip(dc, 8);
    tr.left += pad_x;
    tr.right -= pad_x;
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void paint_nav_row(HDC dc, const RECT& r, HFONT font, const wchar_t* text, bool selected, bool hot) {
    const Theme& t = theme();
    COLORREF fill = selected ? t.surface_active : (hot ? t.surface_hover : t.navigation);
    fill_rect(dc, r, fill);
    const int rail_inset = dc_dip(dc, 4);
    if (selected) {
        RECT ind{r.left, r.top + rail_inset, r.left + dc_dip(dc, 2), r.bottom - rail_inset};
        fill_rect(dc, ind, t.amber);
    }
    RECT tr = r;
    tr.left += dc_dip(dc, 12);
    tr.right -= dc_dip(dc, 8);  // was flush to the right edge
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? t.text : t.secondary);
    if (font) {
        SelectObject(dc, font);
    }
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void paint_entity_row(HDC dc, const RECT& r, HFONT font, const wchar_t* primary, const wchar_t* secondary,
                      bool selected, bool hot) {
    const Theme& t = theme();
    fill_rect(dc, r, selected ? t.surface_active : (hot ? t.surface_hover : t.panel));
    const int rail_inset = dc_dip(dc, 4);
    if (selected) {
        // Amber rail — same affordance as settings nav selection.
        RECT rail{r.left, r.top + rail_inset, r.left + dc_dip(dc, 3), r.bottom - rail_inset};
        fill_rect(dc, rail, t.amber);
    }
    const int pad_x = dc_dip(dc, 12);
    const int pad_y = dc_dip(dc, 6);
    const int inner_h = (std::max)(1, static_cast<int>(r.bottom - r.top) - pad_y * 2);
    // Two equal lines with equal top/bottom padding: single-line rows keep a centered mid-line
    // rather than being pinned to the top half of the row.
    const int line_h = secondary && secondary[0] ? inner_h / 2 : inner_h;
    RECT pr{r.left + pad_x, r.top + pad_y, r.right - pad_x, r.top + pad_y + line_h};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t.text);
    if (font) {
        SelectObject(dc, font);
    }
    DrawTextW(dc, primary, -1, &pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (secondary && secondary[0]) {
        RECT sr{r.left + pad_x, pr.bottom, r.right - pad_x, r.bottom - pad_y};
        SetTextColor(dc, t.muted);
        DrawTextW(dc, secondary, -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void draw_checkbox_glyph(HDC dc, int x, int y, int s, bool checked, bool hot, bool disabled) {
    const Theme& t = theme();
    // Unchecked must read clearly on panel/editor — border_subtle on panel was "hunt to find".
    COLORREF edge = disabled ? t.border_subtle : (checked || hot ? t.border_strong : t.border_default);
    COLORREF fill = checked ? (disabled ? t.amber_muted : t.amber) : (hot ? t.surface_hover : t.surface);
    RECT box{x, y, x + s, y + s};
    round_fill(dc, box, 3, fill, edge);
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, 2, disabled ? t.disabled_text : RGB(0xFF, 0xFF, 0xFF));
        HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, x + s / 5, y + s / 2, nullptr);
        LineTo(dc, x + s * 2 / 5, y + s * 3 / 4);
        LineTo(dc, x + s * 4 / 5, y + s / 4);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

void draw_switch_glyph(HDC dc, int x, int y, int w, int h, bool on, bool hot, bool disabled) {
    const Theme& t = theme();
    COLORREF track = on ? (disabled ? t.amber_muted : t.amber) : (hot ? t.surface_hover : t.surface_active);
    RECT tr{x, y, x + w, y + h};
    round_fill(dc, tr, h / 2, track, track);
    const int pad = 2;
    const int thumb = h - pad * 2;
    const int tx = on ? (x + w - pad - thumb) : (x + pad);
    RECT th{tx, y + pad, tx + thumb, y + pad + thumb};
    round_fill(dc, th, thumb / 2, disabled ? t.muted : t.text, disabled ? t.muted : t.text);
}

bool draw_kit_item(const DRAWITEMSTRUCT* di, HFONT font) {
    if (!di || di->CtlType != ODT_BUTTON) {
        return false;
    }
    if (select_draw_item(di)) {
        return true;
    }
    HANDLE kind_h = GetPropW(di->hwndItem, L"ScyllaBtnKind");
    if (kind_h) {
        const auto kind = static_cast<ButtonKind>(reinterpret_cast<INT_PTR>(kind_h));
        if (kind == ButtonKind::Primary) {
            const Theme& t = theme();
            RECT r = di->rcItem;
            const bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
            const bool down = (di->itemState & ODS_SELECTED) != 0;
            const bool disabled = (di->itemState & ODS_DISABLED) != 0;
            COLORREF fill = disabled ? t.input_bg : (down ? t.amber_pressed : (hot ? t.amber_hover : t.amber));
            fill_rect(di->hDC, r, t.panel);
            round_fill(di->hDC, r, 4, fill, fill);
            wchar_t text[128]{};
            GetWindowTextW(di->hwndItem, text, 128);
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, disabled ? t.disabled_text : RGB(0xFF, 0xFF, 0xFF));
            if (font) {
                SelectObject(di->hDC, font);
            }
            DrawTextW(di->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return true;
        }
        draw_themed_button(di, font, button_kind_to_visual(kind), false, false);
        return true;
    }
    HANDLE check = GetPropW(di->hwndItem, L"ScyllaCheckKind");
    if (check) {
        // Owner-draw path for checkbox — paint via same glyphs
        const Theme& t = theme();
        fill_rect(di->hDC, di->rcItem, t.panel);
        const bool checked = GetPropW(di->hwndItem, L"ScyllaChecked") != nullptr;
        const bool disabled = (di->itemState & ODS_DISABLED) != 0;
        const bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
        const INT_PTR k = reinterpret_cast<INT_PTR>(check);
        wchar_t text[256]{};
        GetWindowTextW(di->hwndItem, text, 256);
        if (font) {
            SelectObject(di->hDC, font);
        }
        RECT r = di->rcItem;
        const int box_h = r.bottom - r.top;
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, disabled ? t.disabled_text : t.text);
        if (k == 1) {
            const int sw = ui_space::dip(di->hwndItem, 36);
            const int sh = ui_space::dip(di->hwndItem, 18);
            const int gy = r.top + (std::max)(0, (box_h - sh) / 2);
            draw_switch_glyph(di->hDC, r.left, gy, sw, sh, checked, hot, disabled);
            RECT tr{r.left + sw + ui_space::dip(di->hwndItem, 8), r.top, r.right, r.bottom};
            DrawTextW(di->hDC, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            const int s = ui_space::dip(di->hwndItem, 16);
            const int gy = r.top + (std::max)(0, (box_h - s) / 2);
            draw_checkbox_glyph(di->hDC, r.left, gy, s, checked, hot, disabled);
            RECT tr{r.left + s + ui_space::dip(di->hwndItem, 8), r.top, r.right, r.bottom};
            DrawTextW(di->hDC, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        return true;
    }
    return false;
}

bool confirm_destructive(HWND parent, const wchar_t* verb, const wchar_t* object, const wchar_t* consequence) {
    std::wstring body = verb ? verb : L"Remove";
    body += L' ';
    body += (object && *object) ? object : L"this item";
    body += L"?";
    if (consequence && *consequence) {
        body += L"\n\n";
        body += consequence;
    }
    // MB_DEFBUTTON2 keeps No as the default so Enter/Space never confirms a destructive action.
    return MessageBoxW(parent, body.c_str(), L"Scylla Workbench",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void report_save_failure(HWND parent, const wchar_t* what, const std::wstring& path) {
    std::wstring body = L"Could not save ";
    body += (what && *what) ? what : L"changes";
    body += L".\n\nThe change is applied in this session but will be lost when Scylla restarts.";
    if (!path.empty()) {
        body += L"\n\nFile: ";
        body += path;
    }
    body += L"\n\nCheck that the file is writable and not open in another program.";
    MessageBoxW(parent, body.c_str(), L"Scylla Workbench", MB_OK | MB_ICONWARNING);
}

HWND create_modal_host(HWND parent, HINSTANCE inst, UINT id, const wchar_t* title, HFONT font, int width_dip,
                       int height_dip) {
    RECT pr{};
    GetClientRect(parent, &pr);
    const int w = ui_space::dip(parent, width_dip);
    const int h = ui_space::dip(parent, height_dip);
    const int x = pr.left + ((pr.right - pr.left) - w) / 2;
    const int y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    HWND host = CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
    apply_control_chrome(host, font);
    SetPropW(host, kModalProp, reinterpret_cast<HANDLE>(1));
    return host;
}

void destroy_modal_host(HWND modal) {
    if (modal) {
        DestroyWindow(modal);
    }
}

void apply_control_chrome(HWND hwnd, HFONT font) {
    if (!hwnd) {
        return;
    }
    if (font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    apply_dark_child(hwnd);
}

}  // namespace ui_kit
}  // namespace scyllagpt
