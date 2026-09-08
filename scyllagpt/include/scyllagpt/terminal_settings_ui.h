#pragma once

#include "scyllagpt/commands.h"
#include "scyllagpt/settings.h"
#include "scyllagpt/terminal_profiles.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

// Settings → Terminal interactive panel (profiles, default, policy, custom).
class TerminalSettingsUi {
public:
    static constexpr UINT IdHeading = 2590;
    static constexpr UINT IdDesc = 2597;
    static constexpr UINT IdProfiles = Cmd_SetTermProfiles;
    static constexpr UINT IdToggleEnable = Cmd_SetTermToggleEnable;
    static constexpr UINT IdDefaultLbl = 2591;
    static constexpr UINT IdDefault = Cmd_SetTermDefault;
    static constexpr UINT IdPolicyLbl = 2592;
    static constexpr UINT IdPolicy = Cmd_SetTermPolicy;
    static constexpr UINT IdRefresh = Cmd_SetTermRefresh;
    static constexpr UINT IdCustomHeading = 2593;
    static constexpr UINT IdCustomName = Cmd_SetTermCustomName;
    static constexpr UINT IdCustomExe = Cmd_SetTermCustomExe;
    static constexpr UINT IdCustomBrowse = Cmd_SetTermCustomBrowse;
    static constexpr UINT IdCustomArgs = Cmd_SetTermCustomArgs;
    static constexpr UINT IdAddCustom = Cmd_SetTermAddCustom;
    static constexpr UINT IdNameLbl = 2594;
    static constexpr UINT IdExeLbl = 2595;
    static constexpr UINT IdArgsLbl = 2596;
    static constexpr UINT IdRemoveCustom = 2598;
    static constexpr UINT IdCwdModeLbl = 2599;
    static constexpr UINT IdCwdMode = Cmd_SetTermCwdMode;
    static constexpr UINT IdCwdPathLbl = 2585;
    static constexpr UINT IdCwdPath = Cmd_SetTermCwdPath;
    static constexpr UINT IdCwdBrowse = Cmd_SetTermCwdBrowse;
    static constexpr UINT IdEditCustom = Cmd_SetTermEditCustom;
    static constexpr UINT IdDupCustom = Cmd_SetTermDupCustom;
    static constexpr UINT IdCancelEdit = Cmd_SetTermCancelEdit;

    // Visible rows before scrollbar; row includes 5px bottom gap.
    static constexpr int kVisibleProfileRows = 8;
    static constexpr int kProfileRowDip = 52;  // primary + secondary + spacing + 5px bottom
    static constexpr int kProfileBottomPadDip = 5;

    bool create(HWND parent, HINSTANCE inst, HFONT font);
    void destroy();

    // Re-point the cached font after a DPI change.
    void set_fonts(HFONT font) { font_ = font; }

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void layout(const RECT& area);
    void reload(std::vector<TerminalProfile>& profiles, Settings& settings);

    // Persist enablement/default/policy into settings; custom into terminals_path.
    bool on_command(WORD id, WORD notify, HWND owner, std::vector<TerminalProfile>& profiles, Settings& settings,
                    const std::wstring& settings_path, const std::wstring& terminals_path);

    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

    // Enter in any custom-profile field saves the profile. Esc only means something while an
    // existing profile is being edited, where there is pending state to abandon.
    UINT default_command(HWND field) const {
        if (field && (field == custom_name_ || field == custom_exe_ || field == custom_args_ ||
                      field == custom_cwd_)) {
            return IdAddCustom;
        }
        return 0;
    }
    UINT cancel_command() const { return editing_id_.empty() ? 0 : IdCancelEdit; }

    void hide_all();

private:
    void fill_profiles(const std::vector<TerminalProfile>& profiles);
    void fill_default_select(const std::vector<TerminalProfile>& profiles, const std::string& default_id);
    void fill_policy_select(const std::string& policy);
    void fill_cwd_mode_select(WorkingDirectoryMode mode);
    WorkingDirectoryMode selected_cwd_mode() const;
    // Swap the form between "Add custom profile" and editing an existing custom profile.
    void begin_edit(const TerminalProfile& profile);
    void end_edit();
    void clear_form();
    // Persist every custom row plus the enablement map after the profile vector changed.
    bool commit_profiles(HWND owner, std::vector<TerminalProfile>& profiles, Settings& settings,
                         const std::wstring& settings_path, const std::wstring& terminals_path);
    bool persist_prefs(Settings& settings, const std::wstring& settings_path,
                       const std::vector<TerminalProfile>& profiles);
    void apply_fonts();
    void toggle_profile_at(int index, std::vector<TerminalProfile>& profiles, Settings& settings,
                           const std::wstring& settings_path, const std::wstring& terminals_path);
    void update_profile_list_scroll();
    // Gray out Toggle / Remove when they have nothing to act on, instead of no-oping silently.
    void update_action_enabled();
    int selected_index() const;
    // One roster for show/hide/font passes — three copies of the list meant a new control was
    // routinely added to create() and forgotten in hide_all().
    std::vector<HWND> all_controls() const;

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    bool visible_ = false;
    std::vector<TerminalProfile> cached_profiles_;
    // Empty while adding; holds the profile id being edited otherwise.
    std::string editing_id_;

    HWND heading_ = nullptr;
    HWND desc_ = nullptr;
    HWND profiles_ = nullptr;
    HWND btn_toggle_ = nullptr;
    HWND btn_edit_ = nullptr;
    HWND btn_dup_ = nullptr;
    HWND btn_remove_ = nullptr;
    HWND default_lbl_ = nullptr;
    HWND default_select_ = nullptr;
    HWND policy_lbl_ = nullptr;
    HWND policy_select_ = nullptr;
    HWND btn_refresh_ = nullptr;
    HWND custom_heading_ = nullptr;
    HWND name_lbl_ = nullptr;
    HWND custom_name_ = nullptr;
    HWND exe_lbl_ = nullptr;
    HWND custom_exe_ = nullptr;
    HWND btn_browse_ = nullptr;
    HWND args_lbl_ = nullptr;
    HWND custom_args_ = nullptr;
    HWND cwd_mode_lbl_ = nullptr;
    HWND cwd_mode_ = nullptr;
    HWND cwd_path_lbl_ = nullptr;
    HWND custom_cwd_ = nullptr;
    HWND btn_cwd_browse_ = nullptr;
    HWND btn_add_ = nullptr;
    HWND btn_cancel_edit_ = nullptr;
};

}  // namespace scyllagpt
