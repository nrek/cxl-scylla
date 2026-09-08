#include "scyllagpt/workbench_panel.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

namespace scyllagpt {
namespace {

constexpr wchar_t kPanelClass[] = L"ScyllaWorkbenchPanel";
constexpr int kIdSurface0 = 9100;
constexpr int kIdSessionTab0 = 9200;
constexpr int kIdSessionClose0 = 9300;
constexpr int kMaxSessionTabs = 32;
constexpr int kIdNew = 9401;
constexpr int kIdNewMenu = 9402;
constexpr int kIdCollapse = 9403;
constexpr int kIdMaximize = 9404;
constexpr int kIdHide = 9405;
constexpr int kIdContent = 9410;
constexpr int kIdStub = 9411;
constexpr int kIdProblemsLabel = 9420;
constexpr int kIdProblemsList = 9421;
constexpr int kIdProblemsRefresh = 9422;
constexpr int kIdOutputLabel = 9423;
constexpr int kIdOutputEdit = 9424;
constexpr int kIdOutputClear = 9425;

// Session tab context menu (TrackPopupMenu command ids, panel-local).
constexpr int kMenuRename = 1;
constexpr int kMenuRestart = 2;
constexpr int kMenuDuplicate = 3;
constexpr int kMenuClose = 4;

// Output is a troubleshooting tail, not an archive: keep the buffer bounded so a chatty
// subsystem cannot grow the process without limit.
constexpr std::size_t kMaxOutputLines = 2000;

// Panel chrome reads the shared design tokens — no private palette. The previous local values
// (0x181A1D et al) drifted from theme() and made the panel read as a different application.
inline COLORREF kPanelBg() { return theme().panel; }
inline COLORREF kPanelRaised() { return theme().surface_active; }
inline COLORREF kPanelText() { return theme().text; }
inline COLORREF kPanelMuted() { return theme().muted; }
inline COLORREF kPanelBorder() { return theme().border_subtle; }
inline COLORREF kPanelAccent() { return theme().amber; }

constexpr int kSessionTabWDip = 132;   // wide enough for "profile · env" before ellipsis
constexpr int kSessionCloseWDip = 18;

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd)), 96);
}

HWND mk_child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           inst, nullptr);
}

void add_button_tooltip(HWND tip, HWND parent, HWND btn, const wchar_t* text) {
    if (!tip || !btn || !text) {
        return;
    }
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = reinterpret_cast<UINT_PTR>(btn);
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void raise_hwnd(HWND h) {
    if (h) {
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool g_panel_class_registered = false;

}  // namespace

const wchar_t* panel_surface_label(PanelSurface s) {
    switch (s) {
        case PanelSurface::Problems:
            return L"Problems";
        case PanelSurface::Output:
            return L"Output";
        case PanelSurface::Ports:
            return L"Ports";
        default:
            return L"Terminal";
    }
}

PanelSurface parse_panel_surface(std::string_view raw) {
    if (raw == "problems") {
        return PanelSurface::Problems;
    }
    if (raw == "output") {
        return PanelSurface::Output;
    }
    if (raw == "ports") {
        return PanelSurface::Ports;
    }
    return PanelSurface::Terminal;
}

std::string panel_surface_string(PanelSurface s) {
    switch (s) {
        case PanelSurface::Problems:
            return "problems";
        case PanelSurface::Output:
            return "output";
        case PanelSurface::Ports:
            return "ports";
        default:
            return "terminal";
    }
}

WorkbenchPanel::~WorkbenchPanel() {
    destroy();
}

bool WorkbenchPanel::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (hwnd_) {
        return true;
    }
    parent_ = parent;
    font_ = font;
    font_small_ = font_small;
    bg_ = CreateSolidBrush(kPanelBg());
    if (!g_panel_class_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WorkbenchPanel::wnd_proc;
        wc.hInstance = inst;
        wc.lpszClassName = kPanelClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(kPanelBg());
        RegisterClassExW(&wc);
        g_panel_class_registered = true;
    }

    hwnd_ = CreateWindowExW(0, kPanelClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, inst, this);
    if (!hwnd_) {
        return false;
    }

    const wchar_t* labels[] = {L"Terminal", L"Problems", L"Output", L"Ports"};
    for (int i = 0; i < 4; ++i) {
        surface_btns_[i] =
            mk_child(hwnd_, L"BUTTON", labels[i], BS_OWNERDRAW | WS_TABSTOP, kIdSurface0 + i, inst);
        if (font_) {
            SendMessageW(surface_btns_[i], WM_SETFONT, reinterpret_cast<WPARAM>(font_small_ ? font_small_ : font_), TRUE);
        }
    }

    // No child window over the session-tab strip: the panel paints and hit-tests it directly.
    // A child there (WS_CLIPCHILDREN) clipped the tabs away and swallowed their clicks.
    btn_new_ = mk_child(hwnd_, L"BUTTON", L"+", BS_OWNERDRAW | WS_TABSTOP, kIdNew, inst);
    btn_new_menu_ = mk_child(hwnd_, L"BUTTON", L"▾", BS_OWNERDRAW | WS_TABSTOP, kIdNewMenu, inst);
    btn_collapse_ = mk_child(hwnd_, L"BUTTON", L"─", BS_OWNERDRAW | WS_TABSTOP, kIdCollapse, inst);
    btn_maximize_ = mk_child(hwnd_, L"BUTTON", L"□", BS_OWNERDRAW | WS_TABSTOP, kIdMaximize, inst);
    btn_hide_ = mk_child(hwnd_, L"BUTTON", L"×", BS_OWNERDRAW | WS_TABSTOP, kIdHide, inst);
    content_ = mk_child(hwnd_, L"STATIC", L"", SS_OWNERDRAW | WS_CLIPCHILDREN, kIdContent, inst);
    stub_label_ = mk_child(hwnd_, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, kIdStub, inst);

    tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0,
                           0, hwnd_, nullptr, inst, nullptr);
    if (tip_) {
        SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, 320);
        add_button_tooltip(tip_, hwnd_, btn_new_, L"New Terminal");
        add_button_tooltip(tip_, hwnd_, btn_new_menu_, L"New Terminal with profile…");
        add_button_tooltip(tip_, hwnd_, btn_collapse_, L"Collapse panel");
        add_button_tooltip(tip_, hwnd_, btn_maximize_, L"Maximize / restore panel");
        add_button_tooltip(tip_, hwnd_, btn_hide_, L"Hide panel");
        add_button_tooltip(tip_, hwnd_, surface_btns_[0], L"Terminal");
        add_button_tooltip(tip_, hwnd_, surface_btns_[1], L"Problems");
        add_button_tooltip(tip_, hwnd_, surface_btns_[2], L"Output");
        add_button_tooltip(tip_, hwnd_, surface_btns_[3], L"Ports");
    }

    for (HWND h : {btn_new_, btn_new_menu_, btn_collapse_, btn_maximize_, btn_hide_, stub_label_}) {
        if (h && font_) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_small_ ? font_small_ : font_), TRUE);
        }
    }

    // Problems / Output surfaces. Created after content_ so they sit above it in z-order —
    // content_ only paints the panel background behind whichever surface is active.
    HFONT small_font = font_small_ ? font_small_ : font_;
    problems_lbl_ = ui_kit::create_static(hwnd_, inst, kIdProblemsLabel, L"Problems", small_font, false);
    problems_list_ = ui_kit::create_list(hwnd_, inst, kIdProblemsList, small_font, /*owner_draw=*/false);
    btn_problems_refresh_ =
        ui_kit::create_button(hwnd_, inst, kIdProblemsRefresh, L"Refresh", ui_kit::ButtonKind::Secondary, small_font);
    output_lbl_ = ui_kit::create_static(hwnd_, inst, kIdOutputLabel, L"Output — Workbench", small_font, false);
    output_edit_ = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_LEFT,
                                   0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOutputEdit)), inst, nullptr);
    ui_kit::apply_control_chrome(output_edit_, small_font);
    ui_kit::style_scroll_host(output_edit_, kPanelBg());
    btn_output_clear_ =
        ui_kit::create_button(hwnd_, inst, kIdOutputClear, L"Clear", ui_kit::ButtonKind::Secondary, small_font);
    for (HWND h : {problems_lbl_, problems_list_, btn_problems_refresh_, output_lbl_, output_edit_,
                   btn_output_clear_}) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
    refill_problems();
    rebuild_output_edit();

    update_stub_label();
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
    return true;
}

void WorkbenchPanel::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (bg_) {
        DeleteObject(bg_);
        bg_ = nullptr;
    }
    session_strip_ = RECT{};
    btn_new_ = btn_new_menu_ = btn_collapse_ = btn_maximize_ = btn_hide_ = nullptr;
    content_ = stub_label_ = tip_ = nullptr;
    problems_lbl_ = problems_list_ = btn_problems_refresh_ = nullptr;
    output_lbl_ = output_edit_ = btn_output_clear_ = nullptr;
    for (auto& b : surface_btns_) {
        b = nullptr;
    }
}

void WorkbenchPanel::set_visible(bool visible) {
    visible_ = visible;
    if (hwnd_) {
        ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
    }
}

void WorkbenchPanel::set_surface(PanelSurface s) {
    // The on_surface_ handler calls back into ensure_terminal_panel(), which calls set_surface()
    // again. Without both guards below that cycle never terminates (stack overflow, plus a
    // settings.json rewrite on every pass).
    if (surface_ == s && notified_surface_) {
        return;
    }
    if (in_surface_change_) {
        surface_ = s;
        update_stub_label();
        return;
    }
    in_surface_change_ = true;
    surface_ = s;
    notified_surface_ = true;
    update_stub_label();
    if (on_surface_) {
        on_surface_();
    }
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    in_surface_change_ = false;
}

void WorkbenchPanel::set_maximized(bool on) {
    maximized_ = on;
}

void WorkbenchPanel::set_collapsed(bool on) {
    collapsed_ = on;
}

void WorkbenchPanel::set_enabled_profiles(std::vector<TerminalProfile> profiles) {
    enabled_profiles_ = std::move(profiles);
}

void WorkbenchPanel::refresh_session_tabs(const TerminalSessionManager& mgr) {
    active_session_ = mgr.active_index();
    session_count_ = static_cast<int>(mgr.count());
    session_titles_.clear();
    session_accents_.clear();
    session_alive_.clear();
    session_exit_code_.clear();
    for (const auto& s : mgr.sessions()) {
        session_titles_.push_back(s.title);
        session_accents_.push_back(s.accent);
        // A shell that has exited still shows a tab. Track it so the tab can say so instead of
        // looking identical to a live session that simply is not printing anything.
        session_alive_.push_back(s.alive);
        session_exit_code_.push_back(s.has_exit_code ? static_cast<int>(s.exit_code) : -1);
    }
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

// Single source of truth for session-tab geometry, shared by WM_PAINT and the click handlers.
int WorkbenchPanel::tab_hit_test(int mx, int my, bool* on_close) const {
    if (on_close) {
        *on_close = false;
    }
    if (!session_strip_visible_ || !hwnd_) {
        return -1;
    }
    if (my < session_strip_.top || my >= session_strip_.bottom || mx < session_strip_.left) {
        return -1;
    }
    const int tab_w = dip(hwnd_, kSessionTabWDip);
    const int close_w = dip(hwnd_, kSessionCloseWDip);
    const int gap = dip(hwnd_, ui_space::kPadXsDip);
    int tx = session_strip_.left;
    for (int i = 0; i < session_count_ && i < kMaxSessionTabs; ++i) {
        if (tx + tab_w > session_strip_.right) {
            break;
        }
        if (mx >= tx && mx < tx + tab_w) {
            if (on_close) {
                *on_close = mx >= tx + tab_w - close_w - dip(hwnd_, 2);
            }
            return i;
        }
        tx += tab_w + gap;
    }
    return -1;
}

RECT WorkbenchPanel::layout(const RECT& panel_rc) {
    RECT empty{};
    if (!hwnd_ || !visible_) {
        return empty;
    }
    const int x = panel_rc.left;
    const int y = panel_rc.top;
    const int w = panel_rc.right - panel_rc.left;
    const int h = panel_rc.bottom - panel_rc.top;
    MoveWindow(hwnd_, x, y, w, h, TRUE);

    const auto m = ui_space::metrics_for(hwnd_);
    const int tab_h = dip(hwnd_, ui_space::kPanelTabHDip);
    const int tool_h = dip(hwnd_, ui_space::kPanelToolHDip);
    const int pad = m.pad_tight;
    const int btn = dip(hwnd_, 28);

    int sx = pad;
    const int surface_w = dip(hwnd_, 78);
    for (int i = 0; i < 4; ++i) {
        MoveWindow(surface_btns_[i], sx, pad / 2, surface_w, tab_h - pad, TRUE);
        ShowWindow(surface_btns_[i], SW_SHOW);
        sx += surface_w + pad / 2;
    }

    int rx = w - pad - btn;
    MoveWindow(btn_hide_, rx, pad / 2, btn, tab_h - pad, TRUE);
    ShowWindow(btn_hide_, SW_SHOW);
    rx -= btn + pad / 2;
    MoveWindow(btn_maximize_, rx, pad / 2, btn, tab_h - pad, TRUE);
    ShowWindow(btn_maximize_, SW_SHOW);
    rx -= btn + pad / 2;
    MoveWindow(btn_collapse_, rx, pad / 2, btn, tab_h - pad, TRUE);
    ShowWindow(btn_collapse_, SW_SHOW);

    const int tool_y = tab_h;
    const bool term = surface_ == PanelSurface::Terminal && !collapsed_;
    if (term) {
        const int new_w = dip(hwnd_, 28);
        const int drop_w = dip(hwnd_, 24);
        MoveWindow(btn_new_menu_, w - pad - drop_w, tool_y + (tool_h - btn) / 2, drop_w, btn, TRUE);
        MoveWindow(btn_new_, w - pad - drop_w - new_w - 2, tool_y + (tool_h - btn) / 2, new_w, btn, TRUE);
        ShowWindow(btn_new_, SW_SHOW);
        ShowWindow(btn_new_menu_, SW_SHOW);
        session_strip_ = RECT{pad, tool_y, (std::max)(pad, w - pad * 2 - new_w - drop_w - pad),
                              tool_y + tool_h};
        session_strip_visible_ = true;
    } else {
        ShowWindow(btn_new_, SW_HIDE);
        ShowWindow(btn_new_menu_, SW_HIDE);
        session_strip_ = RECT{};
        session_strip_visible_ = false;
    }

    const int content_y = collapsed_ ? tab_h : (term ? tab_h + tool_h : tab_h);
    const int content_h = (std::max)(0, h - content_y);
    MoveWindow(content_, 0, content_y, w, content_h, TRUE);
    ShowWindow(content_, SW_SHOW);

    const bool problems = surface_ == PanelSurface::Problems && !collapsed_;
    const bool output = surface_ == PanelSurface::Output && !collapsed_;

    // Only Ports (and any collapsed surface) still falls back to the centered stub label —
    // Terminal, Problems, and Output all have real content now.
    if (!collapsed_ && surface_ != PanelSurface::Ports) {
        ShowWindow(stub_label_, SW_HIDE);
    } else {
        MoveWindow(stub_label_, 0, content_y, w, content_h, TRUE);
        ShowWindow(stub_label_, SW_SHOW);
        update_stub_label();
    }

    // Both list surfaces share one geometry: heading + right-aligned action on the first row,
    // scrollable body underneath.
    const int head_h = dip(hwnd_, 20);
    const int action_w = dip(hwnd_, 72);
    const int body_y = content_y + pad + head_h + pad / 2;
    const int body_h = (std::max)(0, content_y + content_h - pad - body_y);
    auto place_surface = [&](HWND label, HWND body, HWND action, bool on) {
        if (!label || !body || !action) {
            return;
        }
        if (!on) {
            ShowWindow(label, SW_HIDE);
            ShowWindow(body, SW_HIDE);
            ShowWindow(action, SW_HIDE);
            return;
        }
        MoveWindow(label, pad, content_y + pad, (std::max)(1, w - pad * 2 - action_w - pad), head_h, TRUE);
        MoveWindow(action, w - pad - action_w, content_y + pad - dip(hwnd_, 2), action_w, dip(hwnd_, 24), TRUE);
        MoveWindow(body, pad, body_y, (std::max)(1, w - pad * 2), body_h, TRUE);
        ShowWindow(label, SW_SHOW);
        ShowWindow(action, SW_SHOW);
        ShowWindow(body, SW_SHOW);
    };
    place_surface(problems_lbl_, problems_list_, btn_problems_refresh_, problems);
    place_surface(output_lbl_, output_edit_, btn_output_clear_, output);
    if (output) {
        scroll_output_to_end();
    }

    raise_chrome();

    RECT content_rc{x, y + content_y, x + w, y + content_y + content_h};
    return content_rc;
}

void WorkbenchPanel::raise_chrome() {
    // content_ is created after the surface/tool buttons, so without an explicit raise it sits
    // above them in z-order and steals clicks from Terminal/Problems tabs and New Terminal.
    for (HWND h : surface_btns_) {
        raise_hwnd(h);
    }
    raise_hwnd(btn_new_);
    raise_hwnd(btn_new_menu_);
    raise_hwnd(btn_collapse_);
    raise_hwnd(btn_maximize_);
    raise_hwnd(btn_hide_);
    raise_hwnd(stub_label_);
    raise_hwnd(problems_lbl_);
    raise_hwnd(problems_list_);
    raise_hwnd(btn_problems_refresh_);
    raise_hwnd(output_lbl_);
    raise_hwnd(output_edit_);
    raise_hwnd(btn_output_clear_);
}

void WorkbenchPanel::update_stub_label() {
    if (!stub_label_) {
        return;
    }
    const wchar_t* text = L"";
    if (collapsed_) {
        text = L"";
    } else {
        switch (surface_) {
            // Problems and Output have their own controls; the stub only covers Ports and the
            // collapsed state.
            case PanelSurface::Problems:
            case PanelSurface::Output:
                text = L"";
                break;
            case PanelSurface::Ports:
                text = L"Ports — coming soon";
                break;
            default:
                text = session_count_ > 0 ? L"" : L"No terminal sessions — press + to start";
                break;
        }
    }
    SetWindowTextW(stub_label_, text);
}

void WorkbenchPanel::paint_stub_if_needed() {
    update_stub_label();
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void WorkbenchPanel::show_new_menu() {
    if (!hwnd_) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (enabled_profiles_.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 1, L"(no enabled profiles)");
    } else {
        for (std::size_t i = 0; i < enabled_profiles_.size(); ++i) {
            AppendMenuW(menu, MF_STRING, static_cast<UINT>(100 + i), enabled_profiles_[i].name.c_str());
        }
    }
    RECT rc{};
    GetWindowRect(btn_new_menu_ ? btn_new_menu_ : btn_new_, &rc);
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd >= 100) {
        const std::size_t idx = static_cast<std::size_t>(cmd - 100);
        if (idx < enabled_profiles_.size() && on_new_profile_) {
            on_new_profile_(enabled_profiles_[idx].id);
        }
    }
}

void WorkbenchPanel::show_session_menu(int index, int client_x, int client_y) {
    if (!hwnd_ || index < 0 || index >= session_count_) {
        return;
    }
    const bool alive = index >= static_cast<int>(session_alive_.size()) ||
                       session_alive_[static_cast<std::size_t>(index)];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuRename, L"Rename…");
    AppendMenuW(menu, MF_STRING, kMenuRestart, alive ? L"Restart" : L"Restart (respawn)");
    AppendMenuW(menu, MF_STRING, kMenuDuplicate, L"Duplicate");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClose, alive ? L"Close terminal" : L"Close tab");
    POINT pt{client_x, client_y};
    ClientToScreen(hwnd_, &pt);
    const int cmd =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case kMenuRename:
            if (on_rename_session_) {
                on_rename_session_(index);
            }
            break;
        case kMenuRestart:
            if (on_restart_session_) {
                on_restart_session_(index);
            }
            break;
        case kMenuDuplicate:
            if (on_duplicate_session_) {
                on_duplicate_session_(index);
            }
            break;
        case kMenuClose:
            if (on_close_session_) {
                on_close_session_(index, /*confirm=*/true);
            }
            break;
        default:
            break;
    }
}

void WorkbenchPanel::set_problems(std::vector<WorkbenchProblem> items) {
    problems_ = std::move(items);
    refill_problems();
}

void WorkbenchPanel::refill_problems() {
    if (problems_lbl_) {
        std::wstring heading = L"Problems";
        if (!problems_.empty()) {
            heading += L" (" + std::to_wstring(problems_.size()) + L")";
        }
        SetWindowTextW(problems_lbl_, heading.c_str());
    }
    if (!problems_list_) {
        return;
    }
    SendMessageW(problems_list_, LB_RESETCONTENT, 0, 0);
    if (problems_.empty()) {
        // Structure now, diagnostics later: the surface keeps its list shape so a future
        // analyzer only has to supply rows. An unexplained blank slab is not an empty state.
        SendMessageW(problems_list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No problems detected — unsaved documents and workbench "
                                              L"findings appear here."));
    } else {
        for (const auto& p : problems_) {
            std::wstring line = p.primary;
            if (!p.secondary.empty()) {
                line += L"  —  ";
                line += p.secondary;
            }
            SendMessageW(problems_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
    }
    refresh_thin_scrollbar(problems_list_);
    InvalidateRect(problems_list_, nullptr, TRUE);
}

void WorkbenchPanel::append_output(const std::wstring& channel, const std::wstring& line) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t stamp[16]{};
    std::swprintf(stamp, 16, L"%02u:%02u:%02u  ", st.wHour, st.wMinute, st.wSecond);
    std::wstring entry = stamp;
    if (!channel.empty()) {
        entry += L"[";
        entry += channel;
        entry += L"] ";
    }
    entry += line;

    const bool trimmed = output_lines_.size() >= kMaxOutputLines;
    output_lines_.push_back(std::move(entry));
    if (trimmed) {
        output_lines_.erase(output_lines_.begin(),
                            output_lines_.begin() + static_cast<std::ptrdiff_t>(output_lines_.size() - kMaxOutputLines));
        rebuild_output_edit();
        return;
    }
    if (!output_edit_) {
        return;
    }
    std::wstring tail;
    if (output_lines_.size() > 1) {
        tail = L"\r\n";
    }
    tail += output_lines_.back();
    const int len = GetWindowTextLengthW(output_edit_);
    SendMessageW(output_edit_, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(output_edit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(tail.c_str()));
    scroll_output_to_end();
}

void WorkbenchPanel::clear_output() {
    output_lines_.clear();
    rebuild_output_edit();
}

void WorkbenchPanel::rebuild_output_edit() {
    if (!output_edit_) {
        return;
    }
    std::wstring all;
    for (std::size_t i = 0; i < output_lines_.size(); ++i) {
        if (i > 0) {
            all += L"\r\n";
        }
        all += output_lines_[i];
    }
    SetWindowTextW(output_edit_, all.c_str());
    scroll_output_to_end();
}

void WorkbenchPanel::scroll_output_to_end() {
    if (!output_edit_) {
        return;
    }
    SendMessageW(output_edit_, WM_VSCROLL, SB_BOTTOM, 0);
    refresh_thin_scrollbar(output_edit_);
}

bool WorkbenchPanel::handle_command(int id, HWND /*ctrl*/) {
    if (id >= kIdSurface0 && id < kIdSurface0 + 4) {
        set_surface(static_cast<PanelSurface>(id - kIdSurface0));
        return true;
    }
    if (id >= kIdSessionTab0 && id < kIdSessionTab0 + kMaxSessionTabs) {
        if (on_activate_session_) {
            on_activate_session_(id - kIdSessionTab0);
        }
        return true;
    }
    if (id >= kIdSessionClose0 && id < kIdSessionClose0 + kMaxSessionTabs) {
        if (on_close_session_) {
            on_close_session_(id - kIdSessionClose0, /*confirm=*/false);
        }
        return true;
    }
    switch (id) {
        case kIdProblemsRefresh:
            if (on_problems_refresh_) {
                on_problems_refresh_();
            }
            return true;
        case kIdOutputClear:
            clear_output();
            return true;
        case kIdProblemsList: {
            if (!on_problem_activate_ || problems_.empty()) {
                return true;
            }
            const int sel = static_cast<int>(SendMessageW(problems_list_, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(problems_.size())) {
                on_problem_activate_(problems_[static_cast<std::size_t>(sel)].ref);
            }
            return true;
        }
        case kIdNew:
            if (on_new_) {
                on_new_();
            }
            return true;
        case kIdNewMenu:
            show_new_menu();
            return true;
        case kIdCollapse:
            if (on_collapse_) {
                on_collapse_();
            }
            return true;
        case kIdMaximize:
            if (on_maximize_) {
                on_maximize_();
            }
            return true;
        case kIdHide:
            if (on_hide_) {
                on_hide_();
            }
            return true;
        default:
            return false;
    }
}

bool WorkbenchPanel::handle_draw_item(const DRAWITEMSTRUCT* di) {
    if (!di || !di->hDC) {
        return false;
    }
    const int id = static_cast<int>(di->CtlID);
    if (id >= kIdSurface0 && id < kIdSurface0 + 4) {
        const int idx = id - kIdSurface0;
        const bool active = static_cast<int>(surface_) == idx;
        fill_rect(di->hDC, di->rcItem, active ? kPanelRaised() : kPanelBg());
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, active ? kPanelText() : kPanelMuted());
        const wchar_t* label = panel_surface_label(static_cast<PanelSurface>(idx));
        DrawTextW(di->hDC, label, -1, const_cast<RECT*>(&di->rcItem),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (active) {
            RECT under = di->rcItem;
            under.top = under.bottom - 2;
            fill_rect(di->hDC, under, kPanelAccent());
        }
        return true;
    }
    if (id == kIdNew || id == kIdNewMenu || id == kIdCollapse || id == kIdMaximize || id == kIdHide) {
        const bool hot = (di->itemState & ODS_SELECTED) != 0;
        fill_rect(di->hDC, di->rcItem, hot ? theme().hover : kPanelRaised());
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, kPanelText());
        wchar_t text[8]{};
        GetWindowTextW(di->hwndItem, text, 8);
        DrawTextW(di->hDC, text, -1, const_cast<RECT*>(&di->rcItem),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return true;
    }
    if (id == kIdContent) {
        fill_rect(di->hDC, di->rcItem, kPanelBg());
        return true;
    }
    // Problems / Output actions are kit buttons, so they paint through the shared kit renderer
    // rather than the panel's own two-tone chrome.
    return ui_kit::draw_kit_item(di, font_small_ ? font_small_ : font_);
}

bool WorkbenchPanel::handle_notify(NMHDR* /*hdr*/) {
    return false;
}

LRESULT CALLBACK WorkbenchPanel::wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WorkbenchPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<WorkbenchPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->hwnd_ = hwnd;
        }
    } else {
        self = reinterpret_cast<WorkbenchPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->on_message(msg, wParam, lParam);
}

LRESULT WorkbenchPanel::on_message(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            // A Problems row opens its document on double-click only; selection changes must not
            // yank the editor out from under a user who is arrowing through the list.
            if (id == kIdProblemsList && HIWORD(wParam) != LBN_DBLCLK) {
                return 0;
            }
            if (handle_command(id, reinterpret_cast<HWND>(lParam))) {
                return 0;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (handle_draw_item(di)) {
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND ctrl = reinterpret_cast<HWND>(lParam);
            // A read-only EDIT reports as static; the output log and the problems list carry
            // primary text, everything else on the panel stays muted chrome.
            const bool body = ctrl && (ctrl == output_edit_ || ctrl == problems_list_);
            SetTextColor(dc, body ? kPanelText() : kPanelMuted());
            SetBkColor(dc, kPanelBg());
            return reinterpret_cast<LRESULT>(bg_ ? bg_ : GetStockObject(BLACK_BRUSH));
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            fill_rect(dc, rc, kPanelBg());
            // Session tab strip painted here when Terminal surface. No child window covers this
            // rect, so the tabs actually reach the screen.
            if (session_strip_visible_) {
                const RECT src = session_strip_;
                fill_rect(dc, src, kPanelBg());
                const int tab_w = dip(hwnd_, kSessionTabWDip);
                const int close_w = dip(hwnd_, kSessionCloseWDip);
                const int gap = dip(hwnd_, ui_space::kPadXsDip);
                int tx = src.left;
                for (int i = 0; i < session_count_ && i < kMaxSessionTabs; ++i) {
                    RECT tr{tx, src.top + 2, tx + tab_w, src.bottom - 2};
                    const bool active = i == active_session_;
                    fill_rect(dc, tr, active ? kPanelRaised() : kPanelBg());
                    const COLORREF accent =
                        (i < static_cast<int>(session_accents_.size())) ? session_accents_[static_cast<std::size_t>(i)]
                                                                        : terminal_accent_for_index(static_cast<std::size_t>(i));
                    // An exited shell keeps its tab but loses its accent underline and gains an
                    // "(exited)" suffix, so a dead session is never mistaken for an idle one.
                    const bool alive = i >= static_cast<int>(session_alive_.size()) ||
                                       session_alive_[static_cast<std::size_t>(i)];
                    if (active) {
                        RECT under = tr;
                        under.top = under.bottom - 2;
                        fill_rect(dc, under, alive ? accent : kPanelBorder());
                    }
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, (active && alive) ? kPanelText() : kPanelMuted());
                    RECT text_rc = tr;
                    text_rc.left += 6;
                    text_rc.right -= close_w + 4;
                    std::wstring label = L"Terminal";
                    if (i < static_cast<int>(session_titles_.size()) && !session_titles_[static_cast<std::size_t>(i)].empty()) {
                        label = session_titles_[static_cast<std::size_t>(i)];
                    }
                    if (!alive) {
                        const int code = (i < static_cast<int>(session_exit_code_.size()))
                                             ? session_exit_code_[static_cast<std::size_t>(i)]
                                             : -1;
                        if (code >= 0) {
                            label += code == 0 ? L" (exited 0)" : L" (exited " + std::to_wstring(code) + L")";
                        } else {
                            label += L" (exited)";
                        }
                    }
                    DrawTextW(dc, label.c_str(), -1, &text_rc,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    RECT cr{tr.right - close_w - 2, tr.top, tr.right - 2, tr.bottom};
                    SetTextColor(dc, kPanelMuted());
                    DrawTextW(dc, L"×", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    tx += tab_w + gap;
                }
            }
            // Top border line
            HPEN pen = CreatePen(PS_SOLID, 1, kPanelBorder());
            HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
            MoveToEx(dc, rc.left, rc.top, nullptr);
            LineTo(dc, rc.right, rc.top);
            SelectObject(dc, old);
            DeleteObject(pen);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            bool on_close = false;
            const int hit = tab_hit_test(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &on_close);
            if (hit < 0) {
                break;
            }
            if (on_close && on_close_session_) {
                on_close_session_(hit, /*confirm=*/false);
            } else if (on_activate_session_) {
                on_activate_session_(hit);
            }
            return 0;
        }
        case WM_MBUTTONUP: {
            // Middle-click closes a session tab, matching the editor tab strip.
            bool on_close = false;
            const int hit = tab_hit_test(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &on_close);
            if (hit >= 0 && on_close_session_) {
                on_close_session_(hit, /*confirm=*/false);
                return 0;
            }
            break;
        }
        case WM_RBUTTONUP: {
            const int mx = GET_X_LPARAM(lParam);
            const int my = GET_Y_LPARAM(lParam);
            const int hit = tab_hit_test(mx, my, nullptr);
            if (hit < 0) {
                break;
            }
            // Act on what the menu names: select the tab first so Rename/Restart/Duplicate and
            // the visible session cannot disagree.
            if (hit != active_session_ && on_activate_session_) {
                on_activate_session_(hit);
            }
            show_session_menu(hit, mx, my);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

}  // namespace scyllagpt
