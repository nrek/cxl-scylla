#pragma once

// Settings → Security → Connections.
//
// CRUD for the saved project connections the brokered `/scylla-query` skill runs against. Every
// credential field on this page stores a Keyring *name*, never a value. Private-key file import is
// the sole exception to the metadata-only UI path: it copies the selected file directly into the
// Keyring, wipes the transient buffer, and leaves only the generated reference on the connection.
//
// Connections are project-scoped and addressed by alias, because that alias is the only thing the
// agent is allowed to name when it calls the query tool.

#include "scyllagpt/commands.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/project_connection.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class ConnUiRequest {
    None = 0,
    OpenKeyring,
    // Run a live SELECT 1 through the broker. The page cannot do this itself: it would block the UI
    // thread for the length of an SSH round trip, so the window owns the prepare/finish split.
    TestConnection,
};

class ConnectionsSettingsUi {
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
    void reload(ProjectConnectionManager& mgr, Keyring& keyring, const std::string& project_id,
                const std::wstring& project_name);

    bool on_command(WORD id, WORD notify, HWND owner, ProjectConnectionManager& mgr, Keyring& keyring,
                    const std::string& project_id, const std::wstring& persist_path);

    bool owns_hwnd(HWND child) const;
    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

    UINT default_command(HWND field) const;
    UINT cancel_command() const;

    ConnUiRequest take_request() {
        const ConnUiRequest r = request_;
        request_ = ConnUiRequest::None;
        return r;
    }

    // Alias the window should test when it sees ConnUiRequest::TestConnection.
    const std::string& pending_test_alias() const { return pending_test_alias_; }
    // Result of that test, rendered into the inline status line.
    void report_test_result(bool ok, const std::wstring& message);

    void hide_all();

private:
    enum class Mode { Landing, Route, Credentials, Policy };

    // One labelled form line. `extra` is the optional Keyring-reference select that sits beside a
    // literal field, so a host or port can live in the vault instead of the connections file.
    struct FormRow {
        HWND label = nullptr;
        HWND field = nullptr;
        HWND extra = nullptr;
        int height_rows = 1;
    };

    void set_mode(Mode mode);
    void apply_visibility();
    std::vector<HWND> all_controls() const;
    std::vector<HWND> section_controls(Mode mode) const;
    const std::vector<FormRow>& rows_for(Mode mode) const;

    void fill_landing(ProjectConnectionManager& mgr, Keyring& keyring);
    // Refreshes the Keyring name list backing every reference select. Only called when the form is
    // opened: rebuilding a select's items resets its selection, so doing this mid-edit would discard
    // unsaved choices.
    void refresh_secret_names(Keyring& keyring);
    void fill_ref_select(HWND select, const std::string& current);
    void load_form(const ProjectConnection& connection);
    // Reads every control into `out`. Returns false with `error` set when the form cannot make a
    // valid connection, so nothing is written that `load()` would later drop on the floor.
    bool read_form(const std::string& project_id, ProjectConnection& out, std::wstring& error) const;
    std::string selected_alias() const;
    void set_status(const std::wstring& text, bool error);
    // Keyring name currently chosen in `select`, preserving a reference whose secret is absent.
    std::string ref_name_of(HWND select) const;

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    bool visible_ = false;
    Mode mode_ = Mode::Landing;
    ConnUiRequest request_ = ConnUiRequest::None;

    std::string project_id_;
    std::wstring project_name_;
    std::string editing_id_;     // empty = adding
    std::string editing_alias_;  // alias as loaded, to detect renames
    std::string pending_test_alias_;
    // Control to focus once the next layout pass finishes. window.cpp hides every settings panel on
    // each layout, and hiding the focused control drops focus to the frame, so focus has to be set
    // after that cycle rather than when the form opens. Consumed once, never re-asserted, so it
    // cannot steal focus from wherever the user moved it afterwards.
    HWND pending_focus_ = nullptr;

    // Landing
    HWND title_ = nullptr;
    HWND desc_ = nullptr;
    HWND project_lbl_ = nullptr;
    HWND list_ = nullptr;
    HWND btn_add_ = nullptr;
    HWND btn_manage_ = nullptr;
    HWND btn_dup_ = nullptr;
    HWND btn_delete_ = nullptr;
    HWND btn_toggle_ = nullptr;

    // Detail chrome, shared by all three sections
    HWND btn_back_ = nullptr;
    HWND detail_title_ = nullptr;
    HWND status_ = nullptr;
    HWND tab_route_ = nullptr;
    HWND tab_creds_ = nullptr;
    HWND tab_policy_ = nullptr;
    HWND btn_save_ = nullptr;
    HWND btn_cancel_ = nullptr;
    HWND btn_test_ = nullptr;
    HWND btn_unlock_ = nullptr;

    // Route
    HWND name_ = nullptr;
    HWND alias_ = nullptr;
    HWND enabled_ = nullptr;
    HWND route_ = nullptr;
    HWND ssh_host_ = nullptr;
    HWND ssh_host_ref_ = nullptr;
    HWND ssh_port_ = nullptr;
    HWND ssh_port_ref_ = nullptr;
    HWND ssh_user_ = nullptr;
    HWND ssh_user_ref_ = nullptr;

    // Credentials
    HWND key_ref_ = nullptr;
    HWND btn_key_import_ = nullptr;
    HWND passphrase_ref_ = nullptr;
    HWND auth_ref_ = nullptr;
    HWND host_key_ = nullptr;
    HWND engine_ = nullptr;
    HWND db_host_ = nullptr;
    HWND db_host_ref_ = nullptr;
    HWND db_port_ = nullptr;
    HWND db_port_ref_ = nullptr;
    HWND db_name_ = nullptr;
    HWND db_user_ref_ = nullptr;
    HWND db_pass_ref_ = nullptr;
    HWND tls_ref_ = nullptr;

    // Policy
    HWND pol_read_ = nullptr;
    HWND pol_data_ = nullptr;
    HWND pol_schema_ = nullptr;
    HWND pol_admin_ = nullptr;
    HWND pol_unrestricted_ = nullptr;
    HWND pol_multi_ = nullptr;
    HWND res_visibility_ = nullptr;
    HWND res_max_rows_ = nullptr;
    HWND res_max_bytes_ = nullptr;
    HWND res_timeout_ = nullptr;
    HWND res_max_text_ = nullptr;
    HWND res_binary_ = nullptr;

    bool keyring_locked_ = true;
    bool vault_missing_ = false;

    std::vector<FormRow> route_rows_;
    std::vector<FormRow> cred_rows_;
    std::vector<FormRow> policy_rows_;
    std::vector<HWND> ref_selects_;
    // Per-select name shown as "missing" because its secret is absent from the Keyring. Kept so
    // saving an unrelated field does not silently clear a reference that simply cannot be listed.
    std::vector<std::string> ref_missing_;

    std::vector<std::string> landing_aliases_;
    std::vector<std::wstring> landing_primary_;
    std::vector<std::wstring> landing_secondary_;
    std::vector<std::string> secret_names_;
    RECT area_{};
};

}  // namespace scyllagpt
