#pragma once

// Scylla Keyring center-pane UI — app-level vault (Phase 1).
// Never logs or surfaces secret values to the agent layer.

#include "scyllagpt/keyring.h"

#include <cstdint>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class KeyringUiScreen {
    NotCreated,
    CreateWizard,
    Locked,
    Unlocked,
    AddKey,
    EditDescription,
};

class KeyringUi {
public:
    KeyringUi() = default;
    ~KeyringUi();

    KeyringUi(const KeyringUi&) = delete;
    KeyringUi& operator=(const KeyringUi&) = delete;

    void create(HWND parent, HFONT font, HFONT font_small, HFONT font_semi);
    void destroy();
    void set_fonts(HFONT font, HFONT font_small, HFONT font_semi);

    // Active project for filtering project-scoped secrets (does not lock/unlock vault).
    void set_active_project(const std::string& project_id, const std::wstring& project_display_name);

    void show(bool visible);
    void layout(const RECT& area);
    void refresh();

    // Bind / reopen app vault file if present (stays locked unless already unlocked).
    void ensure_app_vault_bound();

    // |notify| is the WM_COMMAND high word; needed so list selection changes can update button
    // enablement without being mistaken for a button press.
    bool handle_command(int id, HWND parent_for_msg, WORD notify = 0);

    void tick_autolock(std::uint64_t idle_ms = 15ull * 60ull * 1000ull);
    void lock_now();
    // Project switch: refresh filter only — do NOT lock the app Keyring.
    void on_project_switching(const std::string& new_project_id,
                              const std::wstring& new_project_display_name);

    // Offer Migrate/Later if a legacy per-project vault exists for the active project.
    void maybe_prompt_migration(HWND parent);

    Keyring& keyring() { return keyring_; }
    const Keyring& keyring() const { return keyring_; }
    KeyringUiScreen screen() const { return screen_; }
    const std::string& project_id() const { return project_id_; }

    // "None" | "Locked" | "Unlocked"
    std::wstring status_label() const;

    bool owns_hwnd(HWND child) const;

    // Enter / Esc contract. Enter confirms the screen's primary action (Unlock / Create / Save);
    // Esc backs out of the screens that have a Cancel.
    UINT default_command(HWND /*field*/) const {
        switch (screen_) {
            case KeyringUiScreen::NotCreated:
            case KeyringUiScreen::CreateWizard:
            case KeyringUiScreen::Locked:
            case KeyringUiScreen::AddKey:
            case KeyringUiScreen::EditDescription:
                return IdBtnPrimary;
            default:
                return 0;
        }
    }
    UINT cancel_command() const {
        switch (screen_) {
            case KeyringUiScreen::CreateWizard:
            case KeyringUiScreen::AddKey:
            case KeyringUiScreen::EditDescription:
                return IdBtnSecondary;
            default:
                return 0;
        }
    }

private:
    enum CtrlId : int {
        IdTitle = 2701,
        IdSubtitle,
        IdMessage,
        IdWarning,
        IdPassphrase,
        IdPassConfirm,
        IdUnlockPass,
        IdBtnPrimary,
        IdBtnSecondary,
        IdBtnLock,
        IdBtnAdd,
        IdList,
        IdBtnCopyRef,
        IdBtnCopyValue,
        IdBtnEditDesc,
        IdBtnDelete,
        IdAddName,
        IdAddValue,
        IdAddDesc,
        IdEditDescField,
        IdRecipes,
        IdStrength,
        IdScopeGlobal,
        IdScopeProject,
        IdMigratePass,
    };

    void hide_all();
    void apply_screen();
    void clear_secret_edits();
    void wipe_edit(HWND edit);
    std::wstring edit_text(HWND edit) const;
    void set_msg(const wchar_t* text, bool error = false);
    void rebuild_list();
    void update_action_enabled();
    void focus_first(HWND control);
    std::string selected_secret_name() const;
    int passphrase_strength(const std::wstring& pass) const;

    bool do_create(HWND parent);
    bool do_unlock(HWND parent);
    bool do_add_key(HWND parent);
    bool do_edit_desc(HWND parent);
    bool do_delete(HWND parent);
    bool do_copy_ref(HWND parent);
    bool do_copy_value(HWND parent);
    bool do_migrate(HWND parent);

    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    HFONT font_semi_ = nullptr;

    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND message_ = nullptr;
    HWND warning_ = nullptr;
    HWND strength_ = nullptr;
    HWND pass_ = nullptr;
    HWND pass_confirm_ = nullptr;
    HWND unlock_pass_ = nullptr;
    HWND btn_primary_ = nullptr;
    HWND btn_secondary_ = nullptr;
    HWND btn_lock_ = nullptr;
    HWND btn_add_ = nullptr;
    HWND list_ = nullptr;
    HWND btn_copy_ref_ = nullptr;
    HWND btn_copy_value_ = nullptr;
    HWND btn_edit_desc_ = nullptr;
    HWND btn_delete_ = nullptr;
    HWND add_name_ = nullptr;
    HWND add_value_ = nullptr;
    HWND add_desc_ = nullptr;
    HWND edit_desc_ = nullptr;
    HWND recipes_ = nullptr;
    HWND scope_global_ = nullptr;
    HWND scope_project_ = nullptr;

    Keyring keyring_;
    std::string project_id_;
    std::wstring project_name_;
    KeyringUiScreen screen_ = KeyringUiScreen::NotCreated;
    int unlock_fails_ = 0;
    bool visible_ = false;
    std::string editing_name_;
    bool migrate_prompted_ = false;
};

}  // namespace scyllagpt
