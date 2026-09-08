#include "scyllagpt/environment_settings_ui.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <algorithm>
#include <vector>

namespace scyllagpt {
namespace {

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd ? hwnd : GetDesktopWindow())), 96);
}

std::wstring get_text(HWND h) {
    if (!h) {
        return {};
    }
    const int n = GetWindowTextLengthW(h);
    if (n <= 0) {
        return {};
    }
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(wcslen(s.c_str()));
    return s;
}

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

std::wstring avail_summary(const EnvVarAvailability& a) {
    std::wstring s;
    if (a.human_terminal) {
        s += L"Human";
    }
    if (a.agent_terminal) {
        if (!s.empty()) {
            s += L" · ";
        }
        s += L"Agent";
    }
    if (a.approved_recipes) {
        if (!s.empty()) {
            s += L" · ";
        }
        s += L"Recipes";
    }
    if (s.empty()) {
        s = L"(none)";
    }
    return s;
}

}  // namespace

bool EnvironmentSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = inst ? inst : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    font_small_ = font_small ? font_small : font;

    title_ = ui_kit::create_static(parent_, inst_, Cmd_EnvTitle, L"Project Environments", font_, false);
    desc_ = ui_kit::create_static(
        parent_, inst_, Cmd_EnvDesc,
        L"Configure runtime variables and protected secret bindings used by terminals and approved "
        L"project operations.",
        font_small_, true);
    project_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_EnvProject, L"", font_small_, true);
    active_lbl_ =
        ui_kit::create_static(parent_, inst_, Cmd_EnvActiveLbl, L"Active for new terminals", font_small_, false);
    active_ = ui_kit::create_select(parent_, inst_, Cmd_EnvActive, font_);
    list_ = ui_kit::create_entity_list(parent_, inst_, Cmd_EnvList, font_);
    btn_add_ = ui_kit::create_button(parent_, inst_, Cmd_EnvAdd, L"+ Add Environment",
                                     ui_kit::ButtonKind::Primary, font_);
    btn_manage_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvManage, L"Manage", ui_kit::ButtonKind::Secondary, font_);
    btn_set_active_ = ui_kit::create_button(parent_, inst_, Cmd_EnvSetActive, L"Set Active",
                                            ui_kit::ButtonKind::Secondary, font_);
    btn_dup_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvDup, L"Duplicate", ui_kit::ButtonKind::Secondary, font_);
    btn_delete_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvDelete, L"Delete", ui_kit::ButtonKind::Danger, font_);

    btn_back_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvBack, L"‹ Project Environments", ui_kit::ButtonKind::Ghost,
                              font_);
    detail_title_ = ui_kit::create_static(parent_, inst_, 0, L"", font_, false);
    detail_name_ = ui_kit::create_text_field(parent_, inst_, Cmd_EnvDetailName, font_);
    inherit_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_EnvInheritLbl, L"Inherits", font_small_, false);
    inherit_ = ui_kit::create_select(parent_, inst_, Cmd_EnvInherit, font_);
    vars_ = ui_kit::create_entity_list(parent_, inst_, Cmd_EnvVars, font_);
    btn_add_var_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvAddVar, L"+ Add Variable", ui_kit::ButtonKind::Primary, font_);
    btn_edit_var_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvEditVar, L"Edit", ui_kit::ButtonKind::Secondary, font_);
    btn_remove_var_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvRemoveVar, L"Remove", ui_kit::ButtonKind::Ghost, font_);
    btn_detail_dup_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvDetailDup, L"Duplicate", ui_kit::ButtonKind::Secondary, font_);
    btn_detail_delete_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvDetailDelete, L"Delete", ui_kit::ButtonKind::Danger, font_);

    add_env_name_ = ui_kit::create_text_field(parent_, inst_, Cmd_EnvAddEnvName, font_);
    add_env_inherit_ = ui_kit::create_select(parent_, inst_, Cmd_EnvAddEnvInherit, font_);
    btn_add_env_save_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvAddEnvSave, L"Add Environment", ui_kit::ButtonKind::Primary,
                              font_);
    btn_add_env_cancel_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvAddEnvCancel, L"Cancel", ui_kit::ButtonKind::Secondary, font_);
    ui_kit::set_placeholder(add_env_name_, L"Environment name");

    var_name_ = ui_kit::create_text_field(parent_, inst_, Cmd_EnvVarFormName, font_);
    var_plain_ = ui_kit::create_radio(parent_, inst_, Cmd_EnvVarPlain, L"Plain Value", font_, true);
    var_protected_ =
        ui_kit::create_radio(parent_, inst_, Cmd_EnvVarProtected, L"Protected Secret", font_, false);
    var_value_ = ui_kit::create_text_field(parent_, inst_, Cmd_EnvVarValue, font_);
    var_secret_ = ui_kit::create_select(parent_, inst_, Cmd_EnvVarSecret, font_);
    btn_var_unlock_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvVarUnlock, L"Unlock Keyring", ui_kit::ButtonKind::Secondary,
                              font_);
    var_avail_human_ =
        ui_kit::create_checkbox(parent_, inst_, Cmd_EnvVarAvailHuman, L"Human terminals", font_);
    var_avail_agent_ =
        ui_kit::create_checkbox(parent_, inst_, Cmd_EnvVarAvailAgent, L"Agent terminals", font_);
    var_avail_recipes_ =
        ui_kit::create_checkbox(parent_, inst_, Cmd_EnvVarAvailRecipes, L"Approved recipes", font_);
    btn_var_save_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvVarSave, L"Save Variable", ui_kit::ButtonKind::Primary, font_);
    btn_var_cancel_ =
        ui_kit::create_button(parent_, inst_, Cmd_EnvVarCancel, L"Cancel", ui_kit::ButtonKind::Secondary, font_);
    ui_kit::set_placeholder(var_name_, L"VARIABLE_NAME");
    ui_kit::set_placeholder(var_value_, L"Plain value");
    ui_kit::set_placeholder(detail_name_, L"Environment name");

    ui_kit::style_scroll_host(list_, theme().panel);
    ui_kit::style_scroll_host(vars_, theme().panel);
    mode_ = Mode::Landing;
    hide_all();
    return true;
}

void EnvironmentSettingsUi::destroy() {
    hide_all();
    for (HWND h : all_controls()) {
        if (h) {
            DestroyWindow(h);
        }
    }
    parent_ = nullptr;
    visible_ = false;
    landing_ids_.clear();
    var_names_.clear();
}

std::vector<HWND> EnvironmentSettingsUi::all_controls() const {
    return {title_,
            desc_,
            project_lbl_,
            active_lbl_,
            active_,
            list_,
            btn_add_,
            btn_manage_,
            btn_set_active_,
            btn_dup_,
            btn_delete_,
            btn_back_,
            detail_title_,
            detail_name_,
            inherit_lbl_,
            inherit_,
            vars_,
            btn_add_var_,
            btn_edit_var_,
            btn_remove_var_,
            btn_detail_dup_,
            btn_detail_delete_,
            add_env_name_,
            add_env_inherit_,
            btn_add_env_save_,
            btn_add_env_cancel_,
            var_name_,
            var_plain_,
            var_protected_,
            var_value_,
            var_secret_,
            btn_var_unlock_,
            var_avail_human_,
            var_avail_agent_,
            var_avail_recipes_,
            btn_var_save_,
            btn_var_cancel_};
}

void EnvironmentSettingsUi::hide_all() {
    show_many(all_controls(), SW_HIDE);
}

void EnvironmentSettingsUi::set_mode(Mode mode) {
    mode_ = mode;
    apply_visibility();
    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
}

void EnvironmentSettingsUi::apply_visibility() {
    if (!visible_) {
        hide_all();
        return;
    }
    hide_all();
    const auto show = [](const std::vector<HWND>& hs) { show_many(hs, SW_SHOW); };
    switch (mode_) {
        case Mode::Landing:
            show({title_, desc_, project_lbl_, active_lbl_, active_, list_, btn_add_, btn_manage_,
                  btn_set_active_, btn_dup_, btn_delete_});
            break;
        case Mode::AddEnv:
            show({title_, desc_, project_lbl_, add_env_name_, add_env_inherit_, btn_add_env_save_,
                  btn_add_env_cancel_});
            break;
        case Mode::Detail:
            show({btn_back_, detail_title_, detail_name_, inherit_lbl_, inherit_, vars_, btn_add_var_,
                  btn_edit_var_, btn_remove_var_, btn_detail_dup_, btn_detail_delete_});
            break;
        case Mode::AddVar:
            show({btn_back_, detail_title_, var_name_, var_plain_, var_protected_, var_value_, var_secret_,
                  btn_var_unlock_, var_avail_human_, var_avail_agent_, var_avail_recipes_, btn_var_save_,
                  btn_var_cancel_});
            break;
    }
}

void EnvironmentSettingsUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        mode_ = Mode::Landing;
        return;
    }
    apply_visibility();
}

void EnvironmentSettingsUi::layout(const RECT& area) {
    area_ = area;
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    const int x = col.left + m.pad_outer;
    const int right = col.right - m.pad_outer;
    const int w = (std::max)(1, right - x);
    const int top = col.top + m.pad_outer;
    const int bottom = col.bottom - m.pad_outer;
    const int row = m.row_h;
    const int gap = m.pad_tight;
    const int desc_h = ui_space::dip(parent_, ui_space::kPageHeaderDescHDip);
    const int btn_w = m.btn_w + dip(parent_, 24);

    if (mode_ == Mode::Landing) {
        int y = top;
        MoveWindow(title_, x, y, w, row, TRUE);
        y += row;
        MoveWindow(desc_, x, y, w, desc_h, TRUE);
        y += desc_h + gap;
        MoveWindow(project_lbl_, x, y, w, row, TRUE);
        y += row + gap;
        const int alw = (std::min)(m.label_w + dip(parent_, 80), w / 2);
        MoveWindow(active_lbl_, x, y, alw, row, TRUE);
        MoveWindow(active_, x + alw + gap, y, (std::max)(1, w - alw - gap), row, TRUE);
        y += row + gap;
        const int actions_h = row;
        const int list_bottom = bottom - actions_h - gap - row - gap;
        MoveWindow(list_, x, y, w, (std::max)(row * 3, list_bottom - y), TRUE);
        const int ay = bottom - row;
        MoveWindow(btn_add_, x, ay, btn_w + dip(parent_, 40), row, TRUE);
        MoveWindow(btn_manage_, x + btn_w + dip(parent_, 48) + gap, ay, btn_w, row, TRUE);
        MoveWindow(btn_set_active_, x + (btn_w + gap) * 2 + dip(parent_, 48), ay, btn_w, row, TRUE);
        MoveWindow(btn_dup_, x + (btn_w + gap) * 3 + dip(parent_, 48), ay, btn_w, row, TRUE);
        MoveWindow(btn_delete_, x + (btn_w + gap) * 4 + dip(parent_, 48), ay, btn_w, row, TRUE);
        return;
    }

    if (mode_ == Mode::AddEnv) {
        int y = top;
        MoveWindow(title_, x, y, w, row, TRUE);
        y += row;
        MoveWindow(desc_, x, y, w, desc_h, TRUE);
        y += desc_h + gap;
        MoveWindow(project_lbl_, x, y, w, row, TRUE);
        y += row + m.pad_section;
        MoveWindow(add_env_name_, x, y, w, row, TRUE);
        ui_kit::center_field_text(add_env_name_);
        y += row + gap;
        MoveWindow(add_env_inherit_, x, y, w, row, TRUE);
        y += row + m.pad_section;
        MoveWindow(btn_add_env_save_, x, y, btn_w + dip(parent_, 40), row, TRUE);
        MoveWindow(btn_add_env_cancel_, x + btn_w + dip(parent_, 48) + gap, y, btn_w, row, TRUE);
        return;
    }

    if (mode_ == Mode::Detail) {
        int y = top;
        MoveWindow(btn_back_, x, y, btn_w + dip(parent_, 80), row, TRUE);
        y += row + gap;
        MoveWindow(detail_title_, x, y, w, row, TRUE);
        y += row + gap;
        MoveWindow(detail_name_, x, y, w, row, TRUE);
        ui_kit::center_field_text(detail_name_);
        y += row + gap;
        const int ilw = (std::min)(m.label_w, w / 3);
        MoveWindow(inherit_lbl_, x, y, ilw, row, TRUE);
        MoveWindow(inherit_, x + ilw + gap, y, (std::max)(1, w - ilw - gap), row, TRUE);
        y += row + gap;
        const int bottom_actions = bottom - row;
        const int mid_actions = bottom_actions - gap - row;
        MoveWindow(vars_, x, y, w, (std::max)(row * 3, mid_actions - gap - y), TRUE);
        MoveWindow(btn_add_var_, x, mid_actions, btn_w + dip(parent_, 40), row, TRUE);
        MoveWindow(btn_edit_var_, x + btn_w + dip(parent_, 48) + gap, mid_actions, btn_w, row, TRUE);
        MoveWindow(btn_remove_var_, x + (btn_w + gap) * 2 + dip(parent_, 48), mid_actions, btn_w, row, TRUE);
        MoveWindow(btn_detail_dup_, x, bottom_actions, btn_w, row, TRUE);
        MoveWindow(btn_detail_delete_, x + btn_w + gap, bottom_actions, btn_w, row, TRUE);
        return;
    }

    // AddVar
    int y = top;
    MoveWindow(btn_back_, x, y, btn_w + dip(parent_, 80), row, TRUE);
    y += row + gap;
    MoveWindow(detail_title_, x, y, w, row, TRUE);
    y += row + m.pad_section;
    MoveWindow(var_name_, x, y, w, row, TRUE);
    ui_kit::center_field_text(var_name_);
    y += row + gap;
    MoveWindow(var_plain_, x, y, w / 2 - gap, row, TRUE);
    MoveWindow(var_protected_, x + w / 2, y, w / 2, row, TRUE);
    y += row + gap;
    MoveWindow(var_value_, x, y, w, row, TRUE);
    ui_kit::center_field_text(var_value_);
    MoveWindow(var_secret_, x, y, w, row, TRUE);
    y += row + gap;
    MoveWindow(btn_var_unlock_, x, y, btn_w + dip(parent_, 20), row, TRUE);
    y += row + m.pad_section;
    MoveWindow(var_avail_human_, x, y, w, row, TRUE);
    y += row + gap / 2;
    MoveWindow(var_avail_agent_, x, y, w, row, TRUE);
    y += row + gap / 2;
    MoveWindow(var_avail_recipes_, x, y, w, row, TRUE);
    y += row + m.pad_section;
    MoveWindow(btn_var_save_, x, y, btn_w + dip(parent_, 20), row, TRUE);
    MoveWindow(btn_var_cancel_, x + btn_w + dip(parent_, 28) + gap, y, btn_w, row, TRUE);
}

bool EnvironmentSettingsUi::persist(ProjectEnvironmentManager& mgr, const std::wstring& path, HWND owner) {
    if (path.empty()) {
        return true;
    }
    if (mgr.save(path)) {
        return true;
    }
    ui_kit::report_save_failure(owner ? owner : parent_, L"project environments", path);
    return false;
}

int EnvironmentSettingsUi::count_protected(const ProjectEnvironment& e) const {
    int n = 0;
    for (const auto& v : e.variables) {
        if (v.kind == EnvVarKind::SecretRef) {
            ++n;
        }
    }
    return n;
}

bool EnvironmentSettingsUi::secret_missing(Keyring& keyring, const std::string& secret_id) const {
    if (!keyring.is_unlocked() || secret_id.empty()) {
        return false;
    }
    for (const auto& r : keyring.list_refs()) {
        if (r.name == secret_id || r.id == secret_id) {
            return false;
        }
    }
    return true;
}

void EnvironmentSettingsUi::fill_active_select(ProjectEnvironmentManager& mgr) {
    if (!active_) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    items.push_back({L"None", 0});
    int sel = 0;
    const auto* st = mgr.state_for(project_id_);
    if (st) {
        int i = 1;
        for (const auto& e : st->environments) {
            items.push_back({utf16(e.name), static_cast<LPARAM>(i)});
            if (e.id == st->active_environment_id) {
                sel = i;
            }
            ++i;
        }
    }
    ui_kit::select_set_items(active_, items);
    ui_kit::select_set_index(active_, sel);
}

void EnvironmentSettingsUi::fill_inherit_select(ProjectEnvironmentManager& mgr, HWND select,
                                                const std::string& exclude_id) {
    if (!select) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    items.push_back({L"None", 0});
    int sel = 0;
    const auto* st = mgr.state_for(project_id_);
    const ProjectEnvironment* cur = exclude_id.empty() ? nullptr : mgr.find_environment(project_id_, exclude_id);
    if (st) {
        int idx = 1;
        for (const auto& e : st->environments) {
            if (e.id == exclude_id) {
                continue;
            }
            items.push_back({utf16(e.name), static_cast<LPARAM>(idx)});
            if (cur && cur->inherits_from_id == e.id) {
                sel = static_cast<int>(items.size()) - 1;
            }
            ++idx;
        }
    }
    ui_kit::select_set_items(select, items);
    ui_kit::select_set_index(select, sel);
}

void EnvironmentSettingsUi::fill_secret_select(Keyring& keyring) {
    secret_names_.clear();
    if (!var_secret_) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    if (!Keyring::app_vault_exists() && !keyring.vault_bound()) {
        items.push_back({L"(create Keyring first)", -1});
        ui_kit::select_set_items(var_secret_, items);
        ui_kit::select_set_index(var_secret_, 0);
        return;
    }
    if (!keyring.is_unlocked()) {
        items.push_back({L"(Keyring locked)", -1});
        ui_kit::select_set_items(var_secret_, items);
        ui_kit::select_set_index(var_secret_, 0);
        return;
    }
    const auto refs = keyring.list_refs_for_ui(project_id_);
    if (refs.empty()) {
        items.push_back({L"(no secrets yet)", -1});
        ui_kit::select_set_items(var_secret_, items);
        ui_kit::select_set_index(var_secret_, 0);
        return;
    }
    int i = 0;
    for (const auto& r : refs) {
        secret_names_.push_back(r.name);
        std::wstring label = utf16(r.name);
        if (r.scope == SecretScope::Global) {
            label += L"  · Global";
        } else {
            label += L"  · Project";
        }
        items.push_back({label, static_cast<LPARAM>(i++)});
    }
    ui_kit::select_set_items(var_secret_, items);
    ui_kit::select_set_index(var_secret_, 0);
}

void EnvironmentSettingsUi::update_var_form_fields(Keyring& keyring) {
    const bool protected_sel = var_protected_ && SendMessageW(var_protected_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (var_value_) {
        ShowWindow(var_value_, protected_sel ? SW_HIDE : SW_SHOW);
    }
    if (var_secret_) {
        ShowWindow(var_secret_, protected_sel ? SW_SHOW : SW_HIDE);
    }
    const bool need_unlock = protected_sel && Keyring::app_vault_exists() && !keyring.is_unlocked();
    const bool need_create = protected_sel && !Keyring::app_vault_exists() && !keyring.vault_bound();
    if (btn_var_unlock_) {
        if (need_create) {
            SetWindowTextW(btn_var_unlock_, L"Create Keyring…");
            ShowWindow(btn_var_unlock_, SW_SHOW);
        } else if (need_unlock) {
            SetWindowTextW(btn_var_unlock_, L"Unlock Keyring");
            ShowWindow(btn_var_unlock_, SW_SHOW);
        } else {
            ShowWindow(btn_var_unlock_, SW_HIDE);
        }
    }
    if (var_avail_agent_) {
        if (protected_sel) {
            SendMessageW(var_avail_agent_, BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(var_avail_agent_, FALSE);
        } else {
            EnableWindow(var_avail_agent_, TRUE);
        }
    }
    fill_secret_select(keyring);
}

void EnvironmentSettingsUi::fill_landing(ProjectEnvironmentManager& mgr, Keyring& keyring) {
    landing_ids_.clear();
    landing_primary_.clear();
    landing_secondary_.clear();
    if (!list_) {
        return;
    }
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    const auto* st = mgr.state_for(project_id_);
    if (!st || st->environments.empty()) {
        landing_primary_.push_back(L"No project environments configured");
        landing_secondary_.push_back(L"Create an environment to define terminal/runtime variables.");
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(landing_primary_.back().c_str()));
        EnableWindow(btn_manage_, FALSE);
        EnableWindow(btn_set_active_, FALSE);
        EnableWindow(btn_dup_, FALSE);
        EnableWindow(btn_delete_, FALSE);
        return;
    }
    int restore = 0;
    for (const auto& e : st->environments) {
        landing_ids_.push_back(e.id);
        std::wstring primary = utf16(e.name);
        if (e.id == st->active_environment_id) {
            primary += L"  · Active";
        }
        landing_primary_.push_back(primary);
        std::wstring sec = std::to_wstring(e.variables.size());
        sec += L" variables · ";
        sec += std::to_wstring(count_protected(e));
        sec += L" protected";
        int missing = 0;
        for (const auto& v : e.variables) {
            if (v.kind == EnvVarKind::SecretRef && secret_missing(keyring, v.secret_id)) {
                ++missing;
            }
        }
        if (missing) {
            sec += L"  ·  ! ";
            sec += std::to_wstring(missing);
            sec += L" missing reference";
        }
        landing_secondary_.push_back(sec);
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(landing_primary_.back().c_str()));
    }
    SendMessageW(list_, LB_SETCURSEL, restore, 0);
    EnableWindow(btn_manage_, TRUE);
    EnableWindow(btn_set_active_, TRUE);
    EnableWindow(btn_dup_, TRUE);
    EnableWindow(btn_delete_, TRUE);
}

void EnvironmentSettingsUi::fill_detail(ProjectEnvironmentManager& mgr, Keyring& keyring) {
    var_names_.clear();
    var_primary_.clear();
    var_secondary_.clear();
    const auto* env = mgr.find_environment(project_id_, detail_env_id_);
    if (!env) {
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        return;
    }
    SetWindowTextW(detail_title_, utf16(env->name).c_str());
    SetWindowTextW(detail_name_, utf16(env->name).c_str());
    fill_inherit_select(mgr, inherit_, detail_env_id_);
    if (!vars_) {
        return;
    }
    SendMessageW(vars_, LB_RESETCONTENT, 0, 0);
    if (env->variables.empty()) {
        var_primary_.push_back(L"No variables yet");
        var_secondary_.push_back(L"Add a plain value or a protected Keyring reference.");
        SendMessageW(vars_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(var_primary_.back().c_str()));
        EnableWindow(btn_edit_var_, FALSE);
        EnableWindow(btn_remove_var_, FALSE);
        return;
    }
    for (const auto& v : env->variables) {
        var_names_.push_back(v.name);
        std::wstring primary = utf16(v.name);
        var_primary_.push_back(primary);
        std::wstring sec;
        if (v.kind == EnvVarKind::SecretRef) {
            sec = L"Protected  ·  ";
            sec += utf16(v.secret_id);
            if (secret_missing(keyring, v.secret_id)) {
                sec += L"  ·  ! Missing";
            }
        } else {
            sec = L"Plain  ·  ";
            sec += utf16(v.plain_value);
        }
        sec += L"\n";
        sec += avail_summary(v.availability);
        var_secondary_.push_back(sec);
        SendMessageW(vars_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(var_primary_.back().c_str()));
    }
    SendMessageW(vars_, LB_SETCURSEL, 0, 0);
    EnableWindow(btn_edit_var_, TRUE);
    EnableWindow(btn_remove_var_, TRUE);
}

std::string EnvironmentSettingsUi::selected_landing_env_id(ProjectEnvironmentManager& /*mgr*/) const {
    if (!list_ || landing_ids_.empty()) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(landing_ids_.size())) {
        return {};
    }
    return landing_ids_[static_cast<std::size_t>(sel)];
}

std::string EnvironmentSettingsUi::selected_var_name(ProjectEnvironmentManager& /*mgr*/) const {
    if (!vars_ || var_names_.empty()) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(vars_, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(var_names_.size())) {
        return {};
    }
    return var_names_[static_cast<std::size_t>(sel)];
}

void EnvironmentSettingsUi::reload(ProjectEnvironmentManager& mgr, Keyring& keyring,
                                   const std::string& project_id, const std::wstring& project_name,
                                   const std::wstring& /*persist_path*/) {
    project_id_ = project_id;
    project_name_ = project_name;
    if (project_id.empty()) {
        SetWindowTextW(project_lbl_, L"Open a project to manage environments.");
        EnableWindow(btn_add_, FALSE);
        EnableWindow(active_, FALSE);
    } else {
        std::wstring s = project_name.empty() ? utf16(project_id) : project_name;
        SetWindowTextW(project_lbl_, s.c_str());
        mgr.ensure_project(project_id);
        EnableWindow(btn_add_, TRUE);
        EnableWindow(active_, TRUE);
    }
    fill_active_select(mgr);
    if (mode_ == Mode::Detail || mode_ == Mode::AddVar) {
        if (mgr.find_environment(project_id_, detail_env_id_)) {
            fill_detail(mgr, keyring);
            if (mode_ == Mode::AddVar) {
                update_var_form_fields(keyring);
            }
        } else {
            mode_ = Mode::Landing;
            fill_landing(mgr, keyring);
        }
    } else if (mode_ == Mode::AddEnv) {
        fill_inherit_select(mgr, add_env_inherit_, {});
    } else {
        mode_ = Mode::Landing;
        fill_landing(mgr, keyring);
    }
    apply_visibility();
}

bool EnvironmentSettingsUi::owns_hwnd(HWND child) const {
    for (HWND h : all_controls()) {
        if (h && h == child) {
            return true;
        }
    }
    return false;
}

bool EnvironmentSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || !parent_) {
        return false;
    }
    if (mi->CtlID != Cmd_EnvList && mi->CtlID != Cmd_EnvVars) {
        return false;
    }
    mi->itemHeight = static_cast<UINT>(dip(parent_, 48));
    return true;
}

bool EnvironmentSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di || (di->CtlID != Cmd_EnvList && di->CtlID != Cmd_EnvVars)) {
        return false;
    }
    const bool env_list = di->CtlID == Cmd_EnvList;
    const auto& primary = env_list ? landing_primary_ : var_primary_;
    const auto& secondary = env_list ? landing_secondary_ : var_secondary_;
    if (di->itemID >= primary.size()) {
        return true;
    }
    const bool sel = (di->itemState & ODS_SELECTED) != 0;
    ui_kit::paint_entity_row(di->hDC, di->rcItem, font ? font : font_, primary[di->itemID].c_str(),
                             di->itemID < secondary.size() ? secondary[di->itemID].c_str() : L"", sel, false);
    return true;
}

UINT EnvironmentSettingsUi::default_command(HWND field) const {
    if (mode_ == Mode::AddEnv && field == add_env_name_) {
        return Cmd_EnvAddEnvSave;
    }
    if (mode_ == Mode::AddVar && (field == var_name_ || field == var_value_)) {
        return Cmd_EnvVarSave;
    }
    if (mode_ == Mode::Detail && field == detail_name_) {
        return 0;
    }
    return 0;
}

UINT EnvironmentSettingsUi::cancel_command() const {
    if (mode_ == Mode::AddEnv) {
        return Cmd_EnvAddEnvCancel;
    }
    if (mode_ == Mode::AddVar) {
        return Cmd_EnvVarCancel;
    }
    if (mode_ == Mode::Detail) {
        return Cmd_EnvBack;
    }
    return 0;
}

bool EnvironmentSettingsUi::on_command(WORD id, WORD notify, HWND owner, ProjectEnvironmentManager& mgr,
                                       Keyring& keyring, const std::string& project_id,
                                       const std::wstring& persist_path) {
    if (project_id.empty() && id != Cmd_EnvBack) {
        if (id >= Cmd_EnvTitle && id <= Cmd_EnvVarCancel) {
            return true;
        }
        return false;
    }

    if (id == Cmd_EnvActive) {
        if (ui_kit::select_handle_command(active_, notify)) {
            return true;
        }
        if (notify == CBN_SELCHANGE) {
            const int sel = ui_kit::select_get_index(active_);
            const auto* st = mgr.state_for(project_id);
            if (!st) {
                return true;
            }
            if (sel <= 0) {
                mgr.set_active(project_id, "");
            } else if (sel - 1 < static_cast<int>(st->environments.size())) {
                mgr.set_active(project_id, st->environments[static_cast<std::size_t>(sel - 1)].id);
            }
            persist(mgr, persist_path, owner);
            fill_landing(mgr, keyring);
            return true;
        }
        return false;
    }
    if (id == Cmd_EnvInherit || id == Cmd_EnvAddEnvInherit || id == Cmd_EnvVarSecret) {
        HWND h = (id == Cmd_EnvInherit)           ? inherit_
                 : (id == Cmd_EnvAddEnvInherit)   ? add_env_inherit_
                                                  : var_secret_;
        return ui_kit::select_handle_command(h, notify);
    }
    if (id == Cmd_EnvList && notify == LBN_DBLCLK) {
        id = Cmd_EnvManage;
        notify = 0;
    }
    if (id == Cmd_EnvList && notify == LBN_SELCHANGE) {
        return true;
    }
    if (id == Cmd_EnvVars && notify == LBN_DBLCLK) {
        id = Cmd_EnvEditVar;
        notify = 0;
    }
    if (id == Cmd_EnvVars && notify == LBN_SELCHANGE) {
        return true;
    }

    if (id == Cmd_EnvAdd) {
        SetWindowTextW(add_env_name_, L"");
        fill_inherit_select(mgr, add_env_inherit_, {});
        set_mode(Mode::AddEnv);
        SetFocus(add_env_name_);
        return true;
    }
    if (id == Cmd_EnvAddEnvCancel) {
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        return true;
    }
    if (id == Cmd_EnvAddEnvSave) {
        std::string name = utf8(get_text(add_env_name_));
        if (name.empty()) {
            MessageBoxW(owner, L"Enter an environment name.", L"Add Environment", MB_OK | MB_ICONWARNING);
            return true;
        }
        const std::string env_id = mgr.add_environment(project_id, name, {});
        // Apply inherit from select by matching label.
        const int isel = ui_kit::select_get_index(add_env_inherit_);
        if (isel > 0) {
            const auto* st = mgr.state_for(project_id);
            if (st) {
                int idx = 0;
                for (const auto& e : st->environments) {
                    if (e.id == env_id) {
                        continue;
                    }
                    ++idx;
                    if (idx == isel) {
                        if (auto* mut = mgr.find_environment_mut(project_id, env_id)) {
                            mut->inherits_from_id = e.id;
                        }
                        break;
                    }
                }
            }
        }
        persist(mgr, persist_path, owner);
        detail_env_id_ = env_id;
        set_mode(Mode::Detail);
        fill_detail(mgr, keyring);
        fill_active_select(mgr);
        return true;
    }
    if (id == Cmd_EnvManage) {
        detail_env_id_ = selected_landing_env_id(mgr);
        if (detail_env_id_.empty()) {
            return true;
        }
        set_mode(Mode::Detail);
        fill_detail(mgr, keyring);
        return true;
    }
    if (id == Cmd_EnvSetActive) {
        const std::string env_id = selected_landing_env_id(mgr);
        if (env_id.empty()) {
            return true;
        }
        mgr.set_active(project_id, env_id);
        persist(mgr, persist_path, owner);
        fill_active_select(mgr);
        fill_landing(mgr, keyring);
        return true;
    }
    if (id == Cmd_EnvDup || id == Cmd_EnvDetailDup) {
        const std::string env_id =
            (id == Cmd_EnvDetailDup) ? detail_env_id_ : selected_landing_env_id(mgr);
        if (env_id.empty()) {
            return true;
        }
        mgr.duplicate_environment(project_id, env_id, {});
        persist(mgr, persist_path, owner);
        if (mode_ == Mode::Detail) {
            fill_detail(mgr, keyring);
        } else {
            fill_landing(mgr, keyring);
        }
        fill_active_select(mgr);
        return true;
    }
    if (id == Cmd_EnvDelete || id == Cmd_EnvDetailDelete) {
        const std::string env_id =
            (id == Cmd_EnvDetailDelete) ? detail_env_id_ : selected_landing_env_id(mgr);
        if (env_id.empty()) {
            return true;
        }
        const auto* env = mgr.find_environment(project_id, env_id);
        if (!env) {
            return true;
        }
        const auto* st = mgr.state_for(project_id);
        const bool is_active = st && st->active_environment_id == env_id;
        std::wstring consequence = L"Existing terminals will not be affected.";
        if (is_active) {
            consequence = L"This is the active environment for new terminals. Existing terminals are unchanged.";
        }
        if (!ui_kit::confirm_destructive(owner, L"Delete environment", utf16(env->name).c_str(),
                                         consequence.c_str())) {
            return true;
        }
        mgr.delete_environment(project_id, env_id);
        persist(mgr, persist_path, owner);
        detail_env_id_.clear();
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        fill_active_select(mgr);
        return true;
    }
    if (id == Cmd_EnvBack) {
        if (mode_ == Mode::AddVar) {
            set_mode(Mode::Detail);
            fill_detail(mgr, keyring);
            return true;
        }
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        fill_active_select(mgr);
        return true;
    }
    if (id == Cmd_EnvInherit && notify == CBN_SELCHANGE) {
        auto* env = mgr.find_environment_mut(project_id, detail_env_id_);
        if (!env) {
            return true;
        }
        const int isel = ui_kit::select_get_index(inherit_);
        env->inherits_from_id.clear();
        if (isel > 0) {
            const auto* st = mgr.state_for(project_id);
            if (st) {
                int idx = 0;
                for (const auto& e : st->environments) {
                    if (e.id == detail_env_id_) {
                        continue;
                    }
                    ++idx;
                    if (idx == isel) {
                        env->inherits_from_id = e.id;
                        break;
                    }
                }
            }
        }
        persist(mgr, persist_path, owner);
        return true;
    }
    if (id == Cmd_EnvDetailName && notify == EN_KILLFOCUS) {
        const std::string name = utf8(get_text(detail_name_));
        if (!name.empty()) {
            mgr.rename_environment(project_id, detail_env_id_, name);
            persist(mgr, persist_path, owner);
            SetWindowTextW(detail_title_, utf16(name).c_str());
            fill_active_select(mgr);
        }
        return true;
    }
    if (id == Cmd_EnvAddVar) {
        editing_var_name_.clear();
        SetWindowTextW(detail_title_, L"Add Environment Variable");
        SetWindowTextW(var_name_, L"");
        SetWindowTextW(var_value_, L"");
        SendMessageW(var_plain_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(var_protected_, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(var_avail_human_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(var_avail_agent_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(var_avail_recipes_, BM_SETCHECK, BST_CHECKED, 0);
        set_mode(Mode::AddVar);
        update_var_form_fields(keyring);
        SetFocus(var_name_);
        return true;
    }
    if (id == Cmd_EnvEditVar) {
        const std::string vname = selected_var_name(mgr);
        const auto* env = mgr.find_environment(project_id, detail_env_id_);
        if (!env || vname.empty()) {
            return true;
        }
        const ProjectEnvironmentVariable* found = nullptr;
        for (const auto& v : env->variables) {
            if (v.name == vname) {
                found = &v;
                break;
            }
        }
        if (!found) {
            return true;
        }
        editing_var_name_ = found->name;
        SetWindowTextW(detail_title_, L"Edit Environment Variable");
        SetWindowTextW(var_name_, utf16(found->name).c_str());
        SetWindowTextW(var_value_, utf16(found->plain_value).c_str());
        const bool prot = found->kind == EnvVarKind::SecretRef;
        SendMessageW(var_plain_, BM_SETCHECK, prot ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageW(var_protected_, BM_SETCHECK, prot ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(var_avail_human_, BM_SETCHECK, found->availability.human_terminal ? BST_CHECKED : BST_UNCHECKED,
                     0);
        SendMessageW(var_avail_agent_, BM_SETCHECK, found->availability.agent_terminal ? BST_CHECKED : BST_UNCHECKED,
                     0);
        SendMessageW(var_avail_recipes_, BM_SETCHECK,
                     found->availability.approved_recipes ? BST_CHECKED : BST_UNCHECKED, 0);
        set_mode(Mode::AddVar);
        update_var_form_fields(keyring);
        if (prot && keyring.is_unlocked()) {
            for (std::size_t i = 0; i < secret_names_.size(); ++i) {
                if (secret_names_[i] == found->secret_id) {
                    ui_kit::select_set_index(var_secret_, static_cast<int>(i));
                    break;
                }
            }
        }
        return true;
    }
    if (id == Cmd_EnvRemoveVar) {
        const std::string vname = selected_var_name(mgr);
        if (vname.empty()) {
            return true;
        }
        if (!ui_kit::confirm_destructive(owner, L"Remove variable", utf16(vname).c_str(),
                                         L"The Keyring secret itself is not deleted.")) {
            return true;
        }
        mgr.remove_variable(project_id, detail_env_id_, vname);
        persist(mgr, persist_path, owner);
        fill_detail(mgr, keyring);
        return true;
    }
    if (id == Cmd_EnvVarPlain || id == Cmd_EnvVarProtected) {
        update_var_form_fields(keyring);
        return true;
    }
    if (id == Cmd_EnvVarUnlock) {
        nav_ = EnvUiNavRequest::OpenKeyring;
        return true;
    }
    if (id == Cmd_EnvVarCancel) {
        set_mode(Mode::Detail);
        fill_detail(mgr, keyring);
        return true;
    }
    if (id == Cmd_EnvVarSave) {
        const bool protected_sel =
            var_protected_ && SendMessageW(var_protected_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        ProjectEnvironmentVariable v;
        v.name = utf8(get_text(var_name_));
        v.availability.human_terminal =
            var_avail_human_ && SendMessageW(var_avail_human_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.availability.agent_terminal =
            var_avail_agent_ && SendMessageW(var_avail_agent_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.availability.approved_recipes =
            var_avail_recipes_ && SendMessageW(var_avail_recipes_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (protected_sel) {
            if (!Keyring::app_vault_exists() && !keyring.vault_bound()) {
                MessageBoxW(owner,
                            L"Selecting a protected secret requires the Scylla Keyring.\n\n"
                            L"Create the Keyring under Security → Keyring first.",
                            L"Keyring required", MB_OK | MB_ICONWARNING);
                nav_ = EnvUiNavRequest::OpenKeyring;
                return true;
            }
            if (!keyring.is_unlocked()) {
                MessageBoxW(owner,
                            L"Unlock the Scylla Keyring to bind a protected secret.",
                            L"Keyring locked", MB_OK | MB_ICONWARNING);
                nav_ = EnvUiNavRequest::OpenKeyring;
                return true;
            }
            const int sel = ui_kit::select_get_index(var_secret_);
            if (sel < 0 || sel >= static_cast<int>(secret_names_.size())) {
                MessageBoxW(owner, L"Select a Keyring secret.", L"Protected Secret", MB_OK | MB_ICONWARNING);
                return true;
            }
            v.kind = EnvVarKind::SecretRef;
            v.secret_id = secret_names_[static_cast<std::size_t>(sel)];
            v.availability.agent_terminal = false;
        } else {
            v.kind = EnvVarKind::Plain;
            v.plain_value = utf8(get_text(var_value_));
        }
        bool ok = false;
        if (editing_var_name_.empty()) {
            ok = mgr.add_variable(project_id, detail_env_id_, v);
        } else {
            ok = mgr.update_variable(project_id, detail_env_id_, editing_var_name_, v);
        }
        if (!ok) {
            MessageBoxW(owner,
                        L"Could not save that variable.\n\nNames must start with a letter or underscore "
                        L"and contain only letters, digits, and underscores.",
                        L"Environment Variable", MB_OK | MB_ICONWARNING);
            return true;
        }
        persist(mgr, persist_path, owner);
        editing_var_name_.clear();
        set_mode(Mode::Detail);
        fill_detail(mgr, keyring);
        return true;
    }
    return false;
}

}  // namespace scyllagpt
