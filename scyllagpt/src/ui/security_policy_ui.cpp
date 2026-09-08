#include "scyllagpt/security_policy_ui.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"

#include <algorithm>
#include <vector>

namespace scyllagpt {
namespace {

constexpr wchar_t kStrictExplanation[] =
    L"Strict is the shipped rule: protected Keyring values reach human terminals and approved "
    L"recipes only. An agent terminal resolves plain variables and never receives protected "
    L"values, even while the Keyring is unlocked.";

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

}  // namespace

bool SecurityPolicyUi::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = inst ? inst : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    font_small_ = font_small ? font_small : font;

    title_ = ui_kit::create_static(parent_, inst_, 0, L"Execution Policy", font_, false);
    desc_ = ui_kit::create_static(parent_, inst_, 0,
                                  L"Where protected (Keyring-backed) environment values are allowed to go.",
                                  font_small_, true);
    status_ = ui_kit::create_static(parent_, inst_, Cmd_SecPolStatus, L"", font_small_, true);

    human_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_SecPolHumanLbl, L"Human terminals", font_, false);
    human_ = ui_kit::create_select(parent_, inst_, Cmd_SecPolHuman, font_);
    human_hint_ = ui_kit::create_static(
        parent_, inst_, Cmd_SecPolHumanHint,
        L"Enforced on launch. Ask confirms before a terminal receives protected values; Blocked "
        L"refuses to start one.",
        font_small_, true);

    agent_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_SecPolAgentLbl, L"Agent terminals", font_, false);
    agent_ = ui_kit::create_select(parent_, inst_, Cmd_SecPolAgent, font_);
    agent_hint_ = ui_kit::create_static(
        parent_, inst_, Cmd_SecPolAgentHint,
        L"Allowed is not offered. Strict resolve drops protected values before an agent terminal "
        L"starts, so only Blocked and Ask describe real behaviour.",
        font_small_, true);

    recipes_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_SecPolRecipesLbl, L"Approved recipes", font_, false);
    recipes_ = ui_kit::create_select(parent_, inst_, Cmd_SecPolRecipes, font_);
    recipes_hint_ = ui_kit::create_static(
        parent_, inst_, Cmd_SecPolRecipesHint,
        L"Recorded now, enforced when approved recipes ship. Nothing runs recipes yet.",
        font_small_, true);

    strict_ = ui_kit::create_static(parent_, inst_, Cmd_SecPolStrict, kStrictExplanation, font_small_, true);
    reset_ = ui_kit::create_button(parent_, inst_, Cmd_SecPolReset, L"Reset to Strict",
                                   ui_kit::ButtonKind::Secondary, font_);

    ui_kit::set_tab_order({human_, agent_, recipes_, reset_});
    hide_all();
    return true;
}

void SecurityPolicyUi::destroy() {
    hide_all();
    for (HWND h : all_controls()) {
        if (h) {
            DestroyWindow(h);
        }
    }
    title_ = desc_ = status_ = nullptr;
    human_lbl_ = human_ = human_hint_ = nullptr;
    agent_lbl_ = agent_ = agent_hint_ = nullptr;
    recipes_lbl_ = recipes_ = recipes_hint_ = nullptr;
    strict_ = reset_ = nullptr;
    parent_ = nullptr;
    visible_ = false;
}

void SecurityPolicyUi::set_fonts(HFONT font, HFONT font_small) {
    font_ = font;
    font_small_ = font_small ? font_small : font;
    for (HWND h : all_controls()) {
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
}

std::vector<HWND> SecurityPolicyUi::all_controls() const {
    return {title_,       desc_,        status_,        human_lbl_,   human_,
            human_hint_,  agent_lbl_,   agent_,         agent_hint_,  recipes_lbl_,
            recipes_,     recipes_hint_, strict_,       reset_};
}

void SecurityPolicyUi::hide_all() {
    show_many(all_controls(), SW_HIDE);
}

void SecurityPolicyUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    show_many(all_controls(), SW_SHOW);
}

void SecurityPolicyUi::layout(const RECT& area) {
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
    const int hint_h = ui_space::dip(parent_, 32);
    const int label_w = (std::min)(m.label_w + ui_space::dip(parent_, 40), w / 2);
    const int select_w = (std::min)((std::max)(1, w - label_w - gap), ui_space::dip(parent_, 220));

    MoveWindow(title_, x, y, w, row, TRUE);
    y += row + ui_space::dip(parent_, ui_space::kPadXsDip);
    MoveWindow(desc_, x, y, w, ui_space::dip(parent_, 20), TRUE);
    y += ui_space::dip(parent_, 20) + gap;
    MoveWindow(status_, x, y, w, ui_space::dip(parent_, 20), TRUE);
    y += ui_space::dip(parent_, 20) + section;

    // Each rule is one row (label + select) followed by a muted enforcement note, so the page
    // never states a rule without saying whether it is live.
    auto place_rule = [&](HWND label, HWND select, HWND hint) {
        MoveWindow(label, x, y, label_w, row, TRUE);
        MoveWindow(select, x + label_w + gap, y, select_w, row, TRUE);
        y += row + ui_space::dip(parent_, ui_space::kPadXsDip);
        MoveWindow(hint, x, y, w, hint_h, TRUE);
        y += hint_h + section;
    };
    place_rule(human_lbl_, human_, human_hint_);
    place_rule(agent_lbl_, agent_, agent_hint_);
    place_rule(recipes_lbl_, recipes_, recipes_hint_);

    const int strict_h = ui_space::dip(parent_, 56);
    MoveWindow(strict_, x, y, w, strict_h, TRUE);
    y += strict_h + gap;
    MoveWindow(reset_, x, y, m.btn_w + ui_space::dip(parent_, 40), row, TRUE);
}

void SecurityPolicyUi::fill_mode_select(HWND select, bool allow_permitted, PolicyMode current) {
    if (!select) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    if (allow_permitted) {
        items.push_back({policy_mode_label(PolicyMode::Allow), static_cast<LPARAM>(PolicyMode::Allow)});
    }
    items.push_back({policy_mode_label(PolicyMode::Ask), static_cast<LPARAM>(PolicyMode::Ask)});
    items.push_back({policy_mode_label(PolicyMode::Block), static_cast<LPARAM>(PolicyMode::Block)});
    ui_kit::select_set_items(select, items);
    ui_kit::select_set_index(select, 0);
    ui_kit::select_set_by_data(select, static_cast<LPARAM>(current));
}

void SecurityPolicyUi::update_strict_line(const ExecutionPolicy& policy) {
    if (!status_) {
        return;
    }
    std::wstring line;
    if (execution_policy_is_strict(policy)) {
        line = L"Strict (recommended) — human terminals ";
    } else {
        line = L"Custom — human terminals ";
    }
    line += policy_mode_label(policy.human_terminals);
    line += L" · agent terminals ";
    line += policy_mode_label(policy.agent_terminals);
    line += L" · approved recipes ";
    line += policy_mode_label(policy.approved_recipes);
    SetWindowTextW(status_, line.c_str());
}

void SecurityPolicyUi::reload(const ExecutionPolicy& policy) {
    fill_mode_select(human_, true, policy.human_terminals);
    fill_mode_select(agent_, false, policy.agent_terminals);
    fill_mode_select(recipes_, true, policy.approved_recipes);
    update_strict_line(policy);
    if (reset_) {
        EnableWindow(reset_, execution_policy_is_strict(policy) ? FALSE : TRUE);
    }
}

bool SecurityPolicyUi::on_command(WORD id, WORD notify, HWND owner, Settings& settings,
                                  const std::wstring& settings_path) {
    if (id != Cmd_SecPolHuman && id != Cmd_SecPolAgent && id != Cmd_SecPolRecipes &&
        id != Cmd_SecPolReset) {
        return false;
    }
    if (id == Cmd_SecPolReset) {
        if (notify != BN_CLICKED) {
            return true;
        }
        settings.execution_policy = strict_execution_policy();
    } else {
        HWND select = id == Cmd_SecPolHuman ? human_ : (id == Cmd_SecPolAgent ? agent_ : recipes_);
        if (ui_kit::select_handle_command(select, notify)) {
            return true;  // opened / closed the popup; no value change yet
        }
        if (notify != CBN_SELCHANGE) {
            return true;
        }
        const auto mode = static_cast<PolicyMode>(ui_kit::select_get_data(select));
        if (id == Cmd_SecPolHuman) {
            settings.execution_policy.human_terminals = mode;
        } else if (id == Cmd_SecPolAgent) {
            // Mirrors the clamp in load_settings: Allow is never a truthful agent value.
            settings.execution_policy.agent_terminals =
                mode == PolicyMode::Allow ? PolicyMode::Block : mode;
        } else {
            settings.execution_policy.approved_recipes = mode;
        }
    }
    if (!save_settings(settings_path, settings)) {
        ui_kit::report_save_failure(owner, L"execution policy", settings_path);
    }
    reload(settings.execution_policy);
    return true;
}

bool SecurityPolicyUi::owns_hwnd(HWND child) const {
    for (HWND h : all_controls()) {
        if (h && h == child) {
            return true;
        }
    }
    return false;
}

}  // namespace scyllagpt
