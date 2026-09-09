#include "scyllagpt/chat_log_view.h"

#include "scyllagpt/chat_log_rows.h"
#include "scyllagpt/composer_tokens.h"
#include "scyllagpt/markdown.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"

// GDI+ needs COM stream types that WIN32_LEAN_AND_MEAN omits from windows.h.
#include <objidl.h>
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <memory>
#include <utility>

#pragma comment(lib, "gdiplus.lib")

namespace scyllagpt {
namespace {

constexpr wchar_t kClassName[] = L"ScyllaChatLogView";

// Spacing comes from the shared 4px grid and is scaled per monitor. Hard-coded pixels here used to
// disagree with every other pane and did not scale above 100% DPI.
struct Metrics {
    int pad = 16;
    int gap = 12;
    int tight = 8;
    int header = 20;
    int cta = 28;
    int thumb = 72;
    int scrollbar = 12;
    int line = 18;
};

Metrics metrics_for(HWND hwnd) {
    Metrics m;
    m.pad = ui_space::dip(hwnd, ui_space::kPadOuterDip);
    m.gap = ui_space::dip(hwnd, ui_space::kPadRowDip);
    m.tight = ui_space::dip(hwnd, ui_space::kPadTightDip);
    m.header = ui_space::dip(hwnd, 20);
    m.cta = ui_space::dip(hwnd, ui_space::kCompactHDip);
    m.thumb = ui_space::dip(hwnd, 72);
    m.scrollbar = ui_space::dip(hwnd, 12);
    m.line = ui_space::dip(hwnd, 18);
    return m;
}

struct LayoutRow {
    RECT body{};
    RECT cta{};
    RECT thumbs{};
    int height = 0;
};

struct State {
    HFONT font = nullptr;
    std::vector<ChatLogMessage> messages;
    std::vector<LayoutRow> rows;
    std::vector<HWND> bodies;
    std::vector<std::wstring> body_texts;  // text each body currently renders
    std::vector<int> body_heights;         // cached measured height; -1 when stale
    std::vector<RECT> body_rects;          // last placed geometry, to skip no-op moves
    std::vector<bool> body_visible;
    std::vector<bool> expanded;
    ChatLogAttachmentCallback on_attachment;
    ChatLogFileOpenCallback on_file_open;
    Metrics metrics;
    int measured_width = -1;
    int measure_result = 0;  // filled from EN_REQUESTRESIZE while measuring
    bool measuring = false;
    int scroll = 0;
    int content_height = 0;
    int wheel_remainder = 0;
    bool scrollbar_hover = false;
    bool scrollbar_dragging = false;
    int scrollbar_grab = 0;
};

State* state_of(HWND hwnd) {
    return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK body_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                           UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEWHEEL) {
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    }
    if (msg == WM_KEYDOWN) {
        // Page and document keys scroll the log; plain Home/End stay with the body so text
        // selection keeps working.
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wp == VK_PRIOR || wp == VK_NEXT || (ctrl && (wp == VK_HOME || wp == VK_END))) {
            return SendMessageW(GetParent(hwnd), msg, wp, lp);
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Message text must match the shell font rather than a hard-coded size. RichEdit wants points, so
// convert the shell font's pixel height through the window DPI instead of guessing.
int body_point_size(HWND hwnd, HFONT font) {
    LOGFONTW lf{};
    if (font && GetObjectW(font, sizeof(lf), &lf) != 0) {
        const int px = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
        const int dpi = (std::max)(96, ui_space::dip(hwnd, 96));
        if (px > 0) return (std::max)(8, MulDiv(px, 72, dpi));
    }
    return 10;
}

void fill_body_text(HWND hwnd, HWND body, HFONT font, const std::wstring& source) {
    ui_kit::MessageBodyStyle style{};
    style.text = theme().text;
    style.bg = theme().agent;
    style.link = theme().amber_text;
    style.code_bg = theme().raised;
    style.base_pt = body_point_size(hwnd, font);
    const std::wstring assembled = ui_kit::set_markdown_body(body, source, style);
    std::vector<ui_kit::CharLinkSpan> links;
    for (const auto& span : find_at_file_spans(assembled)) {
        links.push_back({static_cast<long>(span.begin), static_cast<long>(span.end), span.path});
    }
    ui_kit::apply_link_spans(body, links, theme().amber_text);
}

void position_bodies(HWND hwnd, State* state) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const std::size_t n = (std::min)(state->bodies.size(), state->rows.size());
    if (n == 0) return;
    state->body_rects.resize(state->bodies.size());
    state->body_visible.resize(state->bodies.size(), false);
    HDWP dwp = BeginDeferWindowPos(static_cast<int>(n));
    for (std::size_t i = 0; i < n; ++i) {
        RECT body = state->rows[i].body;
        OffsetRect(&body, 0, -state->scroll);
        const bool visible = body.bottom > client.top && body.top < client.bottom;
        const bool same_rect = EqualRect(&state->body_rects[i], &body) != FALSE;
        const bool same_visibility = state->body_visible[i] == visible;
        if (same_rect && same_visibility) continue;
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        if (same_rect) flags |= SWP_NOMOVE | SWP_NOSIZE;
        if (!same_visibility) flags |= visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
        state->body_rects[i] = body;
        state->body_visible[i] = visible;
        // No RedrawWindow(UPDATENOW) here: forcing a synchronous repaint of every child on every
        // update is what made streaming and scrolling stutter.
        if (dwp) {
            dwp = DeferWindowPos(dwp, state->bodies[i], nullptr, body.left, body.top,
                                 (std::max)(1L, body.right - body.left),
                                 (std::max)(1L, body.bottom - body.top), flags);
        }
    }
    if (dwp) EndDeferWindowPos(dwp);
}

// Ask the control how tall its wrapped content is at |width|. This replaces a DrawTextW estimate
// that used a different font than the one RichEdit renders with, which clipped rows and let the
// control reveal its own unthemed scrollbar.
int measure_body(HWND hwnd, State* state, std::size_t index, int width) {
    HWND body = state->bodies[index];
    // Probe inside a generously tall window: EM_REQUESTRESIZE reports what the control needs at its
    // current width, and a cramped window under-reports.
    SetWindowPos(body, nullptr, 0, 0, (std::max)(1, width), 4096,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    state->measuring = true;
    state->measure_result = 0;
    SendMessageW(body, EM_REQUESTRESIZE, 0, 0);
    state->measuring = false;
    int measured = state->measure_result;
    // The requested rect omits the trailing paragraph, which cropped the last line or two. Take the
    // last character's line top as a floor and add a line for the line itself.
    const LRESULT length = SendMessageW(body, WM_GETTEXTLENGTH, 0, 0);
    if (length > 0) {
        POINTL pt{};
        SendMessageW(body, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), static_cast<LPARAM>(length - 1));
        measured = (std::max)(measured, static_cast<int>(pt.y) + state->metrics.line);
    }
    state->body_rects[index] = RECT{};  // geometry was clobbered by the probe
    return (std::max)(state->metrics.line, measured) + ui_space::dip(hwnd, 2);
}

void update_scroll(HWND hwnd, State* state) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const int page = (std::max)(0, static_cast<int>(r.bottom));
    const int max_scroll = (std::max)(0, state->content_height - page);
    state->scroll = (std::min)(state->scroll, max_scroll);
}

RECT scrollbar_track(HWND hwnd, const State* state) {
    RECT r{};
    GetClientRect(hwnd, &r);
    return {r.right - state->metrics.scrollbar, 0, r.right, r.bottom};
}

RECT scrollbar_thumb(HWND hwnd, const State* state) {
    const RECT track = scrollbar_track(hwnd, state);
    const int track_height = (std::max)(0, static_cast<int>(track.bottom - track.top));
    RECT client{};
    GetClientRect(hwnd, &client);
    const int page = (std::max)(0, static_cast<int>(client.bottom));
    if (state->content_height <= page || track_height <= 0) return track;
    const int thumb_height = (std::max)(24, MulDiv(page, track_height, state->content_height));
    const int travel = (std::max)(0, track_height - thumb_height);
    const int max_scroll = (std::max)(1, state->content_height - page);
    const int y = track.top + (travel > 0 ? MulDiv(state->scroll, travel, max_scroll) : 0);
    return {track.left + 2, y, track.right - 2, y + thumb_height};
}

void measure(HWND hwnd, State* state) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const Metrics& m = state->metrics;
    const int width = (std::max)(1, static_cast<int>(r.right) - m.pad * 2 - m.scrollbar);
    if (width != state->measured_width) {
        state->measured_width = width;
        std::fill(state->body_heights.begin(), state->body_heights.end(), -1);
    }
    state->body_heights.resize(state->messages.size(), -1);
    state->body_rects.resize((std::max)(state->messages.size(), state->bodies.size()));
    state->body_visible.resize((std::max)(state->messages.size(), state->bodies.size()), false);
    state->rows.clear();
    state->rows.reserve(state->messages.size());
    int y = m.gap;
    for (std::size_t i = 0; i < state->messages.size(); ++i) {
        LayoutRow row{};
        y += i ? m.gap : 0;
        if (state->body_heights[i] < 0 && i < state->bodies.size()) {
            state->body_heights[i] = measure_body(hwnd, state, i, width);
        }
        const int body_h = (std::max)(m.line, state->body_heights[i] < 0 ? m.line : state->body_heights[i]);
        row.body = {m.pad, y + m.header, m.pad + width, y + m.header + body_h};
        y = row.body.bottom + m.gap;
        if (!state->messages[i].attachments.empty()) {
            row.cta = {m.pad, y, m.pad + width, y + m.cta};
            y = row.cta.bottom + (state->expanded[i] ? m.tight : 0);
            if (state->expanded[i]) {
                row.thumbs = {m.pad, y, m.pad + width, y + m.thumb};
                y = row.thumbs.bottom;
            }
            y += m.gap;
        }
        row.height = y;
        state->rows.push_back(row);
    }
    state->content_height = y + m.gap;
    update_scroll(hwnd, state);
    position_bodies(hwnd, state);
}

void scroll_to(HWND hwnd, State* state, int target) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const int page = (std::max)(0, static_cast<int>(r.bottom));
    const int max_scroll = (std::max)(0, state->content_height - page);
    const int next = (std::max)(0, (std::min)(target, max_scroll));
    if (next == state->scroll) return;
    state->scroll = next;
    position_bodies(hwnd, state);
    InvalidateRect(hwnd, nullptr, FALSE);
}

int wheel_step(HWND hwnd, const State* state) {
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL) {
        RECT r{};
        GetClientRect(hwnd, &r);
        return (std::max)(state->metrics.line, static_cast<int>(r.bottom) - state->metrics.line);
    }
    if (lines == 0) lines = 3;
    return state->metrics.line * static_cast<int>(lines);
}

void draw_thumbnail(HDC dc, const RECT& r, const DisplayAttachment& attachment) {
    fill_rect(dc, r, theme().surface_hover);
    const std::wstring path = utf16(attachment.path);
    Gdiplus::Bitmap image(path.c_str());
    if (image.GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics graphics(dc);
        const double scale = (std::min)(static_cast<double>(r.right - r.left - 8) / image.GetWidth(),
                                        static_cast<double>(r.bottom - r.top - 8) / image.GetHeight());
        const int w = (std::max)(1, static_cast<int>(image.GetWidth() * scale));
        const int h = (std::max)(1, static_cast<int>(image.GetHeight() * scale));
        graphics.DrawImage(&image, r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - h) / 2, w, h);
    } else {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme().secondary);
        RECT label = r;
        DrawTextW(dc, utf16(attachment.label).c_str(), -1, &label,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void paint(HWND hwnd, State* state, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const Metrics& m = state->metrics;
    fill_rect(dc, client, theme().agent);
    SetBkMode(dc, TRANSPARENT);
    HFONT old = state->font ? static_cast<HFONT>(SelectObject(dc, state->font)) : nullptr;
    for (std::size_t i = 0; i < state->messages.size() && i < state->rows.size(); ++i) {
        const auto& message = state->messages[i];
        const auto& row = state->rows[i];
        const int offset = -state->scroll;
        RECT header = row.body;
        header.top -= m.header;
        header.bottom = header.top + m.header;
        RECT cta = row.cta;
        OffsetRect(&cta, 0, offset);
        if (!message.attachments.empty()) {
            fill_rect(dc, cta, theme().raised);
            RECT text = cta;
            text.left += m.tight;
            SetTextColor(dc, theme().text);
            const std::wstring label = L"+   (" + std::to_wstring(message.attachments.size()) + L") Attachments";
            DrawTextW(dc, label.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (state->expanded[i]) {
                RECT thumbs = row.thumbs;
                OffsetRect(&thumbs, 0, offset);
                for (std::size_t n = 0; n < message.attachments.size(); ++n) {
                    const int x = thumbs.left + static_cast<int>(n) * (m.thumb + m.tight);
                    if (x + m.thumb > thumbs.right) break;
                    RECT tile{x, thumbs.top, x + m.thumb, thumbs.top + m.thumb};
                    draw_thumbnail(dc, tile, message.attachments[n]);
                }
            }
        }
        OffsetRect(&header, 0, offset);
        SetTextColor(dc, message.user ? theme().text : theme().amber_text);
        DrawTextW(dc, message.user ? L"You" : L"Agent", -1, &header,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const int rule_y = row.height - m.gap / 2 + offset;
        RECT rule{client.left + m.pad, rule_y, client.right - m.pad, rule_y + 1};
        fill_rect(dc, rule, theme().divider);
    }
    if (state->content_height > client.bottom) {
        fill_rect(dc, scrollbar_track(hwnd, state), theme().agent);
        RECT thumb = scrollbar_thumb(hwnd, state);
        fill_rect(dc, thumb, state->scrollbar_dragging ? theme().scroll_thumb_active
                                                       : (state->scrollbar_hover ? theme().scroll_thumb_hot
                                                                                 : theme().scroll_thumb));
    }
    if (old) SelectObject(dc, old);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = state_of(hwnd);
    if (msg == WM_NCCREATE) {
        state = reinterpret_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->metrics = metrics_for(hwnd);
    }
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_SIZE:
        state->metrics = metrics_for(hwnd);
        measure(hwnd, state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC child_dc = reinterpret_cast<HDC>(wp);
        SetBkColor(child_dc, theme().agent);
        SetTextColor(child_dc, theme().text);
        SetDCBrushColor(child_dc, theme().agent);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr && hdr->code == EN_REQUESTRESIZE) {
            if (state->measuring) {
                const auto* resize = reinterpret_cast<REQRESIZE*>(lp);
                state->measure_result = resize->rc.bottom - resize->rc.top;
            }
            return 0;
        }
        if (hdr && hdr->code == EN_LINK && state->on_file_open) {
            const auto* link = reinterpret_cast<ENLINK*>(lp);
            if (link->msg != WM_LBUTTONUP) return 0;
            auto target = ui_kit::markdown_link_at(hdr->hwndFrom, link->chrg.cpMin);
            if (!target.empty()) state->on_file_open(target);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, (std::max)(1L, client.right), (std::max)(1L, client.bottom));
        HGDIOBJ previous = SelectObject(buffer, bitmap);
        paint(hwnd, state, buffer);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
               buffer, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(buffer, previous);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Honour the system wheel-lines setting and keep the sub-notch remainder, so precision
        // wheels and touchpads scroll smoothly instead of in fixed 60px jumps.
        state->wheel_remainder += GET_WHEEL_DELTA_WPARAM(wp);
        const int notches = state->wheel_remainder / WHEEL_DELTA;
        if (notches != 0) {
            state->wheel_remainder -= notches * WHEEL_DELTA;
            scroll_to(hwnd, state, state->scroll - notches * wheel_step(hwnd, state));
        }
        return 0;
    }
    case WM_KEYDOWN: {
        RECT r{};
        GetClientRect(hwnd, &r);
        const int page = (std::max)(state->metrics.line, static_cast<int>(r.bottom) - state->metrics.line);
        switch (wp) {
        case VK_PRIOR: scroll_to(hwnd, state, state->scroll - page); return 0;
        case VK_NEXT: scroll_to(hwnd, state, state->scroll + page); return 0;
        case VK_HOME: scroll_to(hwnd, state, 0); return 0;
        case VK_END: scroll_to(hwnd, state, state->content_height); return 0;
        case VK_UP: scroll_to(hwnd, state, state->scroll - state->metrics.line); return 0;
        case VK_DOWN: scroll_to(hwnd, state, state->scroll + state->metrics.line); return 0;
        default: break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT client{};
        GetClientRect(hwnd, &client);
        if (state->scrollbar_dragging) {
            const RECT track = scrollbar_track(hwnd, state);
            const RECT thumb = scrollbar_thumb(hwnd, state);
            const int page = (std::max)(0, static_cast<int>(client.bottom));
            const int travel = (std::max)(1, static_cast<int>(track.bottom - track.top - (thumb.bottom - thumb.top)));
            const int max_scroll = (std::max)(0, state->content_height - page);
            const int grab_y = static_cast<int>(point.y) - state->scrollbar_grab;
            const int thumb_cap = static_cast<int>(track.bottom - (thumb.bottom - thumb.top));
            const int y = (std::max)(static_cast<int>(track.top), (std::min)(grab_y, thumb_cap));
            scroll_to(hwnd, state, travel > 0 ? MulDiv(y - track.top, max_scroll, travel) : 0);
            return 0;
        }
        const RECT track = scrollbar_track(hwnd, state);
        const bool hover = PtInRect(&track, point) && state->content_height > client.bottom;
        if (hover != state->scrollbar_hover) {
            state->scrollbar_hover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (!state->scrollbar_dragging) {
            state->scrollbar_hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT thumb = scrollbar_thumb(hwnd, state);
        RECT client{};
        GetClientRect(hwnd, &client);
        SetFocus(hwnd);
        if (state->content_height > client.bottom && PtInRect(&thumb, point)) {
            state->scrollbar_dragging = true;
            state->scrollbar_hover = true;
            state->scrollbar_grab = point.y - thumb.top;
            SetCapture(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_LBUTTONUP: {
        if (state->scrollbar_dragging) {
            state->scrollbar_dragging = false;
            state->scrollbar_grab = 0;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        point.y += state->scroll;
        for (std::size_t i = 0; i < state->rows.size(); ++i) {
            const auto& row = state->rows[i];
            if (PtInRect(&row.cta, point)) {
                state->expanded[i] = !state->expanded[i];
                measure(hwnd, state);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (state->expanded[i] && PtInRect(&row.thumbs, point) && state->on_attachment) {
                const int index = (point.x - row.thumbs.left) / (state->metrics.thumb + state->metrics.tight);
                if (index >= 0 && index < static_cast<int>(state->messages[i].attachments.size()))
                    state->on_attachment({state->messages[i].attachments[static_cast<std::size_t>(index)]});
                return 0;
            }
        }
        return 0;
    }
    case WM_NCDESTROY:
        for (HWND body : state->bodies) RemoveWindowSubclass(body, body_proc, 1);
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void register_class() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    registered = true;
}

HWND create_body(HWND parent, HFONT font) {
    // No ES_AUTOVSCROLL and no vertical scrollbar: the host sizes each body to its content, and a
    // control-owned bar would paint unthemed (the stray white scrollbar).
    HWND body = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                WS_CHILD | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_READONLY,
                                0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(body, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(body, EM_SETBKGNDCOLOR, 0, theme().agent);
    SendMessageW(body, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(body, EM_SHOWSCROLLBAR, SB_VERT, FALSE);
    SetWindowSubclass(body, body_proc, 1, 0);
    return body;
}

}  // namespace

HWND chat_log_create(HWND parent, HFONT font, ChatLogAttachmentCallback on_attachment,
                     ChatLogFileOpenCallback on_file_open) {
    register_class();
    LoadLibraryW(L"Msftedit.dll");
    auto* state = new State{};
    state->font = font;
    state->on_attachment = std::move(on_attachment);
    state->on_file_open = std::move(on_file_open);
    return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN, 0, 0, 0, 0,
                           parent, nullptr, GetModuleHandleW(nullptr), state);
}

void chat_log_set_font(HWND hwnd, HFONT font) {
    if (auto* s = state_of(hwnd)) {
        s->font = font;
        s->metrics = metrics_for(hwnd);
        // Body text size derives from the shell font, so a font change has to re-render each body.
        for (std::size_t i = 0; i < s->bodies.size(); ++i) {
            SendMessageW(s->bodies[i], WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            if (i < s->body_texts.size()) fill_body_text(hwnd, s->bodies[i], font, s->body_texts[i]);
        }
        std::fill(s->body_heights.begin(), s->body_heights.end(), -1);
        measure(hwnd, s);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void chat_log_set_messages(HWND hwnd, std::vector<ChatLogMessage> messages) {
    auto* s = state_of(hwnd);
    if (!s) return;
    std::vector<std::wstring> next_texts;
    next_texts.reserve(messages.size());
    for (const auto& message : messages) next_texts.push_back(message.text);
    // Reconcile against what is already on screen. Streaming deltas only change the trailing row,
    // so every earlier body is reused untouched instead of being destroyed and rebuilt.
    const auto diff = chat_log_plan_rows(s->body_texts, next_texts);

    for (std::size_t i = diff.destroy_from; i < s->bodies.size(); ++i) {
        RemoveWindowSubclass(s->bodies[i], body_proc, 1);
        DestroyWindow(s->bodies[i]);
    }
    if (diff.destroy_from < s->bodies.size()) {
        s->bodies.resize(diff.destroy_from);
        s->body_rects.resize(diff.destroy_from);
        s->body_visible.resize(diff.destroy_from);
    }

    const auto previous_expanded = s->expanded;
    s->messages = std::move(messages);
    s->expanded.assign(s->messages.size(), false);
    for (std::size_t i = 0; i < (std::min)(previous_expanded.size(), s->expanded.size()); ++i)
        s->expanded[i] = previous_expanded[i];
    s->body_texts = std::move(next_texts);
    s->body_heights.resize(s->messages.size(), -1);
    s->body_rects.resize(s->messages.size());
    s->body_visible.resize(s->messages.size(), false);

    for (std::size_t i = 0; i < diff.rows.size(); ++i) {
        if (diff.rows[i] == ChatRowAction::Reuse) continue;
        if (diff.rows[i] == ChatRowAction::Create) {
            s->bodies.push_back(create_body(hwnd, s->font));
            s->body_rects[i] = RECT{};
            s->body_visible[i] = false;
        }
        fill_body_text(hwnd, s->bodies[i], s->font, s->body_texts[i]);
        s->body_heights[i] = -1;
    }
    measure(hwnd, s);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void chat_log_clear(HWND hwnd) { chat_log_set_messages(hwnd, {}); }

bool chat_log_empty(HWND hwnd) {
    auto* s = state_of(hwnd);
    return !s || s->messages.empty();
}

void chat_log_scroll_bottom(HWND hwnd) {
    if (auto* s = state_of(hwnd)) {
        RECT r{};
        GetClientRect(hwnd, &r);
        scroll_to(hwnd, s, s->content_height - static_cast<int>(r.bottom - r.top));
    }
}

}  // namespace scyllagpt
