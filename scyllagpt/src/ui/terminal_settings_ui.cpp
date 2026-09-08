#include "scyllagpt/terminal_settings_ui.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>

namespace scyllagpt {
namespace {

constexpr UINT_PTR kProfilesSubclassId = 0x54505246;  // 'TPRF'

// Folder picker for the custom start directory. Matches the Knowledge panel's chooser rather
// than the legacy SHBrowseForFolder tree.
bool pick_start_directory(HWND owner, std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Start directory for this terminal profile");
    if (FAILED(dlg->Show(owner))) {
        dlg->Release();
        return false;
    }
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) {
        dlg->Release();
        return false;
    }
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        out = path;
        CoTaskMemFree(path);
    }
    item->Release();
    dlg->Release();
    return !out.empty();
}

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd ? hwnd : GetDesktopWindow())), 96);
}

LRESULT CALLBACK profiles_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK) {
        const POINTS pt = MAKEPOINTS(lparam);
        const DWORD hit = static_cast<DWORD>(SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y)));
        if (HIWORD(hit) == 0) {
            const int idx = static_cast<int>(LOWORD(hit));
            SendMessageW(hwnd, LB_SETCURSEL, idx, 0);
            HWND parent = GetParent(hwnd);
            if (parent) {
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), LBN_DBLCLK),
                             reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, profiles_subclass, kProfilesSubclassId);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

bool TerminalSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font) {
    if (profiles_) {
        return true;
    }
    parent_ = parent;
    inst_ = inst;
    font_ = font;

    heading_ = ui_kit::create_static(parent, inst, IdHeading, L"Terminal", font, false);
    desc_ = ui_kit::create_static(parent, inst, IdDesc,
                                  L"Enable shells for the bottom panel and choose agent policy.", font, true);
    profiles_ = ui_kit::create_entity_list(parent, inst, IdProfiles, font);
    SetWindowSubclass(profiles_, profiles_subclass, kProfilesSubclassId, 0);
    btn_toggle_ =
        ui_kit::create_button(parent, inst, IdToggleEnable, L"Toggle selected", ui_kit::ButtonKind::Secondary, font);
    btn_edit_ = ui_kit::create_button(parent, inst, IdEditCustom, L"Edit selected", ui_kit::ButtonKind::Secondary, font);
    btn_dup_ = ui_kit::create_button(parent, inst, IdDupCustom, L"Duplicate", ui_kit::ButtonKind::Secondary, font);
    btn_remove_ =
        ui_kit::create_button(parent, inst, IdRemoveCustom, L"Remove custom", ui_kit::ButtonKind::Danger, font);
    default_lbl_ = ui_kit::create_static(parent, inst, IdDefaultLbl, L"Default Terminal:", font, false);
    default_select_ = ui_kit::create_select(parent, inst, IdDefault, font);
    policy_lbl_ = ui_kit::create_static(parent, inst, IdPolicyLbl, L"Agent terminal policy", font, false);
    policy_select_ = ui_kit::create_select(parent, inst, IdPolicy, font);
    btn_refresh_ =
        ui_kit::create_button(parent, inst, IdRefresh, L"Refresh detection", ui_kit::ButtonKind::Secondary, font);
    custom_heading_ = ui_kit::create_static(parent, inst, IdCustomHeading, L"Add custom profile", font, false);
    name_lbl_ = ui_kit::create_static(parent, inst, IdNameLbl, L"Name", font, false);
    custom_name_ = ui_kit::create_text_field(parent, inst, IdCustomName, font);
    exe_lbl_ = ui_kit::create_static(parent, inst, IdExeLbl, L"Executable", font, false);
    custom_exe_ = ui_kit::create_path_field(parent, inst, IdCustomExe, font);
    btn_browse_ = ui_kit::create_button(parent, inst, IdCustomBrowse, L"Browse…", ui_kit::ButtonKind::Secondary, font);
    args_lbl_ = ui_kit::create_static(parent, inst, IdArgsLbl, L"Arguments", font, false);
    custom_args_ = ui_kit::create_text_field(parent, inst, IdCustomArgs, font);
    cwd_mode_lbl_ = ui_kit::create_static(parent, inst, IdCwdModeLbl, L"Start directory", font, false);
    cwd_mode_ = ui_kit::create_select(parent, inst, IdCwdMode, font);
    cwd_path_lbl_ = ui_kit::create_static(parent, inst, IdCwdPathLbl, L"Custom path", font, false);
    custom_cwd_ = ui_kit::create_path_field(parent, inst, IdCwdPath, font);
    btn_cwd_browse_ = ui_kit::create_button(parent, inst, IdCwdBrowse, L"Browse…", ui_kit::ButtonKind::Secondary, font);
    btn_add_ = ui_kit::create_button(parent, inst, IdAddCustom, L"Add custom", ui_kit::ButtonKind::Primary, font);
    btn_cancel_edit_ =
        ui_kit::create_button(parent, inst, IdCancelEdit, L"Cancel", ui_kit::ButtonKind::Secondary, font);

    ui_kit::set_placeholder(custom_name_, L"e.g. Git Bash");
    ui_kit::set_placeholder(custom_exe_, L"Full path to the shell executable");
    ui_kit::set_placeholder(custom_args_, L"Launch arguments (optional)");
    ui_kit::set_placeholder(custom_cwd_, L"Folder the shell starts in");
    fill_cwd_mode_select(WorkingDirectoryMode::Project);

    ui_kit::style_scroll_host(profiles_, theme().panel);
    hide_all();
    return true;
}

void TerminalSettingsUi::destroy() {
    hide_all();
    profiles_ = nullptr;
}

std::vector<HWND> TerminalSettingsUi::all_controls() const {
    return {heading_,    desc_,           profiles_,       btn_toggle_,  btn_edit_,       btn_dup_,
            btn_remove_, default_lbl_,    default_select_, policy_lbl_,  policy_select_,  btn_refresh_,
            custom_heading_, name_lbl_,   custom_name_,    exe_lbl_,     custom_exe_,     btn_browse_,
            args_lbl_,   custom_args_,    cwd_mode_lbl_,   cwd_mode_,    cwd_path_lbl_,   custom_cwd_,
            btn_cwd_browse_, btn_add_,    btn_cancel_edit_};
}

void TerminalSettingsUi::apply_fonts() {
    for (HWND h : all_controls()) {
        if (h && font_) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
}

void TerminalSettingsUi::hide_all() {
    visible_ = false;
    for (HWND h : all_controls()) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
}

void TerminalSettingsUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    for (HWND h : all_controls()) {
        if (h) {
            ShowWindow(h, SW_SHOW);
        }
    }
    update_action_enabled();
}

int TerminalSettingsUi::selected_index() const {
    if (!profiles_) {
        return -1;
    }
    const int sel = static_cast<int>(SendMessageW(profiles_, LB_GETCURSEL, 0, 0));
    return (sel >= 0 && sel < static_cast<int>(cached_profiles_.size())) ? sel : -1;
}

void TerminalSettingsUi::update_action_enabled() {
    const int sel = selected_index();
    const bool has = sel >= 0;
    const bool custom = has && cached_profiles_[static_cast<std::size_t>(sel)].source == TerminalProfileSource::Custom;
    if (btn_toggle_) {
        EnableWindow(btn_toggle_, has ? TRUE : FALSE);
        // Say which way the toggle goes rather than making the user read the checkbox first.
        SetWindowTextW(btn_toggle_, has && cached_profiles_[static_cast<std::size_t>(sel)].enabled ? L"Disable selected"
                                                                                                  : L"Enable selected");
        InvalidateRect(btn_toggle_, nullptr, TRUE);
    }
    if (btn_remove_) {
        // Detected profiles are discovered from the machine; only custom rows are ours to delete.
        EnableWindow(btn_remove_, custom ? TRUE : FALSE);
    }
    if (btn_edit_) {
        // Editing a detected profile in place would rewrite a machine-discovered executable path.
        // Duplicate is the intentional conversion route (§65 of the workbench plan).
        EnableWindow(btn_edit_, custom ? TRUE : FALSE);
    }
    if (btn_dup_) {
        EnableWindow(btn_dup_, has ? TRUE : FALSE);
    }
    if (btn_cancel_edit_) {
        ShowWindow(btn_cancel_edit_, (visible_ && !editing_id_.empty()) ? SW_SHOW : SW_HIDE);
    }
    // A custom path only means anything in Custom mode; leaving it live would suggest the value
    // is used when Project or Home is selected.
    const bool custom_cwd = selected_cwd_mode() == WorkingDirectoryMode::Custom;
    if (custom_cwd_) {
        EnableWindow(custom_cwd_, custom_cwd ? TRUE : FALSE);
    }
    if (btn_cwd_browse_) {
        EnableWindow(btn_cwd_browse_, custom_cwd ? TRUE : FALSE);
    }
}

void TerminalSettingsUi::fill_cwd_mode_select(WorkingDirectoryMode mode) {
    if (!cwd_mode_) {
        return;
    }
    const std::vector<ui_kit::SelectItem> items = {
        {L"Active project", 0},
        {L"User home", 1},
        {L"Custom path", 2},
    };
    ui_kit::select_set_items(cwd_mode_, items);
    int sel = 0;
    if (mode == WorkingDirectoryMode::Home) {
        sel = 1;
    } else if (mode == WorkingDirectoryMode::Custom) {
        sel = 2;
    }
    ui_kit::select_set_index(cwd_mode_, sel);
}

WorkingDirectoryMode TerminalSettingsUi::selected_cwd_mode() const {
    if (!cwd_mode_) {
        return WorkingDirectoryMode::Project;
    }
    switch (ui_kit::select_get_index(cwd_mode_)) {
        case 1:
            return WorkingDirectoryMode::Home;
        case 2:
            return WorkingDirectoryMode::Custom;
        default:
            return WorkingDirectoryMode::Project;
    }
}

void TerminalSettingsUi::clear_form() {
    SetWindowTextW(custom_name_, L"");
    SetWindowTextW(custom_exe_, L"");
    SetWindowTextW(custom_args_, L"");
    SetWindowTextW(custom_cwd_, L"");
    fill_cwd_mode_select(WorkingDirectoryMode::Project);
}

void TerminalSettingsUi::begin_edit(const TerminalProfile& profile) {
    editing_id_ = profile.id;
    SetWindowTextW(custom_name_, profile.name.c_str());
    SetWindowTextW(custom_exe_, profile.executable.c_str());
    SetWindowTextW(custom_args_, profile.args.c_str());
    SetWindowTextW(custom_cwd_, profile.custom_working_directory.c_str());
    fill_cwd_mode_select(profile.working_directory_mode);
    if (custom_heading_) {
        std::wstring heading = L"Edit profile — ";
        heading += profile.name;
        SetWindowTextW(custom_heading_, heading.c_str());
    }
    if (btn_add_) {
        SetWindowTextW(btn_add_, L"Save profile");
        InvalidateRect(btn_add_, nullptr, TRUE);
    }
    update_action_enabled();
    if (custom_name_) {
        SetFocus(custom_name_);
    }
}

void TerminalSettingsUi::end_edit() {
    editing_id_.clear();
    clear_form();
    if (custom_heading_) {
        SetWindowTextW(custom_heading_, L"Add custom profile");
    }
    if (btn_add_) {
        SetWindowTextW(btn_add_, L"Add custom");
        InvalidateRect(btn_add_, nullptr, TRUE);
    }
    update_action_enabled();
}

bool TerminalSettingsUi::commit_profiles(HWND owner, std::vector<TerminalProfile>& profiles, Settings& settings,
                                         const std::wstring& settings_path, const std::wstring& terminals_path) {
    std::vector<TerminalProfile> custom;
    for (const auto& p : profiles) {
        if (p.source == TerminalProfileSource::Custom) {
            custom.push_back(p);
        }
    }
    bool ok = true;
    if (!save_custom_terminal_profiles(terminals_path, custom)) {
        ui_kit::report_save_failure(owner, L"custom terminal profiles", terminals_path);
        ok = false;
    }
    ok = persist_prefs(settings, settings_path, profiles) && ok;
    return ok;
}

void TerminalSettingsUi::update_profile_list_scroll() {
    if (!profiles_) {
        return;
    }
    const int count = static_cast<int>(SendMessageW(profiles_, LB_GETCOUNT, 0, 0));
    LONG style = GetWindowLongW(profiles_, GWL_STYLE);
    if (count > kVisibleProfileRows) {
        style |= WS_VSCROLL;
    } else {
        style &= ~WS_VSCROLL;
    }
    SetWindowLongW(profiles_, GWL_STYLE, style);
    SetWindowPos(profiles_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    refresh_thin_scrollbar(profiles_);
}

void TerminalSettingsUi::layout(const RECT& area) {
    if (!visible_ || !profiles_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    int y = col.top + m.pad_outer;
    const int x0 = col.left + m.pad_outer;
    const int x1 = col.right - m.pad_outer;
    const int w = (std::max)(1, x1 - x0);
    const int label_h = dip(parent_, 18);

    MoveWindow(heading_, x0, y, w, dip(parent_, ui_space::kPageHeaderTitleHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderTitleHDip) + m.pad_tight;
    MoveWindow(desc_, x0, y, w, dip(parent_, ui_space::kPageHeaderDescHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderDescHDip) + m.pad_row;

    // Everything below the list has a fixed height, so budget that first and give the list the
    // remainder. A hard 8-row list pushed the custom-profile form off the bottom of the panel.
    const int row_h = dip(parent_, kProfileRowDip);
    const int below_list_h = m.pad_tight + m.btn_h * 2 + m.pad_tight + m.pad_section  // two action rows
                             + (m.row_h + m.pad_row) * 2 + m.pad_section              // default + policy
                             + label_h + m.pad_tight                                  // form heading
                             + (m.row_h + m.pad_row) * 5  // name / exe / args / cwd mode / cwd path
                             + m.btn_h;                   // Add custom / Save profile
    const int list_avail = (col.bottom - m.pad_outer) - y - below_list_h;
    const int list_h = (std::max)(row_h, (std::min)(row_h * kVisibleProfileRows, list_avail));
    MoveWindow(profiles_, x0, y, w, list_h, TRUE);
    update_profile_list_scroll();
    y += list_h + m.pad_tight;

    const int wide_btn = m.btn_w + dip(parent_, 40);
    int bx = x0;
    auto place_btn = [&](HWND h, int bw) {
        MoveWindow(h, bx, y, bw, m.btn_h, TRUE);
        bx += bw + m.pad_tight;
    };
    // Five actions do not fit one row at the reading-width cap, so they wrap: list operations
    // first, destructive / rediscovery second.
    place_btn(btn_toggle_, wide_btn);
    place_btn(btn_edit_, wide_btn);
    place_btn(btn_dup_, m.btn_w);
    y += m.btn_h + m.pad_tight;
    bx = x0;
    place_btn(btn_remove_, m.btn_w + dip(parent_, 30));
    place_btn(btn_refresh_, wide_btn);
    y += m.btn_h + m.pad_section;

    ui_space::place_labeled_row(default_lbl_, default_select_, col, y, m);
    y += m.row_h + m.pad_row;
    ui_space::place_labeled_row(policy_lbl_, policy_select_, col, y, m);
    y += m.row_h + m.pad_section;

    MoveWindow(custom_heading_, x0, y, w, label_h, TRUE);
    y += label_h + m.pad_tight;

    ui_space::place_labeled_row(name_lbl_, custom_name_, col, y, m);
    ui_kit::center_field_text(custom_name_);
    y += m.row_h + m.pad_row;

    const int browse_w = m.btn_w;
    ui_space::place_labeled_row(exe_lbl_, custom_exe_, col, y, m, -(browse_w + m.pad_tight));
    ui_kit::center_field_text(custom_exe_);
    MoveWindow(btn_browse_, x1 - browse_w, y + (m.row_h - m.btn_h) / 2, browse_w, m.btn_h, TRUE);
    y += m.row_h + m.pad_row;

    ui_space::place_labeled_row(args_lbl_, custom_args_, col, y, m);
    ui_kit::center_field_text(custom_args_);
    y += m.row_h + m.pad_row;

    ui_space::place_labeled_row(cwd_mode_lbl_, cwd_mode_, col, y, m);
    y += m.row_h + m.pad_row;

    ui_space::place_labeled_row(cwd_path_lbl_, custom_cwd_, col, y, m, -(browse_w + m.pad_tight));
    ui_kit::center_field_text(custom_cwd_);
    MoveWindow(btn_cwd_browse_, x1 - browse_w, y + (m.row_h - m.btn_h) / 2, browse_w, m.btn_h, TRUE);
    y += m.row_h + m.pad_row;

    const int save_w = m.btn_w + dip(parent_, 20);
    MoveWindow(btn_add_, x0 + m.label_w + m.pad_tight, y, save_w, m.btn_h, TRUE);
    MoveWindow(btn_cancel_edit_, x0 + m.label_w + m.pad_tight + save_w + m.pad_tight, y, m.btn_w, m.btn_h, TRUE);
    if (btn_cancel_edit_ && editing_id_.empty()) {
        ShowWindow(btn_cancel_edit_, SW_HIDE);
    }
}

void TerminalSettingsUi::fill_profiles(const std::vector<TerminalProfile>& profiles) {
    if (!profiles_) {
        return;
    }
    cached_profiles_ = profiles;
    // Preserve the caret across refills: Toggle selected operates on LB_GETCURSEL, so losing
    // the selection here made the button a no-op on the following click.
    const int prev = static_cast<int>(SendMessageW(profiles_, LB_GETCURSEL, 0, 0));
    SendMessageW(profiles_, LB_RESETCONTENT, 0, 0);
    for (const auto& p : profiles) {
        std::wstring line = p.name;
        if (p.source == TerminalProfileSource::Custom) {
            line += L" (custom)";
        }
        SendMessageW(profiles_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    const int count = static_cast<int>(profiles.size());
    if (count > 0) {
        const int want = (prev >= 0 && prev < count) ? prev : 0;
        SendMessageW(profiles_, LB_SETCURSEL, want, 0);
    } else {
        // A listbox with zero items paints as a blank slab with no explanation. One unselectable
        // placeholder row (index beyond cached_profiles_, so selected_index() still reports none)
        // tells the user detection found nothing and what to do about it.
        SendMessageW(profiles_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No terminals detected — use Refresh detection, or add a custom profile below."));
    }
    update_profile_list_scroll();
    update_action_enabled();
    InvalidateRect(profiles_, nullptr, TRUE);
}

void TerminalSettingsUi::fill_default_select(const std::vector<TerminalProfile>& profiles,
                                            const std::string& default_id) {
    if (!default_select_) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    int sel = -1;
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        if (!profiles[i].enabled) {
            continue;
        }
        ui_kit::SelectItem it;
        it.label = profiles[i].name;
        it.data = static_cast<LPARAM>(i);
        if (profiles[i].id == default_id) {
            sel = static_cast<int>(items.size());
        }
        items.push_back(std::move(it));
    }
    ui_kit::select_set_items(default_select_, items);
    if (sel >= 0) {
        ui_kit::select_set_index(default_select_, sel);
    } else if (!items.empty()) {
        ui_kit::select_set_index(default_select_, 0);
    }
}

void TerminalSettingsUi::fill_policy_select(const std::string& policy) {
    if (!policy_select_) {
        return;
    }
    std::vector<ui_kit::SelectItem> items = {
        {L"Ask before command", 0},
        {L"Allow", 1},
        {L"Block", 2},
    };
    ui_kit::select_set_items(policy_select_, items);
    int sel = 0;
    if (policy == "allow") {
        sel = 1;
    } else if (policy == "block") {
        sel = 2;
    }
    ui_kit::select_set_index(policy_select_, sel);
}

void TerminalSettingsUi::reload(std::vector<TerminalProfile>& profiles, Settings& settings) {
    fill_profiles(profiles);
    fill_default_select(profiles, settings.default_terminal_profile_id);
    fill_policy_select(normalize_agent_terminal_policy(settings.agent_terminal_policy));
}

bool TerminalSettingsUi::persist_prefs(Settings& settings, const std::wstring& settings_path,
                                      const std::vector<TerminalProfile>& profiles) {
    settings.terminal_profile_enabled.clear();
    for (const auto& p : profiles) {
        settings.terminal_profile_enabled[p.id] = p.enabled;
    }
    if (default_select_) {
        const int pi = static_cast<int>(ui_kit::select_get_data(default_select_));
        if (pi >= 0 && pi < static_cast<int>(profiles.size())) {
            settings.default_terminal_profile_id = profiles[static_cast<std::size_t>(pi)].id;
        }
    }
    if (policy_select_) {
        const int sel = ui_kit::select_get_index(policy_select_);
        if (sel == 1) {
            settings.agent_terminal_policy = "allow";
        } else if (sel == 2) {
            settings.agent_terminal_policy = "block";
        } else {
            settings.agent_terminal_policy = "ask";
        }
    }
    // Report a failed write: the toggle used to flip in the UI and silently revert on restart.
    if (!save_settings(settings_path, settings)) {
        ui_kit::report_save_failure(parent_, L"terminal settings", settings_path);
        return false;
    }
    return true;
}

void TerminalSettingsUi::toggle_profile_at(int index, std::vector<TerminalProfile>& profiles, Settings& settings,
                                          const std::wstring& settings_path, const std::wstring& terminals_path) {
    if (index < 0 || index >= static_cast<int>(profiles.size())) {
        return;
    }
    profiles[static_cast<std::size_t>(index)].enabled = !profiles[static_cast<std::size_t>(index)].enabled;
    if (!profiles[static_cast<std::size_t>(index)].enabled &&
        settings.default_terminal_profile_id == profiles[static_cast<std::size_t>(index)].id) {
        settings.default_terminal_profile_id.clear();
    }
    persist_prefs(settings, settings_path, profiles);
    if (profiles[static_cast<std::size_t>(index)].source == TerminalProfileSource::Custom) {
        std::vector<TerminalProfile> custom;
        for (const auto& p : profiles) {
            if (p.source == TerminalProfileSource::Custom) {
                custom.push_back(p);
            }
        }
        save_custom_terminal_profiles(terminals_path, custom);
    }
    reload(profiles, settings);
}

bool TerminalSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || mi->CtlType != ODT_LISTBOX || mi->CtlID != IdProfiles) {
        return false;
    }
    mi->itemHeight = dip(parent_ ? parent_ : GetDesktopWindow(), kProfileRowDip);
    return true;
}

bool TerminalSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di || di->CtlType != ODT_LISTBOX || di->hwndItem != profiles_) {
        return false;
    }
    const Theme& t = theme();
    if (di->itemID == static_cast<UINT>(-1)) {
        fill_rect(di->hDC, di->rcItem, t.panel);
        return true;
    }
    // No selection strip — checkbox state is the affordance.
    fill_rect(di->hDC, di->rcItem, t.panel);
    if (cached_profiles_.empty()) {
        // Placeholder row from fill_profiles(): muted copy, no checkbox, no selection strip.
        wchar_t buf[256]{};
        SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, reinterpret_cast<LPARAM>(buf));
        RECT r = di->rcItem;
        r.left += dip(parent_, 10);
        r.right -= dip(parent_, 10);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, t.muted);
        if (font) {
            SelectObject(di->hDC, font);
        }
        DrawTextW(di->hDC, buf, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return true;
    }
    bool checked = false;
    std::wstring primary;
    std::wstring secondary;
    if (di->itemID < cached_profiles_.size()) {
        const auto& p = cached_profiles_[di->itemID];
        checked = p.enabled;
        primary = p.name;
        secondary = p.source == TerminalProfileSource::Custom ? L"Custom" : L"Detected";
        if (p.working_directory_mode == WorkingDirectoryMode::Home) {
            secondary += L"  ·  Home";
        } else if (p.working_directory_mode == WorkingDirectoryMode::Custom) {
            secondary += L"  ·  ";
            secondary += p.custom_working_directory.empty() ? L"Custom path" : p.custom_working_directory;
        }
        if (!p.executable.empty()) {
            secondary += L"  ·  ";
            secondary += p.executable;
        }
    } else {
        wchar_t buf[256]{};
        SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, reinterpret_cast<LPARAM>(buf));
        primary = buf;
    }

    const int bottom_pad = dip(parent_, kProfileBottomPadDip);
    const int top = di->rcItem.top + dip(parent_, 6);
    const int s = dip(parent_, 16);
    const int gx = di->rcItem.left + dip(parent_, 10);
    ui_kit::draw_checkbox_glyph(di->hDC, gx, top, s, checked, false, false);

    RECT content = di->rcItem;
    content.left = gx + s + dip(parent_, 10);
    content.right -= dip(parent_, 8);
    content.top = top;
    content.bottom = di->rcItem.bottom - bottom_pad;

    RECT pr = content;
    pr.bottom = content.top + dip(parent_, 18);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, t.text);
    if (font) {
        SelectObject(di->hDC, font);
    }
    DrawTextW(di->hDC, primary.c_str(), -1, &pr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!secondary.empty()) {
        RECT sr = content;
        sr.top = pr.bottom + dip(parent_, 4);
        SetTextColor(di->hDC, t.muted);
        DrawTextW(di->hDC, secondary.c_str(), -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    return true;
}

bool TerminalSettingsUi::on_command(WORD id, WORD notify, HWND owner, std::vector<TerminalProfile>& profiles,
                                    Settings& settings, const std::wstring& settings_path,
                                    const std::wstring& terminals_path) {
    if (id == IdDefault && ui_kit::select_handle_command(default_select_, notify)) {
        return true;
    }
    if (id == IdPolicy && ui_kit::select_handle_command(policy_select_, notify)) {
        return true;
    }
    if (id == IdCwdMode) {
        if (ui_kit::select_handle_command(cwd_mode_, notify)) {
            return true;
        }
        if (notify == CBN_SELCHANGE) {
            update_action_enabled();
            return true;
        }
        return false;
    }

    switch (id) {
        case IdToggleEnable: {
            const int sel = static_cast<int>(SendMessageW(profiles_, LB_GETCURSEL, 0, 0));
            toggle_profile_at(sel, profiles, settings, settings_path, terminals_path);
            return true;
        }
        case IdRemoveCustom: {
            const int sel = selected_index();
            if (sel < 0 || profiles[static_cast<std::size_t>(sel)].source != TerminalProfileSource::Custom) {
                return true;
            }
            const TerminalProfile victim = profiles[static_cast<std::size_t>(sel)];
            if (!ui_kit::confirm_destructive(owner, L"Remove", victim.name.c_str(),
                                             L"The custom terminal profile is deleted from Scylla Workbench. "
                                             L"Open sessions using it keep running.")) {
                return true;
            }
            profiles.erase(profiles.begin() + sel);
            settings.terminal_profile_enabled.erase(victim.id);
            if (settings.default_terminal_profile_id == victim.id) {
                settings.default_terminal_profile_id.clear();
            }
            if (editing_id_ == victim.id) {
                end_edit();
            }
            commit_profiles(owner, profiles, settings, settings_path, terminals_path);
            reload(profiles, settings);
            return true;
        }
        case IdEditCustom: {
            const int sel = selected_index();
            if (sel < 0 || profiles[static_cast<std::size_t>(sel)].source != TerminalProfileSource::Custom) {
                return true;
            }
            begin_edit(profiles[static_cast<std::size_t>(sel)]);
            return true;
        }
        case IdCancelEdit:
            end_edit();
            return true;
        case IdDupCustom: {
            const int sel = selected_index();
            if (sel < 0) {
                return true;
            }
            // Duplicating a detected profile is the sanctioned way to get an editable copy of a
            // discovered shell without mutating the discovered row.
            TerminalProfile copy = profiles[static_cast<std::size_t>(sel)];
            copy.source = TerminalProfileSource::Custom;
            copy.enabled = true;
            std::wstring base = copy.name + L" copy";
            std::wstring name = base;
            for (int n = 2; n < 100; ++n) {
                const std::string candidate = make_terminal_profile_id("custom", name);
                bool taken = false;
                for (const auto& x : profiles) {
                    if (x.id == candidate) {
                        taken = true;
                        break;
                    }
                }
                if (!taken) {
                    break;
                }
                name = base + L" " + std::to_wstring(n);
            }
            copy.name = name;
            copy.id = make_terminal_profile_id("custom", name);
            profiles.push_back(copy);
            settings.terminal_profile_enabled[copy.id] = true;
            commit_profiles(owner, profiles, settings, settings_path, terminals_path);
            reload(profiles, settings);
            begin_edit(copy);
            return true;
        }
        case IdCwdBrowse: {
            std::wstring folder;
            if (pick_start_directory(owner, folder)) {
                SetWindowTextW(custom_cwd_, folder.c_str());
            }
            return true;
        }
        case IdProfiles:
            if (notify == LBN_DBLCLK) {
                const int sel = static_cast<int>(SendMessageW(profiles_, LB_GETCURSEL, 0, 0));
                toggle_profile_at(sel, profiles, settings, settings_path, terminals_path);
                return true;
            }
            if (notify == LBN_SELCHANGE) {
                update_action_enabled();
                return true;
            }
            return false;
        case IdDefault:
        case IdPolicy:
            if (notify == CBN_SELCHANGE || notify == BN_CLICKED) {
                persist_prefs(settings, settings_path, profiles);
                return true;
            }
            return false;
        case IdRefresh: {
            auto discovered = discover_terminal_profiles();
            auto custom = load_custom_terminal_profiles(terminals_path);
            for (const auto& p : profiles) {
                settings.terminal_profile_enabled[p.id] = p.enabled;
            }
            profiles = merge_terminal_profiles(discovered, custom, settings.terminal_profile_enabled);
            persist_prefs(settings, settings_path, profiles);
            reload(profiles, settings);
            return true;
        }
        case IdCustomBrowse: {
            wchar_t file[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = owner;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0All Files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(custom_exe_, file);
            }
            return true;
        }
        case IdAddCustom: {
            wchar_t name[256]{};
            wchar_t exe[MAX_PATH]{};
            wchar_t args[512]{};
            wchar_t cwd[MAX_PATH]{};
            GetWindowTextW(custom_name_, name, 256);
            GetWindowTextW(custom_exe_, exe, MAX_PATH);
            GetWindowTextW(custom_args_, args, 512);
            GetWindowTextW(custom_cwd_, cwd, MAX_PATH);
            const bool editing = !editing_id_.empty();
            if (name[0] == 0 || exe[0] == 0) {
                MessageBoxW(owner, L"Name and executable are required.",
                            editing ? L"Edit terminal profile" : L"Add custom terminal", MB_OK | MB_ICONWARNING);
                return true;
            }
            const WorkingDirectoryMode mode = selected_cwd_mode();
            if (mode == WorkingDirectoryMode::Custom && cwd[0] == 0) {
                MessageBoxW(owner, L"Choose a start directory, or switch the profile back to Active project.",
                            editing ? L"Edit terminal profile" : L"Add custom terminal", MB_OK | MB_ICONWARNING);
                return true;
            }

            TerminalProfile p;
            // Editing keeps the original id so enablement, the default-profile pointer, and any
            // running session's profile_id stay valid after a rename.
            p.id = editing ? editing_id_ : make_terminal_profile_id("custom", name);
            p.name = name;
            p.executable = exe;
            p.args = args;
            p.source = TerminalProfileSource::Custom;
            p.enabled = true;
            p.working_directory_mode = mode;
            p.custom_working_directory = mode == WorkingDirectoryMode::Custom ? cwd : L"";

            bool replaced = false;
            for (auto& existing : profiles) {
                if (existing.id == p.id) {
                    p.enabled = existing.enabled;
                    existing = p;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                profiles.push_back(p);
            }
            settings.terminal_profile_enabled[p.id] = p.enabled;
            commit_profiles(owner, profiles, settings, settings_path, terminals_path);
            end_edit();
            reload(profiles, settings);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace scyllagpt
