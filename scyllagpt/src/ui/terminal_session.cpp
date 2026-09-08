#include "scyllagpt/terminal_session.h"

#include <cstdio>

namespace scyllagpt {
namespace {

const COLORREF kAccents[] = {
    RGB(0xE6, 0x94, 0x05),  // amber
    RGB(0x5B, 0xB8, 0xC8),  // cyan
    RGB(0x89, 0xBC, 0x9D),  // green
    RGB(0xC4, 0x9A, 0xD8),  // purple
    RGB(0xEF, 0x96, 0x96),  // coral
    RGB(0x7E, 0xA8, 0xE8),  // blue
};

}  // namespace

COLORREF terminal_accent_for_index(std::size_t index) {
    return kAccents[index % (sizeof(kAccents) / sizeof(kAccents[0]))];
}

std::string make_terminal_session_id(std::size_t seq) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "term-%zu", seq);
    return buf;
}

std::wstring make_terminal_session_title(std::wstring_view profile_name, int index_1based) {
    std::wstring t(profile_name);
    if (index_1based > 1) {
        t += L" ";
        t += std::to_wstring(index_1based);
    }
    return t;
}

std::wstring make_terminal_session_title_with_env(std::wstring_view profile_name, int index_1based,
                                                 std::wstring_view env_name, bool human_only) {
    std::wstring t = make_terminal_session_title(profile_name, index_1based);
    if (!env_name.empty()) {
        t += L" · ";
        t += env_name;
    }
    if (human_only) {
        t += L" · Human Only";
    }
    return t;
}

TerminalSessionManager::~TerminalSessionManager() {
    destroy_all();
}

TerminalSession* TerminalSessionManager::active_session() {
    if (active_ < 0 || active_ >= static_cast<int>(sessions_.size())) {
        return nullptr;
    }
    return &sessions_[static_cast<std::size_t>(active_)];
}

const TerminalSession* TerminalSessionManager::active_session() const {
    if (active_ < 0 || active_ >= static_cast<int>(sessions_.size())) {
        return nullptr;
    }
    return &sessions_[static_cast<std::size_t>(active_)];
}

TerminalSession* TerminalSessionManager::session_at(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return nullptr;
    }
    return &sessions_[static_cast<std::size_t>(index)];
}

TerminalSession* TerminalSessionManager::find_by_id(std::string_view id) {
    for (auto& s : sessions_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

bool TerminalSessionManager::create_session(HWND parent, HWND notify, HINSTANCE inst, int control_id_base,
                                           const TerminalProfile& profile, const std::wstring& cwd,
                                           const TerminalCreateOpts& opts, std::wstring* error) {
    if (!parent) {
        if (error) {
            *error = L"No parent HWND for terminal session.";
        }
        return false;
    }

    int same_profile = 0;
    for (const auto& s : sessions_) {
        if (s.profile_id == profile.id) {
            ++same_profile;
        }
    }

    TerminalSession sess;
    sess.id = make_terminal_session_id(next_seq_++);
    sess.title = make_terminal_session_title_with_env(profile.name, same_profile + 1, opts.environment_name,
                                                     opts.human_only);
    sess.profile_id = profile.id;
    sess.environment_id = opts.environment_id;
    sess.environment_name = opts.environment_name;
    sess.human_only = opts.human_only;
    sess.accent = terminal_accent_for_index(sessions_.size());
    sess.alive = true;
    sess.cwd = cwd;
    sess.host = std::make_unique<TerminalHost>();

    const int control_id = control_id_base + static_cast<int>(sessions_.size());
    if (!sess.host->create(parent, notify, control_id, inst, profile, cwd, opts.environment, error)) {
        return false;
    }
    sess.host->set_visible(false);

    sessions_.push_back(std::move(sess));
    active_ = static_cast<int>(sessions_.size()) - 1;
    sync_host_visibility();
    return true;
}

bool TerminalSessionManager::close_session(std::string_view id) {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
        if (sessions_[i].id == id) {
            return close_session_at(static_cast<int>(i));
        }
    }
    return false;
}

bool TerminalSessionManager::close_session_at(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return false;
    }
    if (sessions_[static_cast<std::size_t>(index)].host) {
        sessions_[static_cast<std::size_t>(index)].host->destroy();
    }
    sessions_.erase(sessions_.begin() + index);
    if (sessions_.empty()) {
        active_ = -1;
    } else if (active_ >= static_cast<int>(sessions_.size())) {
        active_ = static_cast<int>(sessions_.size()) - 1;
    } else if (active_ > index) {
        --active_;
    }
    sync_host_visibility();
    return true;
}

bool TerminalSessionManager::rename_session_at(int index, const std::wstring& title) {
    auto* s = session_at(index);
    if (!s || title.empty()) {
        return false;
    }
    s->title = title;
    return true;
}

bool TerminalSessionManager::restart_session_at(int index, HWND parent, HWND notify, HINSTANCE inst,
                                               int control_id_base, const TerminalProfile& profile,
                                               const std::wstring& cwd, const TerminalCreateOpts& opts,
                                               std::wstring* error) {
    auto* s = session_at(index);
    if (!s || !parent) {
        if (error) {
            *error = L"No such terminal session.";
        }
        return false;
    }
    if (s->host) {
        s->host->destroy();
    }
    s->alive = false;
    s->has_exit_code = false;
    s->exit_code = 0;
    s->host = std::make_unique<TerminalHost>();
    if (!s->host->create(parent, notify, control_id_base + index, inst, profile, cwd, opts.environment, error)) {
        s->host.reset();
        sync_host_visibility();
        return false;
    }
    s->profile_id = profile.id;
    s->cwd = cwd;
    s->environment_id = opts.environment_id;
    s->environment_name = opts.environment_name;
    s->human_only = opts.human_only;
    s->alive = true;
    s->host->set_visible(false);
    sync_host_visibility();
    return true;
}

std::vector<std::wstring> TerminalSessionManager::alive_titles() const {
    std::vector<std::wstring> out;
    for (const auto& s : sessions_) {
        if (s.alive) {
            out.push_back(s.title);
        }
    }
    return out;
}

bool TerminalSessionManager::set_active(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return false;
    }
    active_ = index;
    sync_host_visibility();
    return true;
}

bool TerminalSessionManager::set_active_id(std::string_view id) {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
        if (sessions_[i].id == id) {
            return set_active(static_cast<int>(i));
        }
    }
    return false;
}

void TerminalSessionManager::set_panel_visible(bool visible) {
    panel_visible_ = visible;
    sync_host_visibility();
}

void TerminalSessionManager::layout_active(int x, int y, int w, int h) {
    auto* s = active_session();
    if (!s || !s->host) {
        return;
    }
    // Hosts are parented to WorkbenchPanel::content_hwnd(); coordinates are local to that
    // content window (typically 0,0,w,h), not the main frame.
    s->host->move(x, y, w, h);

}

void TerminalSessionManager::reparent_hosts(HWND parent) {
    if (!parent) {
        return;
    }
    for (auto& s : sessions_) {
        if (!s.host) {
            continue;
        }
        HWND h = s.host->hwnd();
        if (!h || !IsWindow(h)) {
            continue;
        }
        if (GetParent(h) != parent) {
            SetParent(h, parent);
        }
    }
}

void TerminalSessionManager::poll_all() {
    for (auto& s : sessions_) {
        if (!s.host) {
            continue;
        }
        // Stop polling a session whose process has exited. The pipe is drained once on the
        // transition to !alive; after that this ran a pipe read per timer tick per dead tab forever.
        if (!s.alive) {
            continue;
        }
        std::string out;
        s.host->poll(&out);
        if (!s.host->running()) {
            // Final drain so the last lines of output are not lost with the process.
            s.host->poll(&out);
            s.alive = false;
            DWORD code = 0;
            if (s.host->exited(&code)) {
                s.exit_code = code;
                s.has_exit_code = true;
            }
        }
    }
}

bool TerminalSessionManager::focus_active() {
    auto* s = active_session();
    if (!s || !s->host || !panel_visible_) {
        return false;
    }
    HWND h = s->host->hwnd();
    if (!h || !IsWindowVisible(h)) {
        return false;
    }
    SetFocus(h);
    return true;
}

void TerminalSessionManager::destroy_all() {
    for (auto& s : sessions_) {
        if (s.host) {
            s.host->destroy();
        }
    }
    sessions_.clear();
    active_ = -1;
}

void TerminalSessionManager::sync_host_visibility() {
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
        auto& s = sessions_[i];
        if (!s.host) {
            continue;
        }
        const bool show = panel_visible_ && static_cast<int>(i) == active_;
        s.host->set_visible(show);
    }
}

}  // namespace scyllagpt
