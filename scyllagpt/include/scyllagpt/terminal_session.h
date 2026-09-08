#pragma once

#include "scyllagpt/terminal_host.h"
#include "scyllagpt/terminal_profiles.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

struct TerminalSession {
    std::string id;
    std::wstring title;
    std::string profile_id;
    std::string environment_id;
    std::wstring environment_name;
    bool human_only = false;
    std::unique_ptr<TerminalHost> host;
    COLORREF accent = RGB(0xE6, 0x94, 0x05);
    bool alive = true;
    // Resolved at spawn time so Restart can respawn the same shell in the same place.
    std::wstring cwd;
    DWORD exit_code = 0;
    bool has_exit_code = false;
};

struct TerminalCreateOpts {
    std::string environment_id;
    std::wstring environment_name;
    bool human_only = false;
    const std::wstring* environment = nullptr;  // Unicode env block or nullptr
};

// Pure helpers (unit-testable).
COLORREF terminal_accent_for_index(std::size_t index);
std::string make_terminal_session_id(std::size_t seq);
std::wstring make_terminal_session_title(std::wstring_view profile_name, int index_1based);
std::wstring make_terminal_session_title_with_env(std::wstring_view profile_name, int index_1based,
                                                 std::wstring_view env_name, bool human_only);

// Owns concurrent TerminalHost instances. Hide panel ≠ destroy sessions.
class TerminalSessionManager {
public:
    TerminalSessionManager() = default;
    ~TerminalSessionManager();

    TerminalSessionManager(const TerminalSessionManager&) = delete;
    TerminalSessionManager& operator=(const TerminalSessionManager&) = delete;

    const std::vector<TerminalSession>& sessions() const { return sessions_; }
    std::size_t count() const { return sessions_.size(); }
    int active_index() const { return active_; }
    TerminalSession* active_session();
    const TerminalSession* active_session() const;
    TerminalSession* session_at(int index);
    TerminalSession* find_by_id(std::string_view id);

    // Create + start a host under |parent|. |notify| is the main frame (WM_APP+40); never content_.
    // control_id base is offset by session index.
    bool create_session(HWND parent, HWND notify, HINSTANCE inst, int control_id_base,
                        const TerminalProfile& profile, const std::wstring& cwd,
                        const TerminalCreateOpts& opts = {}, std::wstring* error = nullptr);

    // Kill one session (explicit close). Returns true if removed.
    bool close_session(std::string_view id);
    bool close_session_at(int index);

    // Instance-level rename (tab title only; the profile is untouched).
    bool rename_session_at(int index, const std::wstring& title);

    // Terminate the process tree and respawn in place, keeping id / title / accent / tab position.
    // The caller re-resolves profile, cwd, and environment so secret material never lives here.
    bool restart_session_at(int index, HWND parent, HWND notify, HINSTANCE inst, int control_id_base,
                            const TerminalProfile& profile, const std::wstring& cwd,
                            const TerminalCreateOpts& opts = {}, std::wstring* error = nullptr);

    // Titles of sessions whose shell is still running (project-close warning).
    std::vector<std::wstring> alive_titles() const;

    // Switch which host HWND is shown; others stay alive but hidden.
    bool set_active(int index);
    bool set_active_id(std::string_view id);

    // Panel visibility: hide/show hosts without destroying processes.
    void set_panel_visible(bool visible);
    bool panel_visible() const { return panel_visible_; }

    void layout_active(int x, int y, int w, int h);
    // Move existing host HWNDs under |parent| (e.g. after parenting fix / panel recreate).
    void reparent_hosts(HWND parent);
    void poll_all();

    // Move keyboard focus into the visible session's host. Returns false when there is nothing
    // focusable (no sessions, or panel hidden).
    bool focus_active();

    void destroy_all();

private:
    void sync_host_visibility();

    std::vector<TerminalSession> sessions_;
    int active_ = -1;
    bool panel_visible_ = false;
    std::size_t next_seq_ = 1;
};

}  // namespace scyllagpt
