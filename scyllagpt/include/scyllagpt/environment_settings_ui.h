#pragma once

// Settings → Security → Project Environments (Slice A rewrite).
// App Keyring is never created from this page.

#include "scyllagpt/commands.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/project_environment.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class EnvUiNavRequest {
    None = 0,
    OpenKeyring,
};

class EnvironmentSettingsUi {
public:
    bool create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small);
    void destroy();

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void set_fonts(HFONT font, HFONT font_small) {
        font_ = font;
        font_small_ = font_small ? font_small : font;
    }

    void layout(const RECT& area);
    void reload(ProjectEnvironmentManager& mgr, Keyring& keyring, const std::string& project_id,
                const std::wstring& project_name, const std::wstring& persist_path);

    bool on_command(WORD id, WORD notify, HWND owner, ProjectEnvironmentManager& mgr, Keyring& keyring,
                    const std::string& project_id, const std::wstring& persist_path);

    bool owns_hwnd(HWND child) const;
    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

    UINT default_command(HWND field) const;
    UINT cancel_command() const;

    EnvUiNavRequest take_nav_request() {
        const EnvUiNavRequest r = nav_;
        nav_ = EnvUiNavRequest::None;
        return r;
    }

    void hide_all();

private:
    enum class Mode { Landing, AddEnv, Detail, AddVar };

    void set_mode(Mode mode);
    void apply_visibility();
    std::vector<HWND> all_controls() const;
    void fill_landing(ProjectEnvironmentManager& mgr, Keyring& keyring);
    void fill_detail(ProjectEnvironmentManager& mgr, Keyring& keyring);
    void fill_active_select(ProjectEnvironmentManager& mgr);
    void fill_inherit_select(ProjectEnvironmentManager& mgr, HWND select, const std::string& exclude_id);
    void fill_secret_select(Keyring& keyring);
    void update_var_form_fields(Keyring& keyring);
    bool persist(ProjectEnvironmentManager& mgr, const std::wstring& path, HWND owner);
    std::string selected_landing_env_id(ProjectEnvironmentManager& mgr) const;
    std::string selected_var_name(ProjectEnvironmentManager& mgr) const;
    int count_protected(const ProjectEnvironment& e) const;
    bool secret_missing(Keyring& keyring, const std::string& secret_id) const;

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    bool visible_ = false;
    Mode mode_ = Mode::Landing;
    EnvUiNavRequest nav_ = EnvUiNavRequest::None;

    std::string project_id_;
    std::wstring project_name_;
    std::string detail_env_id_;
    std::string editing_var_name_;  // empty = add

    // Landing
    HWND title_ = nullptr;
    HWND desc_ = nullptr;
    HWND project_lbl_ = nullptr;
    HWND active_lbl_ = nullptr;
    HWND active_ = nullptr;
    HWND list_ = nullptr;
    HWND btn_add_ = nullptr;
    HWND btn_manage_ = nullptr;
    HWND btn_set_active_ = nullptr;
    HWND btn_dup_ = nullptr;
    HWND btn_delete_ = nullptr;

    // Detail
    HWND btn_back_ = nullptr;
    HWND detail_title_ = nullptr;
    HWND detail_name_ = nullptr;
    HWND inherit_lbl_ = nullptr;
    HWND inherit_ = nullptr;
    HWND vars_ = nullptr;
    HWND btn_add_var_ = nullptr;
    HWND btn_edit_var_ = nullptr;
    HWND btn_remove_var_ = nullptr;
    HWND btn_detail_dup_ = nullptr;
    HWND btn_detail_delete_ = nullptr;

    // Add environment form
    HWND add_env_name_ = nullptr;
    HWND add_env_inherit_ = nullptr;
    HWND btn_add_env_save_ = nullptr;
    HWND btn_add_env_cancel_ = nullptr;

    // Add / edit variable form
    HWND var_name_ = nullptr;
    HWND var_plain_ = nullptr;
    HWND var_protected_ = nullptr;
    HWND var_value_ = nullptr;
    HWND var_secret_ = nullptr;
    HWND btn_var_unlock_ = nullptr;
    HWND var_avail_human_ = nullptr;
    HWND var_avail_agent_ = nullptr;
    HWND var_avail_recipes_ = nullptr;
    HWND btn_var_save_ = nullptr;
    HWND btn_var_cancel_ = nullptr;

    std::vector<std::string> landing_ids_;
    std::vector<std::wstring> landing_primary_;
    std::vector<std::wstring> landing_secondary_;
    std::vector<std::string> var_names_;
    std::vector<std::wstring> var_primary_;
    std::vector<std::wstring> var_secondary_;
    std::vector<std::string> secret_names_;
    RECT area_{};
};

}  // namespace scyllagpt
