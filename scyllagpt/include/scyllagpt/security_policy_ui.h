#pragma once

// Settings → Security → Execution Policy — protected-value rules (Slice B).
// Owns no secrets; it only edits Settings::execution_policy and persists settings.json.

#include "scyllagpt/commands.h"
#include "scyllagpt/settings.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

class SecurityPolicyUi {
public:
    bool create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small);
    void destroy();
    void set_fonts(HFONT font, HFONT font_small);
    void set_visible(bool visible);
    bool visible() const { return visible_; }
    void layout(const RECT& area);
    void reload(const ExecutionPolicy& policy);

    // Returns true when |id| belonged to this page. Persists on change.
    bool on_command(WORD id, WORD notify, HWND owner, Settings& settings,
                    const std::wstring& settings_path);

    bool owns_hwnd(HWND child) const;

private:
    void hide_all();
    std::vector<HWND> all_controls() const;
    void fill_mode_select(HWND select, bool allow_permitted, PolicyMode current);
    void update_strict_line(const ExecutionPolicy& policy);

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    bool visible_ = false;

    HWND title_ = nullptr;
    HWND desc_ = nullptr;
    HWND status_ = nullptr;

    HWND human_lbl_ = nullptr;
    HWND human_ = nullptr;
    HWND human_hint_ = nullptr;
    HWND agent_lbl_ = nullptr;
    HWND agent_ = nullptr;
    HWND agent_hint_ = nullptr;
    HWND recipes_lbl_ = nullptr;
    HWND recipes_ = nullptr;
    HWND recipes_hint_ = nullptr;

    HWND strict_ = nullptr;
    HWND reset_ = nullptr;
};

}  // namespace scyllagpt
