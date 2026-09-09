#include "scyllagpt/security_overview_ui.h"

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

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

}  // namespace

bool SecurityOverviewUi::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = inst ? inst : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    font_small_ = font_small ? font_small : font;

    title_ = ui_kit::create_static(parent_, inst_, 0, L"Security · credentials and execution",
                                   font_, false);
    kr_heading_ = ui_kit::create_static(parent_, inst_, 0, L"SCYLLA KEYRING", font_small_, true);
    kr_status_ = ui_kit::create_static(parent_, inst_, Cmd_SecOvKeyringStatus, L"", font_small_, true);
    kr_primary_ = ui_kit::create_button(parent_, inst_, Cmd_SecOvKeyringPrimary, L"Create Keyring",
                                        ui_kit::ButtonKind::Primary, font_);
    kr_manage_ = ui_kit::create_button(parent_, inst_, Cmd_SecOvKeyringManage, L"Manage Keyring",
                                       ui_kit::ButtonKind::Secondary, font_);
    proj_heading_ = ui_kit::create_static(parent_, inst_, 0, L"ACTIVE PROJECT", font_small_, true);
    proj_status_ = ui_kit::create_static(parent_, inst_, Cmd_SecOvProjectStatus, L"", font_small_, true);
    proj_env_btn_ = ui_kit::create_button(parent_, inst_, Cmd_SecOvManageEnv, L"Manage Project Environment",
                                          ui_kit::ButtonKind::Secondary, font_);
    proj_secrets_btn_ =
        ui_kit::create_button(parent_, inst_, Cmd_SecOvManageSecrets, L"Manage Project Secrets",
                              ui_kit::ButtonKind::Secondary, font_);
    pol_heading_ = ui_kit::create_static(parent_, inst_, 0, L"EXECUTION POLICY", font_small_, true);
    pol_status_ = ui_kit::create_static(parent_, inst_, Cmd_SecOvPolicyStatus, L"", font_small_, true);
    pol_btn_ = ui_kit::create_button(parent_, inst_, Cmd_SecOvManagePolicy, L"Manage Policy",
                                     ui_kit::ButtonKind::Ghost, font_);
    hide_all();
    return true;
}

void SecurityOverviewUi::destroy() {
    hide_all();
    for (HWND h : all_controls()) {
        if (h) {
            DestroyWindow(h);
        }
    }
    title_ = kr_heading_ = kr_status_ = kr_primary_ = kr_manage_ = nullptr;
    proj_heading_ = proj_status_ = proj_env_btn_ = proj_secrets_btn_ = nullptr;
    pol_heading_ = pol_status_ = pol_btn_ = nullptr;
    parent_ = nullptr;
    visible_ = false;
}

void SecurityOverviewUi::set_fonts(HFONT font, HFONT font_small) {
    font_ = font;
    font_small_ = font_small ? font_small : font;
    for (HWND h : all_controls()) {
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
}

std::vector<HWND> SecurityOverviewUi::all_controls() const {
    return {title_,        kr_heading_, kr_status_,       kr_primary_,      kr_manage_,
            proj_heading_, proj_status_, proj_env_btn_,   proj_secrets_btn_, pol_heading_,
            pol_status_,   pol_btn_};
}

void SecurityOverviewUi::hide_all() {
    show_many(all_controls(), SW_HIDE);
}

void SecurityOverviewUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    show_many(all_controls(), SW_SHOW);
}

void SecurityOverviewUi::layout(const RECT& area) {
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    const int x = col.left + m.pad_outer;
    const int right = col.right - m.pad_outer;
    const int w = (std::max)(1, right - x);
    int y = col.top + m.pad_outer;
    const int row = m.row_h;
    const int gap = m.pad_tight;
    const int section = m.pad_section;
    // Security explanations intentionally spell out capability boundaries; reserve enough height
    // for three to four wrapped lines instead of clipping them like status-only copy.
    const int desc_h = ui_space::dip(parent_, 72);

    MoveWindow(title_, x, y, w, row, TRUE);
    y += row + gap;
    MoveWindow(kr_heading_, x, y, w, row, TRUE);
    y += row;
    MoveWindow(kr_status_, x, y, w, desc_h, TRUE);
    y += desc_h + gap;
    const int btn_w = m.btn_w + dip(parent_, 40);
    MoveWindow(kr_primary_, x, y, btn_w, row, TRUE);
    MoveWindow(kr_manage_, x + btn_w + gap, y, btn_w + dip(parent_, 20), row, TRUE);
    y += row + section;

    MoveWindow(proj_heading_, x, y, w, row, TRUE);
    y += row;
    MoveWindow(proj_status_, x, y, w, desc_h + dip(parent_, 16), TRUE);
    y += desc_h + dip(parent_, 16) + gap;
    const int wide = m.btn_w + dip(parent_, 100);
    MoveWindow(proj_env_btn_, x, y, wide, row, TRUE);
    MoveWindow(proj_secrets_btn_, x + wide + gap, y, wide, row, TRUE);
    y += row + section;

    MoveWindow(pol_heading_, x, y, w, row, TRUE);
    y += row;
    MoveWindow(pol_status_, x, y, w, desc_h, TRUE);
    y += desc_h + gap;
    MoveWindow(pol_btn_, x, y, btn_w, row, TRUE);
}

void SecurityOverviewUi::reload(Keyring& keyring, ProjectEnvironmentManager& envs,
                                const std::string& project_id, const std::wstring& project_name,
                                const ExecutionPolicy& policy) {
    vault_exists_ = Keyring::app_vault_exists() || keyring.vault_bound();
    unlocked_ = keyring.is_unlocked();

    std::wstring kr;
    if (!vault_exists_) {
        kr = L"Not configured\r\n\r\nCreate an encrypted Keyring to store secret values. "
             L"Keyring storage alone does not create an SSH or database connection.";
        SetWindowTextW(kr_primary_, L"Create Keyring");
        ShowWindow(kr_primary_, SW_SHOW);
        EnableWindow(kr_primary_, TRUE);
    } else if (!unlocked_) {
        std::size_t n = 0;
        if (keyring.is_unlocked()) {
            n = keyring.list_refs().size();
        }
        kr = L"Locked\r\n\r\nUnlock to manage secrets or let an approved Scylla workflow "
             L"use their references. Secret values are never returned to the agent.";
        SetWindowTextW(kr_primary_, L"Unlock Keyring");
        ShowWindow(kr_primary_, SW_SHOW);
        EnableWindow(kr_primary_, TRUE);
        (void)n;
    } else {
        const auto refs = keyring.list_refs();
        kr = L"Unlocked\r\n";
        kr += std::to_wstring(refs.size());
        kr += L" stored secrets. Approved workflows and connection routes reference them "
              L"directly; secret values remain inside Scylla.";
        SetWindowTextW(kr_primary_, L"Lock");
        ShowWindow(kr_primary_, SW_SHOW);
        EnableWindow(kr_primary_, TRUE);
    }
    SetWindowTextW(kr_status_, kr.c_str());

    std::wstring proj;
    if (project_id.empty()) {
        proj = L"No project open.\r\n\r\nOpen a folder to establish project scope for environments "
               L"and brokered connections.";
        EnableWindow(proj_env_btn_, FALSE);
        EnableWindow(proj_secrets_btn_, FALSE);
    } else {
        proj = project_name.empty() ? utf16(project_id) : project_name;
        proj += L"\r\n\r\n";
        const auto* st = envs.state_for(project_id);
        int env_count = st ? static_cast<int>(st->environments.size()) : 0;
        std::wstring active = L"None";
        int vars = 0;
        int protected_n = 0;
        if (st && !st->active_environment_id.empty()) {
            if (const auto* e = envs.find_environment(project_id, st->active_environment_id)) {
                active = utf16(e->name);
                vars = static_cast<int>(e->variables.size());
                for (const auto& v : e->variables) {
                    if (v.kind == EnvVarKind::SecretRef) {
                        ++protected_n;
                    }
                }
            }
        }
        proj += L"Environment: ";
        proj += active;
        proj += L"\r\n";
        proj += std::to_wstring(env_count);
        proj += L" environments · ";
        proj += std::to_wstring(vars);
        proj += L" variables";
        if (protected_n) {
            proj += L" · ";
            proj += std::to_wstring(protected_n);
            proj += L" protected";
        }
        if (!vault_exists_) {
            proj += L"\r\nProtected variables unavailable until Keyring is created.";
        } else {
            proj += L"\r\nOptional: environments apply variables to launched processes. Brokered "
                    L"connections bind Keyring references directly and do not require one.";
        }
        EnableWindow(proj_env_btn_, TRUE);
        EnableWindow(proj_secrets_btn_, vault_exists_ ? TRUE : FALSE);
    }
    SetWindowTextW(proj_status_, proj.c_str());

    std::wstring pol = execution_policy_is_strict(policy) ? L"Strict" : L"Custom";
    pol += L"\r\n\r\nHuman terminals: ";
    pol += policy_mode_label(policy.human_terminals);
    pol += L" · Agent terminals: ";
    pol += policy_mode_label(policy.agent_terminals);
    pol += L" · Approved recipes: ";
    pol += policy_mode_label(policy.approved_recipes);
    SetWindowTextW(pol_status_, pol.c_str());
}

SecurityOverviewAction SecurityOverviewUi::on_command(WORD id, WORD /*notify*/) {
    if (id == Cmd_SecOvKeyringPrimary) {
        if (vault_exists_ && unlocked_) {
            return SecurityOverviewAction::LockKeyring;
        }
        return SecurityOverviewAction::GotoKeyring;
    }
    if (id == Cmd_SecOvKeyringManage || id == Cmd_SecOvManageSecrets) {
        return SecurityOverviewAction::GotoKeyring;
    }
    if (id == Cmd_SecOvManageEnv) {
        return SecurityOverviewAction::GotoEnvironments;
    }
    if (id == Cmd_SecOvManagePolicy) {
        return SecurityOverviewAction::GotoPolicy;
    }
    return SecurityOverviewAction::None;
}

bool SecurityOverviewUi::owns_hwnd(HWND child) const {
    for (HWND h : all_controls()) {
        if (h && h == child) {
            return true;
        }
    }
    return false;
}

}  // namespace scyllagpt
