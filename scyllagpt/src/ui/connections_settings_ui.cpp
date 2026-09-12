#include "scyllagpt/connections_settings_ui.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <vector>

#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

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

std::wstring trimmed(std::wstring s) {
    const auto begin = s.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(L" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

bool checked(HWND h) {
    return h && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void set_checked(HWND h, bool on) {
    if (h) {
        SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void set_number(HWND h, unsigned value) {
    if (h) {
        SetWindowTextW(h, std::to_wstring(value).c_str());
    }
}

// Returns false when the text is not a plain non-negative integer, so a typo cannot silently become
// a zero limit (which validate() would then reject with a less specific message).
bool read_number(HWND h, std::uint32_t& out) {
    const std::wstring text = trimmed(get_text(h));
    if (text.empty()) {
        return false;
    }
    unsigned long long value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned long long>(c - L'0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool read_private_key_file(const std::wstring& path, std::string& out, std::wstring& error) {
    constexpr DWORD kMaxPrivateKeyBytes = 1024u * 1024u;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"The selected private-key file could not be opened.";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxPrivateKeyBytes) {
        CloseHandle(file);
        error = L"The selected file is empty or larger than 1 MB.";
        return false;
    }
    out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != out.size()) {
        if (!out.empty()) SecureZeroMemory(out.data(), out.size());
        out.clear();
        error = L"The selected private-key file could not be read completely.";
        return false;
    }
    if (out.rfind("-----BEGIN ", 0) != 0 || out.find("PRIVATE KEY-----") == std::string::npos ||
        out.find("-----END ") == std::string::npos) {
        SecureZeroMemory(out.data(), out.size());
        out.clear();
        error = L"The selected file does not look like an OpenSSH or PEM private key.";
        return false;
    }
    return true;
}

std::string imported_key_name(HWND alias) {
    std::string stem = utf8(trimmed(get_text(alias)));
    if (stem.empty()) stem = "imported";
    for (char& ch : stem) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '-' && ch != '_') ch = '-';
    }
    return "scylla_" + stem + "-ssh-private-key";
}

void fill_authority(HWND select, ConnectionAuthority current) {
    if (!select) {
        return;
    }
    ui_kit::select_set_items(select, {{L"Auto — run without asking", 0},
                                     {L"Ask — require approval", 1},
                                     {L"Block — refuse", 2}});
    ui_kit::select_set_index(select, static_cast<int>(current));
}

ConnectionAuthority authority_from_select(HWND select, ConnectionAuthority fallback) {
    switch (ui_kit::select_get_index(select)) {
        case 0: return ConnectionAuthority::Auto;
        case 1: return ConnectionAuthority::Ask;
        case 2: return ConnectionAuthority::Block;
        default: return fallback;
    }
}

}  // namespace

bool ConnectionsSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = inst ? inst : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    font_small_ = font_small ? font_small : font;

    const auto label = [&](const wchar_t* text) {
        return ui_kit::create_static(parent_, inst_, 0, text, font_small_, false);
    };
    const auto field = [&](UINT id) { return ui_kit::create_text_field(parent_, inst_, id, font_); };
    const auto select = [&](UINT id) { return ui_kit::create_select(parent_, inst_, id, font_); };

    title_ = ui_kit::create_static(parent_, inst_, Cmd_ConnTitle, L"Connections", font_, false);
    desc_ = ui_kit::create_static(
        parent_, inst_, Cmd_ConnDesc,
        L"Saved database routes the agent may query by alias. Credentials stay in the Keyring — this "
        L"page stores only reference names, never values.",
        font_small_, true);
    project_lbl_ = ui_kit::create_static(parent_, inst_, Cmd_ConnProject, L"", font_small_, true);
    list_ = ui_kit::create_entity_list(parent_, inst_, Cmd_ConnList, font_);
    btn_add_ = ui_kit::create_button(parent_, inst_, Cmd_ConnAdd, L"+ Add Connection",
                                     ui_kit::ButtonKind::Primary, font_);
    btn_manage_ = ui_kit::create_button(parent_, inst_, Cmd_ConnManage, L"Manage",
                                        ui_kit::ButtonKind::Secondary, font_);
    btn_dup_ = ui_kit::create_button(parent_, inst_, Cmd_ConnDup, L"Duplicate",
                                     ui_kit::ButtonKind::Secondary, font_);
    btn_toggle_ = ui_kit::create_button(parent_, inst_, Cmd_ConnToggleEnabled, L"Disable",
                                        ui_kit::ButtonKind::Secondary, font_);
    btn_delete_ = ui_kit::create_button(parent_, inst_, Cmd_ConnDelete, L"Delete",
                                        ui_kit::ButtonKind::Danger, font_);

    btn_back_ = ui_kit::create_button(parent_, inst_, Cmd_ConnBack, L"‹ Connections",
                                      ui_kit::ButtonKind::Ghost, font_);
    detail_title_ = ui_kit::create_static(parent_, inst_, Cmd_ConnDetailTitle, L"", font_, false);
    status_ = ui_kit::create_static(parent_, inst_, Cmd_ConnStatus, L"", font_small_, true);
    tab_route_ = ui_kit::create_button(parent_, inst_, Cmd_ConnSecRoute, L"Route",
                                       ui_kit::ButtonKind::Secondary, font_);
    tab_creds_ = ui_kit::create_button(parent_, inst_, Cmd_ConnSecCreds, L"Credentials",
                                       ui_kit::ButtonKind::Secondary, font_);
    tab_policy_ = ui_kit::create_button(parent_, inst_, Cmd_ConnSecPolicy, L"Policy & Limits",
                                        ui_kit::ButtonKind::Secondary, font_);
    btn_save_ = ui_kit::create_button(parent_, inst_, Cmd_ConnSave, L"Save Connection",
                                      ui_kit::ButtonKind::Primary, font_);
    btn_cancel_ = ui_kit::create_button(parent_, inst_, Cmd_ConnCancel, L"Cancel",
                                        ui_kit::ButtonKind::Secondary, font_);
    btn_test_ = ui_kit::create_button(parent_, inst_, Cmd_ConnTest, L"Test Connection",
                                      ui_kit::ButtonKind::Secondary, font_);
    btn_unlock_ = ui_kit::create_button(parent_, inst_, Cmd_ConnUnlock, L"Unlock Keyring",
                                        ui_kit::ButtonKind::Secondary, font_);

    // Route
    name_ = field(Cmd_ConnName);
    alias_ = field(Cmd_ConnAlias);
    enabled_ = ui_kit::create_checkbox(parent_, inst_, Cmd_ConnEnabled, L"Available to the agent", font_);
    route_ = select(Cmd_ConnRoute);
    ssh_host_ = field(Cmd_ConnSshHost);
    ssh_host_ref_ = select(Cmd_ConnSshHostRef);
    ssh_port_ = field(Cmd_ConnSshPort);
    ssh_port_ref_ = select(Cmd_ConnSshPortRef);
    ssh_user_ = field(Cmd_ConnSshUser);
    ssh_user_ref_ = select(Cmd_ConnSshUserRef);
    ui_kit::set_placeholder(name_, L"Synq HOT reader");
    ui_kit::set_placeholder(alias_, L"synq-hot-ro");
    ui_kit::set_placeholder(ssh_host_, L"bastion.example.com");
    ui_kit::set_placeholder(ssh_port_, L"22");
    ui_kit::set_placeholder(ssh_user_, L"ubuntu");

    route_rows_ = {
        {label(L"Display name"), name_, nullptr, 1},
        {label(L"Alias the agent uses"), alias_, nullptr, 1},
        {label(L""), enabled_, nullptr, 1},
        {label(L"Route"), route_, nullptr, 1},
        {label(L"SSH host"), ssh_host_, ssh_host_ref_, 1},
        {label(L"SSH port"), ssh_port_, ssh_port_ref_, 1},
        {label(L"SSH username"), ssh_user_, ssh_user_ref_, 1},
    };

    // Credentials
    key_ref_ = select(Cmd_ConnKeyRef);
    btn_key_import_ = ui_kit::create_button(parent_, inst_, Cmd_ConnKeyImport, L"Import from file…",
                                            ui_kit::ButtonKind::Secondary, font_);
    passphrase_ref_ = select(Cmd_ConnPassphraseRef);
    auth_ref_ = select(Cmd_ConnAuthRef);
    host_key_ = field(Cmd_ConnHostKey);
    engine_ = select(Cmd_ConnEngine);
    db_host_ = field(Cmd_ConnDbHost);
    db_host_ref_ = select(Cmd_ConnDbHostRef);
    db_port_ = field(Cmd_ConnDbPort);
    db_port_ref_ = select(Cmd_ConnDbPortRef);
    db_name_ = field(Cmd_ConnDbName);
    db_user_ref_ = select(Cmd_ConnDbUserRef);
    db_pass_ref_ = select(Cmd_ConnDbPassRef);
    tls_ref_ = select(Cmd_ConnTlsRef);
    ui_kit::set_placeholder(host_key_, L"ssh-ed25519 AAAAC3Nza…  (run: ssh-keyscan -t ed25519 <host>)");
    ui_kit::set_placeholder(db_host_, L"cluster-ro.rds.amazonaws.com");
    ui_kit::set_placeholder(db_port_, L"3306");
    ui_kit::set_placeholder(db_name_, L"optional default schema");

    cred_rows_ = {
        {label(L"SSH private key"), key_ref_, btn_key_import_, 1},
        {label(L"Key passphrase"), passphrase_ref_, nullptr, 1},
        {label(L"SSH password"), auth_ref_, nullptr, 1},
        {label(L"Pinned host key"), host_key_, nullptr, 1},
        {label(L"Engine"), engine_, nullptr, 1},
        {label(L"Database host"), db_host_, db_host_ref_, 1},
        {label(L"Database port"), db_port_, db_port_ref_, 1},
        {label(L"Default schema"), db_name_, nullptr, 1},
        {label(L"Database username"), db_user_ref_, nullptr, 1},
        {label(L"Database password"), db_pass_ref_, nullptr, 1},
        {label(L"TLS CA"), tls_ref_, nullptr, 1},
    };

    // Policy
    pol_read_ = select(Cmd_ConnPolRead);
    pol_data_ = select(Cmd_ConnPolData);
    pol_schema_ = select(Cmd_ConnPolSchema);
    pol_admin_ = select(Cmd_ConnPolAdmin);
    pol_unrestricted_ = ui_kit::create_checkbox(parent_, inst_, Cmd_ConnPolUnrestricted,
                                                L"Unrestricted — skip SQL classification entirely", font_);
    pol_multi_ = ui_kit::create_checkbox(parent_, inst_, Cmd_ConnPolMultiStatement,
                                         L"Allow multiple statements per call", font_);
    res_visibility_ = select(Cmd_ConnResVisibility);
    res_max_rows_ = field(Cmd_ConnResMaxRows);
    res_max_bytes_ = field(Cmd_ConnResMaxBytes);
    res_timeout_ = field(Cmd_ConnResTimeout);
    res_max_text_ = field(Cmd_ConnResMaxText);
    res_binary_ = ui_kit::create_checkbox(parent_, inst_, Cmd_ConnResBinary, L"Include binary columns", font_);

    policy_rows_ = {
        {label(L"Reads"), pol_read_, nullptr, 1},
        {label(L"Data changes"), pol_data_, nullptr, 1},
        {label(L"Schema changes"), pol_schema_, nullptr, 1},
        {label(L"Administrative"), pol_admin_, nullptr, 1},
        {label(L""), pol_unrestricted_, nullptr, 1},
        {label(L""), pol_multi_, nullptr, 1},
        {label(L"Result visibility"), res_visibility_, nullptr, 1},
        {label(L"Max rows"), res_max_rows_, nullptr, 1},
        {label(L"Max bytes"), res_max_bytes_, nullptr, 1},
        {label(L"Timeout (seconds)"), res_timeout_, nullptr, 1},
        {label(L"Max text bytes"), res_max_text_, nullptr, 1},
        {label(L""), res_binary_, nullptr, 1},
    };

    ref_selects_ = {ssh_host_ref_, ssh_port_ref_, ssh_user_ref_, key_ref_,     passphrase_ref_,
                    auth_ref_,     db_host_ref_,  db_port_ref_,  db_user_ref_, db_pass_ref_,
                    tls_ref_};

    ui_kit::style_scroll_host(list_, theme().panel);
    mode_ = Mode::Landing;
    hide_all();
    return true;
}

void ConnectionsSettingsUi::destroy() {
    hide_all();
    for (HWND h : all_controls()) {
        if (h) {
            DestroyWindow(h);
        }
    }
    parent_ = nullptr;
    visible_ = false;
    route_rows_.clear();
    cred_rows_.clear();
    policy_rows_.clear();
    ref_selects_.clear();
    landing_aliases_.clear();
}

const std::vector<ConnectionsSettingsUi::FormRow>& ConnectionsSettingsUi::rows_for(Mode mode) const {
    switch (mode) {
        case Mode::Credentials: return cred_rows_;
        case Mode::Policy: return policy_rows_;
        default: return route_rows_;
    }
}

std::vector<HWND> ConnectionsSettingsUi::section_controls(Mode mode) const {
    std::vector<HWND> out;
    for (const auto& row : rows_for(mode)) {
        out.push_back(row.label);
        out.push_back(row.field);
        if (row.extra) {
            out.push_back(row.extra);
        }
    }
    return out;
}

std::vector<HWND> ConnectionsSettingsUi::all_controls() const {
    std::vector<HWND> out{title_,     desc_,      project_lbl_, list_,       btn_add_,   btn_manage_,
                          btn_dup_,   btn_delete_, btn_toggle_,  btn_back_,   detail_title_,
                          status_,    tab_route_, tab_creds_,   tab_policy_, btn_save_,  btn_cancel_,
                          btn_test_,  btn_unlock_};
    for (Mode mode : {Mode::Route, Mode::Credentials, Mode::Policy}) {
        for (HWND h : section_controls(mode)) {
            out.push_back(h);
        }
    }
    return out;
}

void ConnectionsSettingsUi::hide_all() {
    show_many(all_controls(), SW_HIDE);
}

void ConnectionsSettingsUi::set_mode(Mode mode) {
    mode_ = mode;
    apply_visibility();
    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
}

void ConnectionsSettingsUi::apply_visibility() {
    std::vector<HWND> desired;
    if (visible_ && mode_ == Mode::Landing) {
        desired = {title_,   desc_,     project_lbl_, list_,       btn_add_,
                   btn_manage_, btn_dup_, btn_toggle_,  btn_delete_};
    } else if (visible_) {
        desired = {btn_back_, detail_title_, status_,      tab_route_, tab_creds_,
                   tab_policy_, btn_save_,   btn_cancel_, btn_test_};
        for (HWND h : section_controls(mode_)) {
            desired.push_back(h);
        }
        // Shown from any section, not just Credentials: with the Keyring locked no credential reference
        // can be chosen, so the record can never validate and Save would refuse from anywhere.
        if (keyring_locked_ || vault_missing_) {
            desired.push_back(btn_unlock_);
        }
    }

    // Diffed rather than hide-everything-then-show-the-active-set. That hid and immediately re-showed
    // dozens of controls on every layout pass, which flashed the whole panel; and a kit button that is
    // shown without being invalidated paints blank until something else dirties it.
    for (HWND h : all_controls()) {
        if (!h) {
            continue;
        }
        const bool want = std::find(desired.begin(), desired.end(), h) != desired.end();
        // The control's own style, not IsWindowVisible: the latter reports false whenever the frame
        // itself is hidden, which would desync this diff during startup and teardown.
        const bool shown = (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
        if (want == shown) {
            continue;
        }
        ShowWindow(h, want ? SW_SHOW : SW_HIDE);
        if (want) {
            InvalidateRect(h, nullptr, TRUE);
        }
    }
    if (!visible_ || mode_ == Mode::Landing) {
        return;
    }
    // The section tabs stay visible across a section switch, so the diff above never touches them —
    // but their active state changed and they own that paint.
    for (HWND tab : {tab_route_, tab_creds_, tab_policy_}) {
        if (tab) {
            InvalidateRect(tab, nullptr, TRUE);
        }
    }

    // Win32 tab order is z-order, which follows creation order — and the action buttons were created
    // before the fields they act on, so Tab would otherwise walk Save/Cancel/Test before the form.
    std::vector<HWND> order{btn_back_, tab_route_, tab_creds_, tab_policy_};
    for (const auto& form_row : rows_for(mode_)) {
        if (form_row.field) order.push_back(form_row.field);
        if (form_row.extra) order.push_back(form_row.extra);
    }
    if (keyring_locked_ || vault_missing_) order.push_back(btn_unlock_);
    order.insert(order.end(), {btn_save_, btn_test_, btn_cancel_});
    ui_kit::set_tab_order(order);
}

void ConnectionsSettingsUi::set_visible(bool visible) {
    // Deliberately keeps mode_. window.cpp's layout() calls hide_settings_controls() -> set_visible(false)
    // on every settings layout pass, so resetting here threw the open form away the instant it appeared.
    // Leaving the page is signalled by reload(), which is the only place that resets.
    visible_ = visible;
    apply_visibility();
}

void ConnectionsSettingsUi::layout(const RECT& area) {
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
        const int actions_y = bottom - row;
        MoveWindow(list_, x, y, w, (std::max)(row * 3, actions_y - gap - y), TRUE);
        int bx = x;
        MoveWindow(btn_add_, bx, actions_y, btn_w + dip(parent_, 40), row, TRUE);
        bx += btn_w + dip(parent_, 48) + gap;
        for (HWND b : {btn_manage_, btn_dup_, btn_toggle_, btn_delete_}) {
            MoveWindow(b, bx, actions_y, btn_w, row, TRUE);
            bx += btn_w + gap;
        }
        return;
    }

    int y = top;
    MoveWindow(btn_back_, x, y, btn_w + dip(parent_, 40), row, TRUE);
    y += row + gap;
    MoveWindow(detail_title_, x, y, w, row, TRUE);
    y += row + gap;

    const int tab_w = dip(parent_, 130);
    int tx = x;
    for (HWND tab : {tab_route_, tab_creds_, tab_policy_}) {
        MoveWindow(tab, tx, y, tab_w, dip(parent_, 30), TRUE);
        tx += tab_w + gap;
    }
    y += dip(parent_, 30) + m.pad_section;

    const int label_w = (std::min)(m.label_w + dip(parent_, 40), w / 3);
    for (const auto& form_row : rows_for(mode_)) {
        const int h = row * form_row.height_rows;
        if (form_row.label) {
            MoveWindow(form_row.label, x, y, label_w - gap, h, TRUE);
        }
        const int field_x = x + label_w;
        const int field_total = (std::max)(1, w - label_w);
        if (form_row.extra) {
            // Literal on the left, "or Keyring reference" on the right. Both are always visible so
            // the choice is legible without a mode toggle.
            const int half = (field_total - gap) / 2;
            MoveWindow(form_row.field, field_x, y, half, h, TRUE);
            MoveWindow(form_row.extra, field_x + half + gap, y, (std::max)(1, field_total - half - gap), h,
                       TRUE);
        } else {
            MoveWindow(form_row.field, field_x, y, field_total, h, TRUE);
        }
        ui_kit::center_field_text(form_row.field);
        y += h + gap;
    }
    // Keep feedback and actions in the form's normal flow. Anchoring these to the
    // nominal panel bottom placed them below the clipped client area at some DPI
    // and window-size combinations, leaving only a fragment of the error visible.
    const int status_y = y;
    const int actions_y = status_y + row + gap;
    MoveWindow(status_, x, status_y, w, row, TRUE);
    int bx = x;
    MoveWindow(btn_save_, bx, actions_y, btn_w + dip(parent_, 40), row, TRUE);
    bx += btn_w + dip(parent_, 48) + gap;
    MoveWindow(btn_cancel_, bx, actions_y, btn_w, row, TRUE);
    bx += btn_w + gap;
    MoveWindow(btn_test_, bx, actions_y, btn_w + dip(parent_, 20), row, TRUE);
    bx += btn_w + dip(parent_, 28) + gap;
    // Sits with the actions rather than inside Credentials: it applies to the whole form.
    if (keyring_locked_ || vault_missing_) {
        MoveWindow(btn_unlock_, bx, actions_y, btn_w + dip(parent_, 30), row, TRUE);
    }

    if (pending_focus_) {
        SetFocus(pending_focus_);
        pending_focus_ = nullptr;
    }
}

void ConnectionsSettingsUi::set_status(const std::wstring& text, bool error) {
    if (!status_) {
        return;
    }
    // The static is muted by default; an error is prefixed rather than recolored so the page needs no
    // per-control color plumbing.
    SetWindowTextW(status_, error ? (L"! " + text).c_str() : text.c_str());
    InvalidateRect(status_, nullptr, TRUE);
}

void ConnectionsSettingsUi::report_test_result(bool ok, const std::wstring& message) {
    set_status(message, !ok);
}

void ConnectionsSettingsUi::fill_ref_select(HWND select, const std::string& current) {
    if (!select) {
        return;
    }
    const auto slot = std::find(ref_selects_.begin(), ref_selects_.end(), select);
    const std::size_t index = static_cast<std::size_t>(slot - ref_selects_.begin());
    if (index < ref_missing_.size()) {
        ref_missing_[index].clear();
    }

    // The closed face is the only place a locked or empty Keyring is visible, so it has to say which.
    // A bare "(none)" reads as "there is nothing to pick" rather than "you have not unlocked yet".
    const wchar_t* empty_label = L"(none)";
    if (vault_missing_) {
        empty_label = L"(create the Keyring first)";
    } else if (keyring_locked_) {
        empty_label = L"(Keyring locked — unlock to choose)";
    } else if (secret_names_.empty()) {
        empty_label = L"(no secrets in this project yet)";
    }

    std::vector<ui_kit::SelectItem> items;
    items.push_back({empty_label, -1});
    int selected = 0;
    for (std::size_t i = 0; i < secret_names_.size(); ++i) {
        items.push_back({utf16(secret_names_[i]), static_cast<LPARAM>(i)});
        if (secret_names_[i] == current) {
            selected = static_cast<int>(i) + 1;
        }
    }
    // A saved reference can outlive the secret it names, and a locked Keyring lists nothing at all.
    // Either way the stored name stays selected and flagged, so opening the form cannot erase it.
    if (!current.empty() && selected == 0) {
        items.push_back({utf16(current) + (keyring_locked_ ? L"  · Keyring locked" : L"  · missing"),
                         static_cast<LPARAM>(secret_names_.size())});
        selected = static_cast<int>(items.size()) - 1;
        if (index < ref_missing_.size()) {
            ref_missing_[index] = current;
        }
    }
    ui_kit::select_set_items(select, items);
    ui_kit::select_set_index(select, selected);
}

std::string ConnectionsSettingsUi::ref_name_of(HWND select) const {
    if (!select || ui_kit::select_get_index(select) <= 0) {
        return {};
    }
    const LPARAM data = ui_kit::select_get_data(select);
    if (data >= 0 && static_cast<std::size_t>(data) < secret_names_.size()) {
        return secret_names_[static_cast<std::size_t>(data)];
    }
    const auto slot = std::find(ref_selects_.begin(), ref_selects_.end(), select);
    const std::size_t index = static_cast<std::size_t>(slot - ref_selects_.begin());
    return index < ref_missing_.size() ? ref_missing_[index] : std::string();
}

void ConnectionsSettingsUi::refresh_secret_names(Keyring& keyring) {
    keyring_locked_ = !keyring.is_unlocked();
    vault_missing_ = !Keyring::app_vault_exists() && !keyring.vault_bound();
    secret_names_.clear();
    if (!keyring_locked_) {
        for (const auto& ref : keyring.list_refs_for_ui(project_id_)) {
            secret_names_.push_back(ref.name);
        }
    }
    ref_missing_.assign(ref_selects_.size(), std::string());
    if (btn_unlock_) {
        SetWindowTextW(btn_unlock_, vault_missing_ ? L"Create Keyring…" : L"Unlock Keyring");
        InvalidateRect(btn_unlock_, nullptr, TRUE);
    }
}

void ConnectionsSettingsUi::load_form(const ProjectConnection& c) {
    SetWindowTextW(name_, utf16(c.name).c_str());
    SetWindowTextW(alias_, utf16(c.alias).c_str());
    set_checked(enabled_, c.enabled);
    ui_kit::select_set_items(route_, {{L"Remote execution over SSH", 0},
                                     {L"SSH tunnel", 1},
                                     {L"Direct", 2}});
    ui_kit::select_set_index(route_, static_cast<int>(c.route_type));
    SetWindowTextW(ssh_host_, utf16(c.ssh.host).c_str());
    set_number(ssh_port_, c.ssh.port);
    SetWindowTextW(ssh_user_, utf16(c.ssh.username).c_str());
    SetWindowTextW(host_key_, utf16(c.ssh.host_key).c_str());

    ui_kit::select_set_items(engine_, {{L"MySQL", 0}, {L"PostgreSQL", 1}, {L"SQL Server", 2}, {L"MongoDB", 3}});
    ui_kit::select_set_index(engine_, static_cast<int>(c.database.engine));
    SetWindowTextW(db_host_, utf16(c.database.host).c_str());
    set_number(db_port_, c.database.port);
    SetWindowTextW(db_name_, utf16(c.database.database).c_str());

    fill_authority(pol_read_, c.query_policy.read);
    fill_authority(pol_data_, c.query_policy.data_modification);
    fill_authority(pol_schema_, c.query_policy.schema_modification);
    fill_authority(pol_admin_, c.query_policy.administrative);
    set_checked(pol_unrestricted_, c.query_policy.unrestricted);
    set_checked(pol_multi_, c.query_policy.allow_multiple_statements);

    ui_kit::select_set_items(res_visibility_, {{L"Agent and human", 0},
                                              {L"Agent only", 1},
                                              {L"Human only", 2},
                                              {L"Metadata only", 3},
                                              {L"Aggregate only", 4}});
    ui_kit::select_set_index(res_visibility_, static_cast<int>(c.result_policy.visibility));
    set_number(res_max_rows_, c.result_policy.max_rows);
    set_number(res_max_bytes_, c.result_policy.max_bytes);
    set_number(res_timeout_, c.result_policy.timeout_seconds);
    set_number(res_max_text_, c.result_policy.max_text_bytes);
    set_checked(res_binary_, c.result_policy.include_binary);

    // Reference selects are populated directly from the record, not from the previous form state.
    fill_ref_select(ssh_host_ref_, c.ssh.host_ref);
    fill_ref_select(ssh_port_ref_, c.ssh.port_ref);
    fill_ref_select(ssh_user_ref_, c.ssh.username_ref);
    fill_ref_select(key_ref_, c.ssh.private_key_ref);
    fill_ref_select(passphrase_ref_, c.ssh.key_passphrase_ref);
    fill_ref_select(auth_ref_, c.ssh.auth_ref);
    fill_ref_select(db_host_ref_, c.database.host_ref);
    fill_ref_select(db_port_ref_, c.database.port_ref);
    fill_ref_select(db_user_ref_, c.database.username_ref);
    fill_ref_select(db_pass_ref_, c.database.password_ref);
    fill_ref_select(tls_ref_, c.database.tls_ca_ref);
}

bool ConnectionsSettingsUi::read_form(const std::string& project_id, ProjectConnection& out,
                                      std::wstring& error) const {
    const auto ref_of = [&](HWND select) { return ref_name_of(select); };

    out = ProjectConnection{};
    out.id = editing_id_;
    out.project_id = project_id;
    out.name = utf8(trimmed(get_text(name_)));
    out.alias = ProjectConnectionManager::normalize_alias(utf8(trimmed(get_text(alias_))));
    out.enabled = checked(enabled_);
    switch (ui_kit::select_get_index(route_)) {
        case 1: out.route_type = ConnectionRouteType::SshTunnel; break;
        case 2: out.route_type = ConnectionRouteType::Direct; break;
        default: out.route_type = ConnectionRouteType::RemoteExecution; break;
    }

    out.ssh.host = utf8(trimmed(get_text(ssh_host_)));
    out.ssh.host_ref = ref_of(ssh_host_ref_);
    out.ssh.username = utf8(trimmed(get_text(ssh_user_)));
    out.ssh.username_ref = ref_of(ssh_user_ref_);
    out.ssh.port_ref = ref_of(ssh_port_ref_);
    out.ssh.host_key = utf8(trimmed(get_text(host_key_)));
    out.ssh.private_key_ref = ref_of(key_ref_);
    out.ssh.key_passphrase_ref = ref_of(passphrase_ref_);
    out.ssh.auth_ref = ref_of(auth_ref_);

    switch (ui_kit::select_get_index(engine_)) {
        case 1: out.database.engine = DatabaseEngine::PostgreSql; break;
        case 2: out.database.engine = DatabaseEngine::SqlServer; break;
        case 3: out.database.engine = DatabaseEngine::MongoDb; break;
        default: out.database.engine = DatabaseEngine::MySql; break;
    }
    out.database.host = utf8(trimmed(get_text(db_host_)));
    out.database.host_ref = ref_of(db_host_ref_);
    out.database.port_ref = ref_of(db_port_ref_);
    out.database.database = utf8(trimmed(get_text(db_name_)));
    out.database.username_ref = ref_of(db_user_ref_);
    out.database.password_ref = ref_of(db_pass_ref_);
    out.database.tls_ca_ref = ref_of(tls_ref_);

    // Ports: a reference makes the literal optional, otherwise the number must parse.
    std::uint32_t port = 0;
    if (out.ssh.port_ref.empty()) {
        if (!read_number(ssh_port_, port) || port == 0 || port > 65535) {
            error = L"SSH port must be a number between 1 and 65535, or come from a Keyring reference.";
            return false;
        }
        out.ssh.port = static_cast<std::uint16_t>(port);
    } else if (read_number(ssh_port_, port) && port > 0 && port <= 65535) {
        out.ssh.port = static_cast<std::uint16_t>(port);
    }
    if (out.database.port_ref.empty()) {
        if (!read_number(db_port_, port) || port == 0 || port > 65535) {
            error = L"Database port must be a number between 1 and 65535, or come from a Keyring reference.";
            return false;
        }
        out.database.port = static_cast<std::uint16_t>(port);
    } else if (read_number(db_port_, port) && port > 0 && port <= 65535) {
        out.database.port = static_cast<std::uint16_t>(port);
    }

    out.query_policy.read = authority_from_select(pol_read_, ConnectionAuthority::Auto);
    out.query_policy.data_modification = authority_from_select(pol_data_, ConnectionAuthority::Ask);
    out.query_policy.schema_modification = authority_from_select(pol_schema_, ConnectionAuthority::Ask);
    out.query_policy.administrative = authority_from_select(pol_admin_, ConnectionAuthority::Block);
    out.query_policy.unrestricted = checked(pol_unrestricted_);
    out.query_policy.allow_multiple_statements = checked(pol_multi_);

    switch (ui_kit::select_get_index(res_visibility_)) {
        case 1: out.result_policy.visibility = ResultVisibility::AgentOnly; break;
        case 2: out.result_policy.visibility = ResultVisibility::HumanOnly; break;
        case 3: out.result_policy.visibility = ResultVisibility::MetadataOnly; break;
        case 4: out.result_policy.visibility = ResultVisibility::AggregateOnly; break;
        default: out.result_policy.visibility = ResultVisibility::AgentAndHuman; break;
    }
    if (!read_number(res_max_rows_, out.result_policy.max_rows) ||
        !read_number(res_max_bytes_, out.result_policy.max_bytes) ||
        !read_number(res_timeout_, out.result_policy.timeout_seconds) ||
        !read_number(res_max_text_, out.result_policy.max_text_bytes)) {
        error = L"Result limits must all be whole numbers.";
        return false;
    }
    out.result_policy.include_binary = checked(res_binary_);

    std::string reason;
    if (!ProjectConnectionManager::validate(out, &reason)) {
        error = utf16(reason);
        if (!error.empty()) {
            error[0] = static_cast<wchar_t>(towupper(error[0]));
        }
        error += L".";
        return false;
    }
    error.clear();
    return true;
}

void ConnectionsSettingsUi::fill_landing(ProjectConnectionManager& mgr, Keyring& keyring) {
    landing_aliases_.clear();
    landing_primary_.clear();
    landing_secondary_.clear();
    if (!list_) {
        return;
    }
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    const auto known = keyring.is_unlocked() ? keyring.list_refs_for_ui(project_id_) : std::vector<SecretRef>{};
    const auto missing = [&](const std::string& ref) {
        if (ref.empty() || !keyring.is_unlocked()) {
            return false;
        }
        for (const auto& r : known) {
            if (r.name == ref) {
                return false;
            }
        }
        return true;
    };

    auto rows = mgr.for_project(project_id_);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto* connection) { return connection->ssh_only; }), rows.end());
    if (rows.empty()) {
        landing_primary_.push_back(L"No connections configured");
        landing_secondary_.push_back(
            L"Add one to let the agent query a database by alias through /scylla-query.");
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(landing_primary_.back().c_str()));
        for (HWND b : {btn_manage_, btn_dup_, btn_toggle_, btn_delete_}) {
            EnableWindow(b, FALSE);
        }
        return;
    }
    for (const auto* c : rows) {
        landing_aliases_.push_back(c->alias);
        std::wstring primary = utf16(c->alias);
        if (!c->name.empty()) {
            primary += L"  ·  " + utf16(c->name);
        }
        if (!c->enabled) {
            primary += L"  ·  Disabled";
        }
        landing_primary_.push_back(primary);

        std::wstring secondary;
        const std::string ssh_host = c->ssh.host_ref.empty() ? c->ssh.host : ("→ " + c->ssh.host_ref);
        const std::string db_host = c->database.host_ref.empty() ? c->database.host : ("→ " + c->database.host_ref);
        secondary += utf16(ssh_host) + L"  →  " + utf16(db_host);
        if (c->ssh.host_key.empty()) {
            secondary += L"  ·  ! no pinned host key";
        }
        int absent = 0;
        for (const std::string* ref : {&c->ssh.host_ref, &c->ssh.port_ref, &c->ssh.username_ref,
                                       &c->ssh.private_key_ref, &c->ssh.key_passphrase_ref,
                                       &c->ssh.auth_ref, &c->database.host_ref, &c->database.port_ref,
                                       &c->database.username_ref, &c->database.password_ref,
                                       &c->database.tls_ca_ref}) {
            if (missing(*ref)) {
                ++absent;
            }
        }
        if (absent > 0) {
            secondary += L"  ·  ! " + std::to_wstring(absent) + L" missing Keyring reference";
        }
        landing_secondary_.push_back(secondary);
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(landing_primary_.back().c_str()));
    }
    SendMessageW(list_, LB_SETCURSEL, 0, 0);
    for (HWND b : {btn_manage_, btn_dup_, btn_toggle_, btn_delete_}) {
        EnableWindow(b, TRUE);
    }
}

std::string ConnectionsSettingsUi::selected_alias() const {
    if (!list_ || landing_aliases_.empty()) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(landing_aliases_.size())) {
        return {};
    }
    return landing_aliases_[static_cast<std::size_t>(sel)];
}

void ConnectionsSettingsUi::reload(ProjectConnectionManager& mgr, Keyring& keyring,
                                   const std::string& project_id, const std::wstring& project_name) {
    // A different project means the open form describes a connection that is no longer in scope.
    if (project_id != project_id_) {
        mode_ = Mode::Landing;
        editing_id_.clear();
        editing_alias_.clear();
    } else if (mode_ != Mode::Landing) {
        // Same project, form still open: this is the return trip from the Keyring page. Re-sync the
        // reference lists so newly unlocked secrets appear, preserving what is already chosen.
        // Names must be read before the list is rebuilt, or every stored index goes stale.
        std::vector<std::string> chosen;
        chosen.reserve(ref_selects_.size());
        for (HWND select : ref_selects_) {
            chosen.push_back(ref_name_of(select));
        }
        refresh_secret_names(keyring);
        for (std::size_t i = 0; i < ref_selects_.size(); ++i) {
            fill_ref_select(ref_selects_[i], chosen[i]);
        }
    }
    project_id_ = project_id;
    project_name_ = project_name;
    if (project_id.empty()) {
        SetWindowTextW(project_lbl_, L"Open a project to manage its connections.");
        EnableWindow(btn_add_, FALSE);
    } else {
        SetWindowTextW(project_lbl_, (project_name.empty() ? utf16(project_id) : project_name).c_str());
        EnableWindow(btn_add_, TRUE);
    }
    if (mode_ == Mode::Landing) {
        keyring_locked_ = !keyring.is_unlocked();
        fill_landing(mgr, keyring);
    }
    apply_visibility();
}

bool ConnectionsSettingsUi::owns_hwnd(HWND child) const {
    for (HWND h : all_controls()) {
        if (h && h == child) {
            return true;
        }
    }
    return false;
}

bool ConnectionsSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || !parent_ || mi->CtlID != Cmd_ConnList) {
        return false;
    }
    mi->itemHeight = static_cast<UINT>(dip(parent_, 48));
    return true;
}

bool ConnectionsSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di || di->CtlID != Cmd_ConnList) {
        return false;
    }
    if (di->itemID >= landing_primary_.size()) {
        return true;
    }
    const bool sel = (di->itemState & ODS_SELECTED) != 0;
    ui_kit::paint_entity_row(di->hDC, di->rcItem, font ? font : font_, landing_primary_[di->itemID].c_str(),
                             di->itemID < landing_secondary_.size() ? landing_secondary_[di->itemID].c_str()
                                                                    : L"",
                             sel, false);
    return true;
}

UINT ConnectionsSettingsUi::default_command(HWND field) const {
    if (mode_ == Mode::Landing) {
        return 0;
    }
    // Enter anywhere in the form saves; that is the only destructive-free default here.
    for (const auto& row : rows_for(mode_)) {
        if (row.field == field) {
            return Cmd_ConnSave;
        }
    }
    return 0;
}

UINT ConnectionsSettingsUi::cancel_command() const {
    return mode_ == Mode::Landing ? 0 : static_cast<UINT>(Cmd_ConnCancel);
}

bool ConnectionsSettingsUi::on_command(WORD id, WORD notify, HWND owner, ProjectConnectionManager& mgr,
                                       Keyring& keyring, const std::string& project_id,
                                       const std::wstring& persist_path) {
    if (id < Cmd_ConnFirst || id > Cmd_ConnLast) {
        return false;
    }
    switch (notify) {
        // Edit-control chatter, not commands. Claiming these made window.cpp treat every keystroke as a
        // handled command and relayout the whole frame, which flashed the panel while typing. None of
        // these codes collide with the BN_ / LBN_ / CBN_ values this page does act on.
        case EN_SETFOCUS:
        case EN_KILLFOCUS:
        case EN_CHANGE:
        case EN_UPDATE:
        case EN_ERRSPACE:
        case EN_MAXTEXT:
        case EN_HSCROLL:
        case EN_VSCROLL:
            return false;
        default:
            break;
    }
    if (project_id.empty()) {
        return true;  // page is inert without a project, but still owns its ids
    }

    // Every kit select needs its click forwarded to open the popup.
    for (HWND select : {route_, engine_, pol_read_, pol_data_, pol_schema_, pol_admin_, res_visibility_}) {
        if (select && GetDlgCtrlID(select) == id) {
            return ui_kit::select_handle_command(select, notify);
        }
    }
    for (HWND select : ref_selects_) {
        if (select && GetDlgCtrlID(select) == id) {
            return ui_kit::select_handle_command(select, notify);
        }
    }

    const auto persist = [&]() {
        if (persist_path.empty() || mgr.save(persist_path)) {
            return true;
        }
        ui_kit::report_save_failure(owner ? owner : parent_, L"connections", persist_path);
        return false;
    };
    const auto open_detail = [&](const ProjectConnection& c, bool duplicate) {
        editing_id_ = duplicate ? std::string() : c.id;
        editing_alias_ = duplicate ? std::string() : c.alias;
        refresh_secret_names(keyring);
        load_form(c);
        if (duplicate) {
            SetWindowTextW(alias_, L"");
            SetWindowTextW(name_, (utf16(c.name) + L" (copy)").c_str());
        }
        SetWindowTextW(detail_title_, editing_id_.empty() ? L"Add Connection" : utf16(c.alias).c_str());
        // Leading with the Keyring state when it is locked: every credential field on this form is a
        // reference, so there is nothing useful to do until it is open.
        if (vault_missing_) {
            set_status(L"No Keyring yet — credentials are stored there and referenced by name here.", true);
        } else if (keyring_locked_) {
            set_status(L"Keyring locked — unlock it to choose credential references.", true);
        } else {
            set_status(editing_id_.empty() ? L"Alias, SSH route, pinned host key, and database "
                                             L"credentials are required."
                                           : L"",
                       false);
        }
        set_mode(Mode::Route);
        pending_focus_ = name_;
    };

    if (id == Cmd_ConnList) {
        // Selection events only. Claiming LBN_SETFOCUS / LBN_KILLFOCUS / LBN_SELCANCEL would report
        // mere focus movement as handled, and window.cpp relayouts the whole frame on a handled command.
        if (notify == LBN_DBLCLK) {
            id = Cmd_ConnManage;
            notify = 0;
        } else {
            return notify == LBN_SELCHANGE;
        }
    }

    if (id == Cmd_ConnAdd) {
        ProjectConnection fresh;
        fresh.project_id = project_id;
        open_detail(fresh, false);
        return true;
    }
    if (id == Cmd_ConnManage || id == Cmd_ConnDup) {
        const std::string alias = selected_alias();
        const auto* found = alias.empty() ? nullptr : mgr.resolve_alias(project_id, alias);
        if (!found) {
            return true;
        }
        open_detail(*found, id == Cmd_ConnDup);
        return true;
    }
    if (id == Cmd_ConnToggleEnabled) {
        const std::string alias = selected_alias();
        const auto* found = alias.empty() ? nullptr : mgr.resolve_alias(project_id, alias);
        if (!found) {
            return true;
        }
        if (auto* mut = mgr.find_mut(found->id)) {
            mut->enabled = !mut->enabled;
            persist();
        }
        fill_landing(mgr, keyring);
        return true;
    }
    if (id == Cmd_ConnDelete) {
        const std::string alias = selected_alias();
        const auto* found = alias.empty() ? nullptr : mgr.resolve_alias(project_id, alias);
        if (!found) {
            return true;
        }
        if (!ui_kit::confirm_destructive(owner, L"Delete connection", utf16(found->alias).c_str(),
                                        L"Keyring secrets are not deleted. Any agent instruction that "
                                        L"names this alias will stop working.")) {
            return true;
        }
        mgr.remove(project_id, found->id);
        persist();
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        return true;
    }
    if (id == Cmd_ConnBack || id == Cmd_ConnCancel) {
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        return true;
    }
    // Switching section also has to re-seat focus: the outgoing section's controls are hidden, and
    // hiding the focused one leaves focus on the frame.
    const auto show_section = [&](Mode mode) {
        set_mode(mode);
        const auto& rows = rows_for(mode);
        pending_focus_ = rows.empty() ? nullptr : rows.front().field;
    };
    if (id == Cmd_ConnSecRoute) {
        show_section(Mode::Route);
        return true;
    }
    if (id == Cmd_ConnSecCreds) {
        show_section(Mode::Credentials);
        if (vault_missing_) {
            set_status(L"No Keyring yet — every field here is a reference into it.", true);
        } else if (keyring_locked_) {
            set_status(L"Keyring locked — unlock it to choose credential references.", true);
        }
        return true;
    }
    if (id == Cmd_ConnSecPolicy) {
        show_section(Mode::Policy);
        return true;
    }
    if (id == Cmd_ConnUnlock) {
        request_ = ConnUiRequest::OpenKeyring;
        return true;
    }
    if (id == Cmd_ConnKeyImport) {
        if (keyring_locked_ || vault_missing_) {
            request_ = ConnUiRequest::OpenKeyring;
            set_status(vault_missing_ ? L"Create the Keyring before importing a private key."
                                      : L"Unlock the Keyring before importing a private key.",
                       true);
            return true;
        }

        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner ? owner : parent_;
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrFilter = L"SSH private keys\0*.pem;*.key;id_*\0All files\0*.*\0";
        dialog.lpstrTitle = L"Import SSH private key into Scylla Keyring";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return true;

        std::string private_key;
        std::wstring read_error;
        if (!read_private_key_file(path, private_key, read_error)) {
            set_status(read_error, true);
            MessageBoxW(dialog.hwndOwner, read_error.c_str(), L"Import SSH private key",
                        MB_OK | MB_ICONWARNING);
            return true;
        }

        const std::string secret_name = imported_key_name(alias_);
        const auto refs = keyring.list_refs_for_ui(project_id);
        const bool replacing = std::any_of(refs.begin(), refs.end(), [&](const SecretRef& ref) {
            return ref.name == secret_name && ref.scope == SecretScope::Project &&
                   ref.project_id == project_id;
        });
        if (replacing) {
            const std::wstring prompt = L"Replace the existing project Keyring value named\n\n" +
                                        utf16(secret_name) + L"\n\nwith the selected file?";
            if (MessageBoxW(dialog.hwndOwner, prompt.c_str(), L"Import SSH private key",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                SecureZeroMemory(private_key.data(), private_key.size());
                return true;
            }
        }

        std::wstring filename(path);
        const std::size_t slash = filename.find_last_of(L"\\/");
        if (slash != std::wstring::npos) filename.erase(0, slash + 1);
        const std::string description = "SSH private key imported from " + utf8(filename);
        const KeyringStatus imported = keyring.add_secret(secret_name, private_key, description,
                                                          SecretScope::Project, project_id);
        SecureZeroMemory(private_key.data(), private_key.size());
        private_key.clear();
        if (imported != KeyringStatus::Ok) {
            const std::wstring message = L"The private key could not be stored in the Keyring (" +
                                         utf16(keyring_status_string(imported)) + L").";
            set_status(message, true);
            MessageBoxW(dialog.hwndOwner, message.c_str(), L"Import SSH private key",
                        MB_OK | MB_ICONERROR);
            return true;
        }

        std::vector<std::string> chosen;
        chosen.reserve(ref_selects_.size());
        for (HWND select : ref_selects_) chosen.push_back(ref_name_of(select));
        refresh_secret_names(keyring);
        for (std::size_t i = 0; i < ref_selects_.size(); ++i) {
            fill_ref_select(ref_selects_[i], ref_selects_[i] == key_ref_ ? secret_name : chosen[i]);
        }
        set_status(L"Private key imported into the project Keyring and selected for this connection.", false);
        return true;
    }
    // Save and Test both refuse the same way, and both refusals used to land only in the muted status
    // line at the bottom of the page — quiet enough to read as "the button does nothing". Validation
    // failures are message boxes here, matching every other settings page.
    const auto refuse = [&](const std::wstring& message) {
        set_status(message, true);
        MessageBoxW(owner ? owner : parent_, message.c_str(), L"Connection", MB_OK | MB_ICONWARNING);
    };
    // A locked Keyring cannot satisfy the mandatory credential references, so the form can never
    // validate. Say that up front instead of reporting whichever reference happens to be checked first.
    const auto blocked_by_keyring = [&]() {
        if (!keyring_locked_ && !vault_missing_) {
            return false;
        }
        const wchar_t* text =
            vault_missing_
                ? L"This connection needs Keyring references for its database credentials, and no "
                  L"Keyring exists yet.\n\nCreate the Keyring now?"
                : L"This connection needs Keyring references for its database credentials, and the "
                  L"Keyring is locked — the reference dropdowns cannot list anything.\n\nUnlock it now?";
        set_status(vault_missing_ ? L"Create the Keyring before saving this connection."
                                  : L"Unlock the Keyring before saving this connection.",
                   true);
        if (MessageBoxW(owner ? owner : parent_, text, L"Connection", MB_YESNO | MB_ICONWARNING) == IDYES) {
            request_ = ConnUiRequest::OpenKeyring;
        }
        return true;
    };

    if (id == Cmd_ConnSave) {
        if (blocked_by_keyring()) {
            return true;
        }
        ProjectConnection built;
        std::wstring error;
        if (!read_form(project_id, built, error)) {
            refuse(error);
            return true;
        }
        std::string reason;
        if (!mgr.upsert(built, &reason)) {
            refuse(utf16(reason));
            return true;
        }
        if (!persist()) {
            return true;
        }
        const bool renamed = !editing_alias_.empty() && editing_alias_ != built.alias;
        set_mode(Mode::Landing);
        fill_landing(mgr, keyring);
        if (renamed) {
            set_status(L"Saved. The alias changed, so update any saved instruction that named \"" +
                           utf16(editing_alias_) + L"\".",
                       false);
        }
        editing_id_.clear();
        editing_alias_.clear();
        return true;
    }
    if (id == Cmd_ConnTest) {
        if (blocked_by_keyring()) {
            return true;
        }
        // Save first: the broker resolves by alias from the persisted set, so testing unsaved edits
        // would silently test the previous values.
        ProjectConnection built;
        std::wstring error;
        if (!read_form(project_id, built, error)) {
            refuse(error);
            return true;
        }
        std::string reason;
        if (!mgr.upsert(built, &reason) || !persist()) {
            refuse(reason.empty() ? L"The connection could not be saved before testing." : utf16(reason));
            return true;
        }
        editing_id_ = built.id;
        editing_alias_ = built.alias;
        pending_test_alias_ = built.alias;
        set_status(L"Testing…", false);
        request_ = ConnUiRequest::TestConnection;
        return true;
    }
    return true;
}

}  // namespace scyllagpt
