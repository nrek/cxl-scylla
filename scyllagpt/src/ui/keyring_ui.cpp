#include "scyllagpt/keyring_ui.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace scyllagpt {
namespace {

constexpr int kMinPassLen = 8;

void secure_wipe_wstring(std::wstring& s) {
    if (!s.empty()) {
        SecureZeroMemory(s.data(), s.size() * sizeof(wchar_t));
        s.clear();
    }
}

}  // namespace

KeyringUi::~KeyringUi() {
    destroy();
}

void KeyringUi::create(HWND parent, HFONT font, HFONT font_small, HFONT font_semi) {
    parent_ = parent;
    font_ = font;
    font_small_ = font_small;
    font_semi_ = font_semi;
    const HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));

    title_ = ui_kit::create_static(parent, inst, IdTitle, L"SCYLLA KEYRING", font_semi ? font_semi : font, false);
    subtitle_ = ui_kit::create_static(parent, inst, IdSubtitle, L"", font, true);
    message_ = ui_kit::create_static(parent, inst, IdMessage, L"", font, false);
    warning_ = ui_kit::create_static(parent, inst, IdWarning, L"", font_small ? font_small : font, true);
    strength_ = ui_kit::create_static(parent, inst, IdStrength, L"", font_small ? font_small : font, true);

    pass_ = ui_kit::create_text_field(parent, inst, IdPassphrase, font, true);
    pass_confirm_ = ui_kit::create_text_field(parent, inst, IdPassConfirm, font, true);
    unlock_pass_ = ui_kit::create_text_field(parent, inst, IdUnlockPass, font, true);

    btn_primary_ =
        ui_kit::create_button(parent, inst, IdBtnPrimary, L"Create Scylla Keyring", ui_kit::ButtonKind::Primary, font);
    btn_secondary_ =
        ui_kit::create_button(parent, inst, IdBtnSecondary, L"Cancel", ui_kit::ButtonKind::Secondary, font);
    btn_lock_ = ui_kit::create_button(parent, inst, IdBtnLock, L"Lock", ui_kit::ButtonKind::Secondary, font);
    btn_add_ = ui_kit::create_button(parent, inst, IdBtnAdd, L"Add Key", ui_kit::ButtonKind::Primary, font);

    list_ = ui_kit::create_list(parent, inst, IdList, font, false);
    ui_kit::style_scroll_host(list_, theme().panel);

    btn_copy_ref_ =
        ui_kit::create_button(parent, inst, IdBtnCopyRef, L"Copy Reference", ui_kit::ButtonKind::Secondary, font);
    btn_copy_value_ =
        ui_kit::create_button(parent, inst, IdBtnCopyValue, L"Copy Value…", ui_kit::ButtonKind::Secondary, font);
    btn_edit_desc_ =
        ui_kit::create_button(parent, inst, IdBtnEditDesc, L"Edit description", ui_kit::ButtonKind::Secondary, font);
    btn_delete_ = ui_kit::create_button(parent, inst, IdBtnDelete, L"Delete", ui_kit::ButtonKind::Danger, font);

    add_name_ = ui_kit::create_text_field(parent, inst, IdAddName, font);
    add_value_ = ui_kit::create_text_area(parent, inst, IdAddValue, font);
    add_desc_ = ui_kit::create_text_field(parent, inst, IdAddDesc, font);
    add_name_label_ = ui_kit::create_static(parent, inst, IdAddNameLabel, L"Key name",
                                            font_small ? font_small : font, true);
    add_value_label_ = ui_kit::create_static(parent, inst, IdAddValueLabel, L"Secret value",
                                             font_small ? font_small : font, true);
    add_desc_label_ = ui_kit::create_static(parent, inst, IdAddDescLabel,
                                            L"Description (optional)",
                                            font_small ? font_small : font, true);
    edit_desc_ = ui_kit::create_text_field(parent, inst, IdEditDescField, font);

    // These screens stack identical-looking boxes with no labels; the only explanation was a
    // paragraph in message_. Cue banners say what each box is without a layout change.
    ui_kit::set_placeholder(pass_, L"Passphrase (8 characters minimum)");
    ui_kit::set_placeholder(pass_confirm_, L"Confirm passphrase");
    ui_kit::set_placeholder(unlock_pass_, L"Keyring passphrase");
    ui_kit::set_placeholder(add_name_, L"Key name, e.g. openai_api_key");
    ui_kit::set_placeholder(add_desc_, L"Description (optional)");
    ui_kit::set_placeholder(edit_desc_, L"Description");

    scope_global_ = ui_kit::create_radio(parent, inst, IdScopeGlobal, L"Global (all projects)", font, true);
    scope_project_ = ui_kit::create_radio(parent, inst, IdScopeProject, L"This project only", font, false);
    SendMessageW(scope_global_, BM_SETCHECK, BST_CHECKED, 0);

    recipes_ = ui_kit::create_static(parent, inst, IdRecipes, L"EXECUTION RECIPES\r\nRecipes — next iteration",
                                     font_small ? font_small : font, true);

    hide_all();
}

void KeyringUi::destroy() {
    lock_now();
    const HWND ctrls[] = {title_,         subtitle_,      message_,        warning_,
                          strength_,      pass_,          pass_confirm_,   unlock_pass_,
                          btn_primary_,   btn_secondary_, btn_lock_,       btn_add_,
                          list_,          btn_copy_ref_,  btn_copy_value_, btn_edit_desc_,
                          btn_delete_,    add_name_label_, add_name_,      add_value_label_, add_value_,
                          add_desc_label_, add_desc_,
                          edit_desc_,     recipes_,       scope_global_,   scope_project_};
    for (HWND h : ctrls) {
        if (h && IsWindow(h)) {
            DestroyWindow(h);
        }
    }
    title_ = subtitle_ = message_ = warning_ = strength_ = nullptr;
    pass_ = pass_confirm_ = unlock_pass_ = nullptr;
    btn_primary_ = btn_secondary_ = btn_lock_ = btn_add_ = nullptr;
    list_ = btn_copy_ref_ = btn_copy_value_ = btn_edit_desc_ = btn_delete_ = nullptr;
    add_name_label_ = add_value_label_ = add_desc_label_ = nullptr;
    add_name_ = add_value_ = add_desc_ = edit_desc_ = recipes_ = nullptr;
    scope_global_ = scope_project_ = nullptr;
    parent_ = nullptr;
}

void KeyringUi::set_fonts(HFONT font, HFONT font_small, HFONT font_semi) {
    font_ = font;
    font_small_ = font_small;
    font_semi_ = font_semi;
    for (HWND h : {subtitle_, message_, pass_, pass_confirm_, unlock_pass_, btn_primary_,
                   btn_secondary_, btn_lock_, btn_add_, list_, btn_copy_ref_, btn_copy_value_,
                   btn_edit_desc_, btn_delete_, add_name_label_, add_name_, add_value_label_,
                   add_value_, add_desc_label_, add_desc_, edit_desc_,
                   scope_global_, scope_project_}) {
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
    if (title_ && font_semi_) {
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(font_semi_), TRUE);
    }
    for (HWND h : {warning_, strength_, recipes_, add_name_label_, add_value_label_, add_desc_label_}) {
        if (h && font_small_) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_small_), TRUE);
        }
    }
}

void KeyringUi::ensure_app_vault_bound() {
    const std::wstring vault = Keyring::default_app_vault_path();

    if (!Keyring::app_vault_exists()) {
        if (!(keyring_.is_unlocked() && keyring_.vault_bound() && keyring_.vault_path() == vault)) {
            if (!keyring_.is_unlocked()) {
                keyring_ = Keyring{};
            }
            if (screen_ != KeyringUiScreen::CreateWizard) {
                screen_ = KeyringUiScreen::NotCreated;
            }
        }
        refresh();
        return;
    }

    // Already unlocked on the app vault — do not reset or lock.
    if (keyring_.is_unlocked() && keyring_.vault_bound() && keyring_.vault_path() == vault) {
        if (screen_ != KeyringUiScreen::AddKey && screen_ != KeyringUiScreen::EditDescription &&
            screen_ != KeyringUiScreen::CreateWizard) {
            screen_ = KeyringUiScreen::Unlocked;
        }
        refresh();
        return;
    }

    if (!keyring_.vault_bound() || keyring_.vault_path() != vault) {
        keyring_ = Keyring{};
        if (keyring_.open_app() != KeyringStatus::Ok) {
            screen_ = KeyringUiScreen::NotCreated;
            set_msg(L"Could not open the Scylla Keyring vault file.", true);
            refresh();
            return;
        }
    }

    screen_ = keyring_.is_unlocked() ? KeyringUiScreen::Unlocked : KeyringUiScreen::Locked;
    refresh();
}

void KeyringUi::set_active_project(const std::string& project_id,
                                   const std::wstring& project_display_name) {
    if (project_id != project_id_) {
        migrate_prompted_ = false;
    }
    project_id_ = project_id;
    project_name_ = project_display_name;
    if (keyring_.is_unlocked()) {
        rebuild_list();
    }
    refresh();
}

void KeyringUi::show(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        clear_secret_edits();
        return;
    }
    apply_screen();
}

void KeyringUi::hide_all() {
    for (HWND h : {title_,         subtitle_,      message_,        warning_,
                   strength_,      pass_,          pass_confirm_,   unlock_pass_,
                   btn_primary_,   btn_secondary_, btn_lock_,       btn_add_,
                   list_,          btn_copy_ref_,  btn_copy_value_, btn_edit_desc_,
                   btn_delete_,    add_name_label_, add_name_,      add_value_label_, add_value_,
                   add_desc_label_, add_desc_,
                   edit_desc_,     recipes_,       scope_global_,   scope_project_}) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
}

void KeyringUi::wipe_edit(HWND edit) {
    if (!edit) {
        return;
    }
    const int len = GetWindowTextLengthW(edit);
    if (len > 0) {
        std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(edit, buf.data(), len + 1);
        SecureZeroMemory(buf.data(), buf.size() * sizeof(wchar_t));
    }
    SetWindowTextW(edit, L"");
}

void KeyringUi::clear_secret_edits() {
    wipe_edit(pass_);
    wipe_edit(pass_confirm_);
    wipe_edit(unlock_pass_);
    wipe_edit(add_value_);
}

std::wstring KeyringUi::edit_text(HWND edit) const {
    if (!edit) {
        return {};
    }
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    GetWindowTextW(edit, out.data(), len + 1);
    return out;
}

void KeyringUi::set_msg(const wchar_t* text, bool /*error*/) {
    if (message_) {
        SetWindowTextW(message_, text ? text : L"");
    }
}

int KeyringUi::passphrase_strength(const std::wstring& pass) const {
    if (pass.size() < static_cast<size_t>(kMinPassLen)) {
        return 0;
    }
    if (pass.size() < 12) {
        return 1;
    }
    if (pass.size() < 16) {
        return 2;
    }
    return 3;
}

void KeyringUi::rebuild_list() {
    if (!list_) {
        return;
    }
    // Preserve the caret by name, not index: after add/delete the indices shift, and resetting to
    // -1 left every per-key action ("Select a key first") dead until the user clicked again.
    const std::string keep = selected_secret_name();
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    if (!keyring_.is_unlocked()) {
        update_action_enabled();
        return;
    }
    const auto refs = keyring_.list_refs_for_ui(project_id_);
    if (refs.empty()) {
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No keys yet — choose Add Key to store your first secret."));
        // Unselectable placeholder: selected_secret_name() bounds-checks against refs, so it
        // reports no selection and the per-key actions stay disabled.
        update_action_enabled();
        return;
    }
    int restore = -1;
    for (const auto& ref : refs) {
        std::wstring line;
        if (ref.scope == SecretScope::Global) {
            line = L"[Global] ";
        } else {
            line = L"[Project] ";
        }
        if (ref.name == keep) {
            restore = static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
        }
        line += utf16(ref.name);
        if (!ref.description.empty()) {
            line += L"  —  ";
            line += utf16(ref.description);
        }
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SendMessageW(list_, LB_SETCURSEL, restore >= 0 ? restore : 0, 0);
    update_action_enabled();
}

void KeyringUi::update_action_enabled() {
    // Per-key buttons are meaningless without a selection; disable rather than answering a click
    // with a "Select a key first" modal.
    const bool has = !selected_secret_name().empty();
    for (HWND h : {btn_copy_ref_, btn_copy_value_, btn_edit_desc_, btn_delete_}) {
        if (h) {
            EnableWindow(h, has ? TRUE : FALSE);
        }
    }
}

std::string KeyringUi::selected_secret_name() const {
    if (!list_ || !keyring_.is_unlocked()) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (sel < 0) {
        return {};
    }
    const auto refs = keyring_.list_refs_for_ui(project_id_);
    if (sel >= static_cast<int>(refs.size())) {
        return {};
    }
    return refs[static_cast<size_t>(sel)].name;
}

void KeyringUi::refresh() {
    if (visible_) {
        apply_screen();
    }
}

void KeyringUi::apply_screen() {
    hide_all();
    if (!visible_) {
        return;
    }

    const auto m = ui_space::metrics_for(parent_ ? parent_ : GetDesktopWindow());
    (void)m;

    if (title_) {
        ShowWindow(title_, SW_SHOW);
        SetWindowTextW(title_, L"SCYLLA KEYRING");
    }
    if (subtitle_) {
        ShowWindow(subtitle_, SW_SHOW);
        if (project_name_.empty()) {
            SetWindowTextW(subtitle_,
                           project_id_.empty() ? L"App credential store (no active project filter)"
                                               : utf16(project_id_).c_str());
        } else {
            std::wstring sub = L"Active project filter: ";
            sub += project_name_;
            SetWindowTextW(subtitle_, sub.c_str());
        }
    }

    switch (screen_) {
        case KeyringUiScreen::NotCreated:
            set_msg(L"No Scylla Keyring exists yet.\r\n\r\n"
                    L"Create one to store credentials Scylla can use without exposing "
                    L"their values to the agent.");
            ShowWindow(message_, SW_SHOW);
            SetWindowTextW(btn_primary_, L"Create Scylla Keyring");
            ShowWindow(btn_primary_, SW_SHOW);
            break;

        case KeyringUiScreen::CreateWizard:
            set_msg(L"Choose a passphrase for Scylla's credential store.");
            ShowWindow(message_, SW_SHOW);
            SetWindowTextW(warning_,
                           L"Scylla cannot recover this passphrase.\r\n"
                           L"If it is lost, the Keyring cannot be unlocked.");
            ShowWindow(warning_, SW_SHOW);
            ShowWindow(pass_, SW_SHOW);
            ShowWindow(pass_confirm_, SW_SHOW);
            ShowWindow(strength_, SW_SHOW);
            SetWindowTextW(btn_primary_, L"Create Scylla Keyring");
            SetWindowTextW(btn_secondary_, L"Cancel");
            ShowWindow(btn_primary_, SW_SHOW);
            ShowWindow(btn_secondary_, SW_SHOW);
            ui_kit::set_tab_order({pass_, pass_confirm_, btn_primary_, btn_secondary_});
            focus_first(pass_);
            break;

        case KeyringUiScreen::Locked:
            set_msg(L"Locked\r\n\r\nEnter the passphrase to unlock Scylla's credential store.");
            ShowWindow(message_, SW_SHOW);
            ShowWindow(unlock_pass_, SW_SHOW);
            SetWindowTextW(btn_primary_, L"Unlock");
            ShowWindow(btn_primary_, SW_SHOW);
            ui_kit::set_tab_order({unlock_pass_, btn_primary_});
            focus_first(unlock_pass_);
            break;

        case KeyringUiScreen::Unlocked:
            set_msg(L"Unlocked  ·  Auto-lock: 15 min");
            ShowWindow(message_, SW_SHOW);
            ShowWindow(list_, SW_SHOW);
            ShowWindow(btn_add_, SW_SHOW);
            ShowWindow(btn_copy_ref_, SW_SHOW);
            ShowWindow(btn_copy_value_, SW_SHOW);
            ShowWindow(btn_edit_desc_, SW_SHOW);
            ShowWindow(btn_delete_, SW_SHOW);
            ShowWindow(btn_lock_, SW_SHOW);
            ShowWindow(recipes_, SW_SHOW);
            rebuild_list();
            ui_kit::set_tab_order({list_, btn_add_, btn_copy_ref_, btn_copy_value_, btn_edit_desc_, btn_delete_,
                                   btn_lock_});
            focus_first(list_);
            break;

        case KeyringUiScreen::AddKey:
            set_msg(L"ADD SECRET\r\nStores a protected value and returns only its scylla_… reference. "
                    L"Approved workflows can bind that reference directly.");
            ShowWindow(message_, SW_SHOW);
            ShowWindow(add_name_label_, SW_SHOW);
            ShowWindow(add_name_, SW_SHOW);
            ShowWindow(add_value_label_, SW_SHOW);
            ShowWindow(add_value_, SW_SHOW);
            ShowWindow(add_desc_label_, SW_SHOW);
            ShowWindow(add_desc_, SW_SHOW);
            ShowWindow(scope_global_, SW_SHOW);
            ShowWindow(scope_project_, SW_SHOW);
            SetWindowTextW(btn_primary_, L"Save Key");
            SetWindowTextW(btn_secondary_, L"Cancel");
            ShowWindow(btn_primary_, SW_SHOW);
            ShowWindow(btn_secondary_, SW_SHOW);
            // btn_primary_/btn_secondary_ are created before the add fields, so without this Tab
            // from the scope radios jumped backwards past the buttons.
            ui_kit::set_tab_order({add_name_, add_value_, add_desc_, scope_global_, scope_project_, btn_primary_,
                                   btn_secondary_});
            focus_first(add_name_);
            break;

        case KeyringUiScreen::EditDescription:
            set_msg(L"EDIT DESCRIPTION\r\nReference values stay hidden.");
            ShowWindow(message_, SW_SHOW);
            ShowWindow(edit_desc_, SW_SHOW);
            SetWindowTextW(btn_primary_, L"Save");
            SetWindowTextW(btn_secondary_, L"Cancel");
            ShowWindow(btn_primary_, SW_SHOW);
            ShowWindow(btn_secondary_, SW_SHOW);
            ui_kit::set_tab_order({edit_desc_, btn_primary_, btn_secondary_});
            focus_first(edit_desc_);
            break;
    }
}

void KeyringUi::focus_first(HWND control) {
    // Only steal focus if it is currently outside this panel: apply_screen() runs on refresh ticks
    // too, and yanking the caret out of whatever the user is typing in would be worse than no focus.
    if (!control || !IsWindow(control)) {
        return;
    }
    const HWND cur = GetFocus();
    if (cur && cur != control && owns_hwnd(cur)) {
        return;
    }
    SetFocus(control);
    if (control != list_) {
        SendMessageW(control, EM_SETSEL, 0, -1);
    }
}

void KeyringUi::layout(const RECT& area) {
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    const int x0 = col.left + m.pad_outer;
    const int x1 = col.right - m.pad_outer;
    const int w = (std::max)(1, x1 - x0);
    int y = col.top + m.pad_outer;

    auto place = [&](HWND h, int hh, int width = -1) {
        if (!h || !IsWindowVisible(h)) {
            return;
        }
        const int ww = width < 0 ? w : width;
        MoveWindow(h, x0, y, ww, hh, TRUE);
        y += hh + m.pad_row;
    };

    // Field / button widths below are DIP-scaled: the raw 80 / 360 / 400 literals they replace
    // stayed physically fixed, so at 150% DPI the buttons clipped their own labels.
    const int cta_w = m.btn_w + ui_space::dip(parent_, 80);
    const int field_w = (std::min)(w, ui_space::dip(parent_, 360));
    const int form_w = (std::min)(w, ui_space::dip(parent_, 400));
    const int sub_h = m.row_h - ui_space::dip(parent_, 4);

    place(title_, m.row_h);
    place(subtitle_, sub_h);
    y += m.pad_tight;

    switch (screen_) {
        case KeyringUiScreen::NotCreated:
            place(message_, m.row_h * 3);
            place(btn_primary_, m.btn_h, cta_w);
            break;

        case KeyringUiScreen::CreateWizard:
            place(message_, m.row_h * 2);
            place(warning_, m.row_h * 2);
            place(pass_, m.row_h, field_w);
            place(pass_confirm_, m.row_h, field_w);
            place(strength_, sub_h);
            {
                const int bw = cta_w;
                if (btn_primary_ && IsWindowVisible(btn_primary_)) {
                    MoveWindow(btn_primary_, x0, y, bw, m.btn_h, TRUE);
                }
                if (btn_secondary_ && IsWindowVisible(btn_secondary_)) {
                    MoveWindow(btn_secondary_, x0 + bw + m.pad_tight, y, m.btn_w, m.btn_h, TRUE);
                }
                y += m.btn_h + m.pad_section;
            }
            break;

        case KeyringUiScreen::Locked:
            place(message_, m.row_h * 3);
            place(unlock_pass_, m.row_h, field_w);
            place(btn_primary_, m.btn_h, m.btn_w);
            break;

        case KeyringUiScreen::Unlocked: {
            place(message_, m.row_h);
            // Anchor the action row and the recipes hint to the bottom, then give the list the
            // remainder. The old expression subtracted a guessed constant from area.bottom and
            // clamped at 6 rows, so on a short pane the buttons and recipes were pushed off-panel.
            const int recipes_h = m.row_h * 2;
            const int tail_h = m.btn_h + m.pad_section + recipes_h;
            const int list_h =
                (std::max)(m.row_h * 3, static_cast<int>(col.bottom) - m.pad_outer - y - m.pad_row - tail_h);
            if (list_ && IsWindowVisible(list_)) {
                MoveWindow(list_, x0, y, w, list_h, TRUE);
                y += list_h + m.pad_row;
            }
            {
                // Buttons wrap to a second row when the pane is too narrow, instead of running
                // past the right edge.
                int bx = x0;
                int by = y;
                auto place_btn = [&](HWND h, int bw) {
                    if (!h || !IsWindowVisible(h)) {
                        return;
                    }
                    if (bx > x0 && bx + bw > x1) {
                        bx = x0;
                        by += m.btn_h + m.pad_tight;
                    }
                    MoveWindow(h, bx, by, bw, m.btn_h, TRUE);
                    bx += bw + m.pad_tight;
                };
                const int wide = m.btn_w + ui_space::dip(parent_, 20);
                place_btn(btn_add_, m.btn_w);
                place_btn(btn_copy_ref_, wide);
                place_btn(btn_copy_value_, wide);
                place_btn(btn_edit_desc_, m.btn_w + ui_space::dip(parent_, 40));
                place_btn(btn_delete_, m.btn_w);
                place_btn(btn_lock_, m.btn_w);
                y = by + m.btn_h + m.pad_section;
            }
            place(recipes_, recipes_h);
            break;
        }

        case KeyringUiScreen::AddKey:
            place(message_, m.row_h * 2);
            place(add_name_label_, sub_h, form_w);
            place(add_name_, m.row_h, form_w);
            place(add_value_label_, sub_h, form_w);
            place(add_value_, m.row_h * 5, form_w);
            place(add_desc_label_, sub_h, form_w);
            place(add_desc_, m.row_h, form_w);
            place(scope_global_, m.row_h, form_w);
            place(scope_project_, m.row_h, form_w);
            {
                const int bw = m.btn_w;
                if (btn_primary_) {
                    MoveWindow(btn_primary_, x0, y, bw, m.btn_h, TRUE);
                }
                if (btn_secondary_) {
                    MoveWindow(btn_secondary_, x0 + bw + m.pad_tight, y, bw, m.btn_h, TRUE);
                }
            }
            break;

        case KeyringUiScreen::EditDescription:
            place(message_, m.row_h * 2);
            place(edit_desc_, m.row_h, form_w);
            {
                const int bw = m.btn_w;
                if (btn_primary_) {
                    MoveWindow(btn_primary_, x0, y, bw, m.btn_h, TRUE);
                }
                if (btn_secondary_) {
                    MoveWindow(btn_secondary_, x0 + bw + m.pad_tight, y, bw, m.btn_h, TRUE);
                }
            }
            break;
    }
}

bool KeyringUi::do_create(HWND parent) {
    std::wstring p1 = edit_text(pass_);
    std::wstring p2 = edit_text(pass_confirm_);
    if (p1.size() < static_cast<size_t>(kMinPassLen)) {
        secure_wipe_wstring(p1);
        secure_wipe_wstring(p2);
        MessageBoxW(parent, L"Passphrase must be at least 8 characters.", L"Scylla Keyring",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    if (p1 != p2) {
        secure_wipe_wstring(p1);
        secure_wipe_wstring(p2);
        MessageBoxW(parent, L"Passphrase and confirmation do not match.", L"Scylla Keyring",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    std::string pass_utf8 = utf8(p1);
    secure_wipe_wstring(p1);
    secure_wipe_wstring(p2);
    const KeyringStatus st = Keyring::create_app(pass_utf8);
    if (st != KeyringStatus::Ok) {
        if (!pass_utf8.empty()) {
            SecureZeroMemory(pass_utf8.data(), pass_utf8.size());
        }
        clear_secret_edits();
        MessageBoxW(parent, L"Could not create the Scylla Keyring vault.", L"Scylla Keyring",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    keyring_ = Keyring{};
    if (keyring_.open_app() != KeyringStatus::Ok || keyring_.unlock(pass_utf8) != KeyringStatus::Ok) {
        if (!pass_utf8.empty()) {
            SecureZeroMemory(pass_utf8.data(), pass_utf8.size());
        }
        clear_secret_edits();
        screen_ = KeyringUiScreen::Locked;
        apply_screen();
        return true;
    }
    if (!pass_utf8.empty()) {
        SecureZeroMemory(pass_utf8.data(), pass_utf8.size());
    }
    clear_secret_edits();
    keyring_.mark_activity();
    unlock_fails_ = 0;
    screen_ = KeyringUiScreen::Unlocked;
    apply_screen();
    maybe_prompt_migration(parent);
    return true;
}

bool KeyringUi::do_unlock(HWND parent) {
    std::wstring p = edit_text(unlock_pass_);
    if (p.empty()) {
        MessageBoxW(parent, L"Enter the Keyring passphrase.", L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (unlock_fails_ > 0) {
        const unsigned backoff = 400u * static_cast<unsigned>(unlock_fails_);
        const DWORD delay = backoff < 4000u ? backoff : 4000u;
        Sleep(delay);
    }
    std::string pass_utf8 = utf8(p);
    secure_wipe_wstring(p);
    const KeyringStatus st = keyring_.unlock(pass_utf8);
    if (!pass_utf8.empty()) {
        SecureZeroMemory(pass_utf8.data(), pass_utf8.size());
    }
    clear_secret_edits();
    if (st != KeyringStatus::Ok) {
        ++unlock_fails_;
        MessageBoxW(parent,
                    L"Unable to unlock Scylla Keyring.\r\n\r\nThe passphrase was not accepted.",
                    L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    unlock_fails_ = 0;
    keyring_.mark_activity();
    screen_ = KeyringUiScreen::Unlocked;
    apply_screen();
    maybe_prompt_migration(parent);
    return true;
}

bool KeyringUi::do_add_key(HWND parent) {
    std::wstring name_w = edit_text(add_name_);
    std::wstring value_w = edit_text(add_value_);
    std::wstring desc_w = edit_text(add_desc_);
    std::string name;
    if (!Keyring::normalize_secret_name(utf8(name_w), name)) {
        secure_wipe_wstring(value_w);
        MessageBoxW(parent,
                    L"Invalid key name.\r\nUse scylla_[A-Z0-9_]+ (or alphanumeric / _ / -).",
                    L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (value_w.empty()) {
        MessageBoxW(parent, L"Enter a secret value.", L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }

    const bool project_scope =
        scope_project_ && SendMessageW(scope_project_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SecretScope scope = SecretScope::Global;
    std::string scope_project_id;
    if (project_scope) {
        if (project_id_.empty() || !Keyring::is_valid_project_id(project_id_)) {
            secure_wipe_wstring(value_w);
            MessageBoxW(parent,
                        L"Project-scoped keys require an active project.\r\n"
                        L"Open a project first, or choose Global scope.",
                        L"Scylla Keyring", MB_OK | MB_ICONWARNING);
            return false;
        }
        scope = SecretScope::Project;
        scope_project_id = project_id_;
    }

    for (const auto& ref : keyring_.list_refs()) {
        if (ref.name == name) {
            secure_wipe_wstring(value_w);
            MessageBoxW(parent, L"That reference already exists. Delete it first or choose another name.",
                        L"Scylla Keyring", MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    std::string value = utf8(value_w);
    const std::string desc = utf8(desc_w);
    secure_wipe_wstring(value_w);
    const KeyringStatus st = keyring_.add_secret(name, value, desc, scope, scope_project_id);
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size());
    }
    clear_secret_edits();
    SetWindowTextW(add_name_, L"");
    SetWindowTextW(add_desc_, L"");
    if (st != KeyringStatus::Ok) {
        MessageBoxW(parent, L"Could not add the key.", L"Scylla Keyring", MB_OK | MB_ICONERROR);
        return false;
    }
    keyring_.mark_activity();
    screen_ = KeyringUiScreen::Unlocked;
    apply_screen();
    return true;
}

bool KeyringUi::do_edit_desc(HWND parent) {
    std::string name = editing_name_;
    if (name.empty()) {
        name = selected_secret_name();
    }
    if (name.empty()) {
        MessageBoxW(parent, L"Select a key first.", L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    const std::wstring desc_w = edit_text(edit_desc_);
    const KeyringStatus st = keyring_.update_description(name, utf8(desc_w));
    if (st != KeyringStatus::Ok) {
        MessageBoxW(parent, L"Could not update the description.", L"Scylla Keyring", MB_OK | MB_ICONERROR);
        return false;
    }
    editing_name_.clear();
    keyring_.mark_activity();
    screen_ = KeyringUiScreen::Unlocked;
    apply_screen();
    return true;
}

bool KeyringUi::do_delete(HWND parent) {
    const std::string name = selected_secret_name();
    if (name.empty()) {
        MessageBoxW(parent, L"Select a key to delete.", L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!ui_kit::confirm_destructive(parent, L"Delete key", utf16(name).c_str(),
                                     L"The stored value is erased from the vault and cannot be recovered. "
                                     L"Any recipe or environment variable using this reference stops resolving.")) {
        return false;
    }
    const KeyringStatus st = keyring_.remove_secret(name);
    if (st != KeyringStatus::Ok) {
        MessageBoxW(parent, L"Could not delete the key.", L"Scylla Keyring", MB_OK | MB_ICONERROR);
        return false;
    }
    keyring_.mark_activity();
    rebuild_list();
    return true;
}

bool KeyringUi::do_copy_ref(HWND parent) {
    const std::string name = selected_secret_name();
    if (name.empty()) {
        MessageBoxW(parent, L"Select a key to copy its reference.", L"Scylla Keyring",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    const std::wstring wide = utf16(name);
    if (!OpenClipboard(parent)) {
        return false;
    }
    EmptyClipboard();
    const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    std::memcpy(locked, wide.c_str(), bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    keyring_.mark_activity();
    set_msg(L"Reference copied to clipboard (name only — not the secret value).");
    return true;
}

bool KeyringUi::do_copy_value(HWND parent) {
    const std::string name = selected_secret_name();
    if (name.empty()) {
        MessageBoxW(parent, L"Select a key first.", L"Scylla Keyring", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring prompt = L"Copy the secret VALUE for\r\n\r\n";
    prompt += utf16(name);
    prompt += L"\r\n\r\nto the clipboard?\r\n\r\n"
              L"Anyone with access to this clipboard can read it.";
    if (MessageBoxW(parent, prompt.c_str(), L"Copy Value", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
        return false;
    }
    const KeyringStatus st = keyring_.copy_secret_value_to_clipboard(name);
    if (st != KeyringStatus::Ok) {
        MessageBoxW(parent, L"Could not copy the value.", L"Scylla Keyring", MB_OK | MB_ICONERROR);
        return false;
    }
    set_msg(L"Value copied (human action). Prefer Copy Reference when sharing with the agent.");
    return true;
}

bool KeyringUi::do_migrate(HWND parent) {
    if (!keyring_.is_unlocked()) {
        MessageBoxW(parent, L"Unlock the Scylla Keyring before migrating.", L"Scylla Keyring",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    if (project_id_.empty() || !Keyring::is_valid_project_id(project_id_)) {
        return false;
    }
    if (!Keyring::legacy_project_vault_exists(project_id_)) {
        return false;
    }

    const int upgrade = MessageBoxW(
        parent,
        L"KEYRING UPGRADE\r\n\r\n"
        L"Found an older project-specific Keyring for this project.\r\n\r\n"
        L"Click Yes then enter the old passphrase in the next dialog.",
        L"Scylla Keyring", MB_YESNO | MB_ICONINFORMATION);
    if (upgrade != IDYES) {
        return false;
    }

    set_msg(L"Type the legacy vault passphrase into the Unlock field below, then click OK.");
    if (unlock_pass_) {
        ShowWindow(unlock_pass_, SW_SHOW);
        SetFocus(unlock_pass_);
    }
    MessageBoxW(parent,
                L"Enter the old project Keyring passphrase in the Unlock field, "
                L"then click OK to migrate.",
                L"Scylla Keyring", MB_OK | MB_ICONINFORMATION);

    std::wstring p = edit_text(unlock_pass_);
    if (p.empty()) {
        wipe_edit(unlock_pass_);
        MessageBoxW(parent, L"Migration cancelled — no passphrase entered.", L"Scylla Keyring",
                    MB_OK | MB_ICONWARNING);
        if (visible_) {
            apply_screen();
        }
        return false;
    }

    std::string pass_utf8 = utf8(p);
    secure_wipe_wstring(p);
    const std::wstring legacy = Keyring::default_vault_path(project_id_);
    const KeyringStatus st = keyring_.migrate_from_project_vault(legacy, pass_utf8, project_id_);
    if (!pass_utf8.empty()) {
        SecureZeroMemory(pass_utf8.data(), pass_utf8.size());
    }
    wipe_edit(unlock_pass_);

    if (st != KeyringStatus::Ok) {
        MessageBoxW(parent,
                    L"Migration failed.\r\n\r\nCheck the legacy passphrase and try again from Access.",
                    L"Scylla Keyring", MB_OK | MB_ICONERROR);
        if (visible_) {
            apply_screen();
        }
        return false;
    }

    keyring_.mark_activity();
    rebuild_list();
    MessageBoxW(parent, L"Legacy project Keyring secrets were imported into Scylla Keyring.",
                L"Scylla Keyring", MB_OK | MB_ICONINFORMATION);
    if (visible_) {
        apply_screen();
    }
    return true;
}

void KeyringUi::maybe_prompt_migration(HWND parent) {
    if (!keyring_.is_unlocked()) {
        return;
    }
    if (project_id_.empty() || !Keyring::is_valid_project_id(project_id_)) {
        return;
    }
    if (!Keyring::legacy_project_vault_exists(project_id_)) {
        return;
    }
    if (migrate_prompted_) {
        return;
    }
    migrate_prompted_ = true;

    const int choice = MessageBoxW(
        parent,
        L"KEYRING UPGRADE\r\n\r\n"
        L"Found an older project-specific Keyring for this project.\r\n\r\n"
        L"Migrate its secrets into Scylla Keyring now?\r\n\r\n"
        L"Yes = Migrate   ·   No = Later",
        L"Scylla Keyring", MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES) {
        return;
    }
    do_migrate(parent);
}

bool KeyringUi::handle_command(int id, HWND parent_for_msg, WORD notify) {
    switch (id) {
        case IdList:
            // Selection moved: refresh enablement only. Returning false keeps the host from
            // running a full relayout + chrome refresh on every focus/selection notification.
            if (notify == LBN_SELCHANGE || notify == LBN_SETFOCUS) {
                update_action_enabled();
            }
            return false;

        case IdBtnPrimary:
            if (screen_ == KeyringUiScreen::NotCreated) {
                screen_ = KeyringUiScreen::CreateWizard;
                clear_secret_edits();
                apply_screen();
                return true;
            }
            if (screen_ == KeyringUiScreen::CreateWizard) {
                do_create(parent_for_msg);
                return true;
            }
            if (screen_ == KeyringUiScreen::Locked) {
                do_unlock(parent_for_msg);
                return true;
            }
            if (screen_ == KeyringUiScreen::AddKey) {
                do_add_key(parent_for_msg);
                return true;
            }
            if (screen_ == KeyringUiScreen::EditDescription) {
                do_edit_desc(parent_for_msg);
                return true;
            }
            return false;

        case IdBtnSecondary:
            if (screen_ == KeyringUiScreen::CreateWizard) {
                clear_secret_edits();
                screen_ = Keyring::app_vault_exists() ? KeyringUiScreen::Locked : KeyringUiScreen::NotCreated;
                if (Keyring::app_vault_exists() && !keyring_.vault_bound()) {
                    ensure_app_vault_bound();
                } else {
                    apply_screen();
                }
                return true;
            }
            if (screen_ == KeyringUiScreen::AddKey || screen_ == KeyringUiScreen::EditDescription) {
                clear_secret_edits();
                editing_name_.clear();
                screen_ = KeyringUiScreen::Unlocked;
                apply_screen();
                return true;
            }
            return false;

        case IdBtnLock:
            lock_now();
            screen_ = KeyringUiScreen::Locked;
            apply_screen();
            return true;

        case IdBtnAdd:
            if (keyring_.is_unlocked()) {
                clear_secret_edits();
                SetWindowTextW(add_name_, L"");
                SetWindowTextW(add_desc_, L"");
                if (scope_global_) {
                    SendMessageW(scope_global_, BM_SETCHECK, BST_CHECKED, 0);
                }
                if (scope_project_) {
                    SendMessageW(scope_project_, BM_SETCHECK, BST_UNCHECKED, 0);
                }
                screen_ = KeyringUiScreen::AddKey;
                apply_screen();
            }
            return true;

        case IdBtnCopyRef:
            do_copy_ref(parent_for_msg);
            return true;

        case IdBtnCopyValue:
            do_copy_value(parent_for_msg);
            return true;

        case IdBtnEditDesc: {
            const std::string name = selected_secret_name();
            if (name.empty()) {
                MessageBoxW(parent_for_msg, L"Select a key first.", L"Scylla Keyring",
                            MB_OK | MB_ICONWARNING);
                return true;
            }
            editing_name_ = name;
            std::wstring cur;
            for (const auto& ref : keyring_.list_refs_for_ui(project_id_)) {
                if (ref.name == name) {
                    cur = utf16(ref.description);
                    break;
                }
            }
            SetWindowTextW(edit_desc_, cur.c_str());
            screen_ = KeyringUiScreen::EditDescription;
            apply_screen();
            return true;
        }

        case IdBtnDelete:
            do_delete(parent_for_msg);
            return true;

        default:
            return false;
    }
}

void KeyringUi::tick_autolock(std::uint64_t idle_ms) {
    if (keyring_.should_autolock(idle_ms)) {
        lock_now();
        if (visible_ && (screen_ == KeyringUiScreen::Unlocked || screen_ == KeyringUiScreen::AddKey ||
                         screen_ == KeyringUiScreen::EditDescription)) {
            screen_ = KeyringUiScreen::Locked;
            apply_screen();
        } else if (keyring_.vault_bound() && !keyring_.is_unlocked() &&
                   screen_ == KeyringUiScreen::Unlocked) {
            screen_ = KeyringUiScreen::Locked;
        }
    }
}

void KeyringUi::lock_now() {
    clear_secret_edits();
    keyring_.lock();
    unlock_fails_ = 0;
}

void KeyringUi::on_project_switching(const std::string& new_project_id,
                                     const std::wstring& new_project_display_name) {
    set_active_project(new_project_id, new_project_display_name);
}

std::wstring KeyringUi::status_label() const {
    if (!Keyring::app_vault_exists() && !keyring_.vault_bound()) {
        return L"None";
    }
    if (keyring_.is_unlocked()) {
        return L"Unlocked";
    }
    return L"Locked";
}

bool KeyringUi::owns_hwnd(HWND child) const {
    if (!child) {
        return false;
    }
    for (HWND h : {title_,         subtitle_,      message_,        warning_,
                   strength_,      pass_,          pass_confirm_,   unlock_pass_,
                   btn_primary_,   btn_secondary_, btn_lock_,       btn_add_,
                   list_,          btn_copy_ref_,  btn_copy_value_, btn_edit_desc_,
                   btn_delete_,    add_name_label_, add_name_,      add_value_label_, add_value_,
                   add_desc_label_, add_desc_,
                   edit_desc_,     recipes_,       scope_global_,   scope_project_}) {
        if (h == child) {
            return true;
        }
    }
    return false;
}

}  // namespace scyllagpt
