#pragma once

#include "scyllagpt/terminal_session.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class PanelSurface {
    Terminal = 0,
    Problems,
    Output,
    Ports,
};

const wchar_t* panel_surface_label(PanelSurface s);
PanelSurface parse_panel_surface(std::string_view raw);
std::string panel_surface_string(PanelSurface s);

// One row on the Problems surface. There is no language-server pipeline yet, so v0 carries
// workbench-level findings (unsaved documents); |ref| is the caller's own row handle.
struct WorkbenchProblem {
    std::wstring primary;
    std::wstring secondary;
    int ref = -1;
};

// Bottom workbench chrome: surface tabs, session tabs, New Terminal, collapse/hide.
// Does not own TerminalHost instances — those live in TerminalSessionManager.
class WorkbenchPanel {
public:
    using ActionFn = std::function<void()>;
    using NewProfileFn = std::function<void(const std::string& profile_id)>;
    // |confirm| is set for close requests that came from the tab context menu, where the plan
    // asks for a prompt before a live shell dies. The × glyph closes without ceremony.
    using CloseSessionFn = std::function<void(int index, bool confirm)>;
    using ActivateSessionFn = std::function<void(int index)>;
    using SessionFn = std::function<void(int index)>;

    WorkbenchPanel() = default;
    ~WorkbenchPanel();

    WorkbenchPanel(const WorkbenchPanel&) = delete;
    WorkbenchPanel& operator=(const WorkbenchPanel&) = delete;

    bool create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small);
    void destroy();

    HWND hwnd() const { return hwnd_; }
    bool created() const { return hwnd_ != nullptr; }

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    // Re-point cached fonts after a DPI change (owner-draw painting uses these).
    void set_fonts(HFONT font, HFONT font_small) {
        font_ = font;
        font_small_ = font_small ? font_small : font;
    }

    void set_surface(PanelSurface s);
    PanelSurface surface() const { return surface_; }

    void set_maximized(bool on);
    bool maximized() const { return maximized_; }

    void set_collapsed(bool on);
    bool collapsed() const { return collapsed_; }

    // Layout panel chrome into |rc| (client coords of parent). Returns content rect in
    // parent coords (for size); terminal hosts parented to content_hwnd() use local 0,0,w,h.
    RECT layout(const RECT& panel_rc);

    // Raise surface tabs / New / window controls above content_ (creation order puts content
    // later, which otherwise steals hits from chrome).
    void raise_chrome();

    void refresh_session_tabs(const TerminalSessionManager& mgr);
    void set_enabled_profiles(std::vector<TerminalProfile> profiles);
    const std::vector<std::wstring>& session_titles() const { return session_titles_; }

    // Problems surface (v0): caller supplies the rows; the panel owns presentation + empty state.
    void set_problems(std::vector<WorkbenchProblem> items);

    // Output surface: append-only workbench log. Safe before create() — lines are buffered and
    // flushed into the control when the panel is built.
    void append_output(const std::wstring& channel, const std::wstring& line);
    void clear_output();

    void set_on_hide(ActionFn fn) { on_hide_ = std::move(fn); }
    void set_on_collapse(ActionFn fn) { on_collapse_ = std::move(fn); }
    void set_on_maximize(ActionFn fn) { on_maximize_ = std::move(fn); }
    void set_on_new_terminal(ActionFn fn) { on_new_ = std::move(fn); }
    void set_on_new_with_profile(NewProfileFn fn) { on_new_profile_ = std::move(fn); }
    void set_on_close_session(CloseSessionFn fn) { on_close_session_ = std::move(fn); }
    void set_on_activate_session(ActivateSessionFn fn) { on_activate_session_ = std::move(fn); }
    void set_on_rename_session(SessionFn fn) { on_rename_session_ = std::move(fn); }
    void set_on_restart_session(SessionFn fn) { on_restart_session_ = std::move(fn); }
    void set_on_duplicate_session(SessionFn fn) { on_duplicate_session_ = std::move(fn); }
    void set_on_surface_changed(ActionFn fn) { on_surface_ = std::move(fn); }
    void set_on_problems_refresh(ActionFn fn) { on_problems_refresh_ = std::move(fn); }
    // Argument is the WorkbenchProblem::ref of the activated row, not its list index.
    void set_on_problem_activate(SessionFn fn) { on_problem_activate_ = std::move(fn); }

    // Forwarded from parent WndProc when child notifies.
    bool handle_command(int id, HWND ctrl);
    bool handle_draw_item(const DRAWITEMSTRUCT* di);
    bool handle_notify(NMHDR* hdr);

    // Content area for active surface (terminal host or stub label).
    HWND content_hwnd() const { return content_; }
    void paint_stub_if_needed();

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT on_message(UINT msg, WPARAM wParam, LPARAM lParam);
    void show_new_menu();
    void show_session_menu(int index, int client_x, int client_y);
    void update_stub_label();
    void refill_problems();
    void rebuild_output_edit();
    void scroll_output_to_end();
    // Client-coord rect of the session-tab strip. Owned by the panel (no child window), so
    // WM_PAINT and WM_LBUTTONDOWN agree on where the tabs are.
    RECT session_strip_{};
    bool session_strip_visible_ = false;
    int tab_hit_test(int mx, int my, bool* on_close) const;

    HWND parent_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND btn_new_ = nullptr;
    HWND btn_new_menu_ = nullptr;
    HWND btn_collapse_ = nullptr;
    HWND btn_maximize_ = nullptr;
    HWND btn_hide_ = nullptr;
    HWND content_ = nullptr;
    HWND stub_label_ = nullptr;
    HWND tip_ = nullptr;
    HWND surface_btns_[4]{};
    HWND problems_lbl_ = nullptr;
    HWND problems_list_ = nullptr;
    HWND btn_problems_refresh_ = nullptr;
    HWND output_lbl_ = nullptr;
    HWND output_edit_ = nullptr;
    HWND btn_output_clear_ = nullptr;

    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    HBRUSH bg_ = nullptr;

    PanelSurface surface_ = PanelSurface::Terminal;
    bool visible_ = false;
    bool maximized_ = false;
    bool collapsed_ = false;
    bool in_surface_change_ = false;  // re-entrancy guard: on_surface_ calls back into set_surface
    bool notified_surface_ = false;   // ensures the first set_surface always notifies

    std::vector<TerminalProfile> enabled_profiles_;
    std::vector<std::wstring> session_titles_;
    std::vector<COLORREF> session_accents_;
    std::vector<bool> session_alive_;
    std::vector<int> session_exit_code_;   // -1 when the exit code is unknown
    int active_session_ = -1;
    int session_count_ = 0;

    std::vector<WorkbenchProblem> problems_;
    std::vector<std::wstring> output_lines_;

    ActionFn on_hide_;
    ActionFn on_collapse_;
    ActionFn on_maximize_;
    ActionFn on_new_;
    NewProfileFn on_new_profile_;
    CloseSessionFn on_close_session_;
    ActivateSessionFn on_activate_session_;
    SessionFn on_rename_session_;
    SessionFn on_restart_session_;
    SessionFn on_duplicate_session_;
    ActionFn on_surface_;
    ActionFn on_problems_refresh_;
    SessionFn on_problem_activate_;
};

}  // namespace scyllagpt
