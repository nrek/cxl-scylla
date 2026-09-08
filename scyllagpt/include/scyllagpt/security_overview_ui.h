#pragma once

// Settings → Security → Overview — app Keyring + project summary (Slice A).

#include "scyllagpt/commands.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/project_environment.h"
#include "scyllagpt/settings.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class SecurityOverviewAction {
    None = 0,
    GotoKeyring,
    GotoEnvironments,
    GotoPolicy,
    LockKeyring,
};

class SecurityOverviewUi {
public:
    bool create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small);
    void destroy();
    void set_fonts(HFONT font, HFONT font_small);
    void set_visible(bool visible);
    bool visible() const { return visible_; }
    void layout(const RECT& area);
    void reload(Keyring& keyring, ProjectEnvironmentManager& envs, const std::string& project_id,
                const std::wstring& project_name, const ExecutionPolicy& policy);

    // Returns a navigation request after a handled click.
    SecurityOverviewAction on_command(WORD id, WORD notify);

    bool owns_hwnd(HWND child) const;

private:
    void hide_all();
    std::vector<HWND> all_controls() const;

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    bool visible_ = false;

    HWND title_ = nullptr;
    HWND kr_heading_ = nullptr;
    HWND kr_status_ = nullptr;
    HWND kr_primary_ = nullptr;
    HWND kr_manage_ = nullptr;
    HWND proj_heading_ = nullptr;
    HWND proj_status_ = nullptr;
    HWND proj_env_btn_ = nullptr;
    HWND proj_secrets_btn_ = nullptr;
    HWND pol_heading_ = nullptr;
    HWND pol_status_ = nullptr;
    HWND pol_btn_ = nullptr;

    bool vault_exists_ = false;
    bool unlocked_ = false;
};

}  // namespace scyllagpt
