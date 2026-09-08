#include "scyllagpt/mcp_settings_ui.h"

#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <algorithm>

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
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(n));
    return s;
}

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

}  // namespace

bool McpSettingsUi::create(HWND parent, HFONT font, int id_list, int id_detail, int id_btn_add, int id_btn_add_account,
                           int id_btn_manage, int id_btn_reauth, int id_btn_check, int id_btn_disable,
                           int id_btn_disconnect, int id_btn_remove, int id_add_template, int id_add_name,
                           int id_add_alias, int id_add_endpoint, int id_add_save, int id_add_cancel) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    id_list_ = id_list;
    id_detail_ = id_detail;
    id_btn_add_ = id_btn_add;
    id_btn_add_account_ = id_btn_add_account;
    id_btn_manage_ = id_btn_manage;
    id_btn_reauth_ = id_btn_reauth;
    id_btn_check_ = id_btn_check;
    id_btn_disable_ = id_btn_disable;
    id_btn_disconnect_ = id_btn_disconnect;
    id_btn_remove_ = id_btn_remove;
    id_add_template_ = id_add_template;
    id_add_name_ = id_add_name;
    id_add_alias_ = id_add_alias;
    id_add_endpoint_ = id_add_endpoint;
    id_add_save_ = id_add_save;
    id_add_cancel_ = id_add_cancel;

    heading_ = ui_kit::create_static(parent, inst_, 0, L"MCP", font, false);
    desc_ = ui_kit::create_static(parent, inst_, 0, L"Manage MCP connections, aliases, and auth state.", font, true);
    list_ = ui_kit::create_entity_list(parent, inst_, static_cast<UINT>(id_list), font);
    detail_ = ui_kit::create_text_field(parent, inst_, static_cast<UINT>(id_detail), font);
    // Multi-line read-only detail
    SetWindowLongW(detail_, GWL_STYLE,
                   GetWindowLongW(detail_, GWL_STYLE) | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
    ui_kit::center_field_text(detail_);

    btn_add_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_add), L"Add MCP",
                                     ui_kit::ButtonKind::Primary, font);
    btn_add_account_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_add_account),
                                             L"Add Another Account", ui_kit::ButtonKind::Secondary, font);
    btn_manage_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_manage), L"Manage",
                                        ui_kit::ButtonKind::Secondary, font);
    btn_reauth_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_reauth), L"Reauthenticate",
                                        ui_kit::ButtonKind::Secondary, font);
    btn_check_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_check), L"Check Now",
                                       ui_kit::ButtonKind::Secondary, font);
    btn_disable_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_disable), L"Disable",
                                         ui_kit::ButtonKind::Secondary, font);
    btn_disconnect_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_disconnect), L"Disconnect",
                                            ui_kit::ButtonKind::Secondary, font);
    btn_remove_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_btn_remove), L"Remove",
                                        ui_kit::ButtonKind::Danger, font);
    btn_test_ = ui_kit::create_button(parent, inst_, IdTest, L"Test Connection", ui_kit::ButtonKind::Secondary, font);

    add_lbl_template_ = ui_kit::create_static(parent, inst_, 0, L"Template", font, false);
    add_template_ = ui_kit::create_select(parent, inst_, static_cast<UINT>(id_add_template), font);
    add_lbl_name_ = ui_kit::create_static(parent, inst_, 0, L"Connection name", font, false);
    add_name_ = ui_kit::create_text_field(parent, inst_, static_cast<UINT>(id_add_name), font);
    add_lbl_alias_ = ui_kit::create_static(parent, inst_, 0, L"Agent alias (@)", font, false);
    add_alias_ = ui_kit::create_text_field(parent, inst_, static_cast<UINT>(id_add_alias), font);
    add_lbl_transport_ = ui_kit::create_static(parent, inst_, 0, L"Transport", font, false);
    add_transport_ = ui_kit::create_select(parent, inst_, IdAddTransport, font);
    add_lbl_endpoint_ = ui_kit::create_static(parent, inst_, 0, L"Endpoint / command", font, false);
    add_endpoint_ = ui_kit::create_path_field(parent, inst_, static_cast<UINT>(id_add_endpoint), font);
    btn_add_save_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_add_save), L"Save",
                                          ui_kit::ButtonKind::Primary, font);
    btn_add_cancel_ = ui_kit::create_button(parent, inst_, static_cast<UINT>(id_add_cancel), L"Cancel",
                                            ui_kit::ButtonKind::Secondary, font);

    edit_lbl_name_ = ui_kit::create_static(parent, inst_, 0, L"Connection name", font, false);
    edit_name_ = ui_kit::create_text_field(parent, inst_, IdEditName, font);
    edit_lbl_alias_ = ui_kit::create_static(parent, inst_, 0, L"Agent alias (@)", font, false);
    edit_alias_ = ui_kit::create_text_field(parent, inst_, IdEditAlias, font);
    edit_lbl_transport_ = ui_kit::create_static(parent, inst_, 0, L"Transport", font, false);
    edit_transport_ = ui_kit::create_select(parent, inst_, IdEditTransport, font);
    edit_lbl_endpoint_ = ui_kit::create_static(parent, inst_, 0, L"Endpoint / command", font, false);
    edit_endpoint_ = ui_kit::create_path_field(parent, inst_, IdEditEndpoint, font);
    edit_enabled_ = ui_kit::create_checkbox(parent, inst_, IdEditEnabled, L"Enabled", font);
    edit_scope_all_ = ui_kit::create_checkbox(parent, inst_, IdEditScopeAll, L"Available to all projects", font);
    edit_lbl_scope_ = ui_kit::create_static(parent, inst_, 0, L"Project scope", font, true);
    edit_scope_list_ = ui_kit::create_list(parent, inst_, IdEditScopeList, font, true);
    btn_edit_save_ = ui_kit::create_button(parent, inst_, IdEditSave, L"Save", ui_kit::ButtonKind::Primary, font);
    btn_edit_cancel_ = ui_kit::create_button(parent, inst_, IdEditCancel, L"Cancel", ui_kit::ButtonKind::Secondary,
                                             font);

    ui_kit::set_placeholder(add_name_, L"e.g. Linear — work account");
    ui_kit::set_placeholder(add_alias_, L"linear (used as @linear by agents)");
    ui_kit::set_placeholder(add_endpoint_, L"https://… or a local command");
    ui_kit::set_placeholder(edit_name_, L"e.g. Linear — work account");
    ui_kit::set_placeholder(edit_alias_, L"linear (used as @linear by agents)");

    ui_kit::style_scroll_host(list_, theme().panel);
    ui_kit::style_scroll_host(detail_, theme().panel);
    ui_kit::style_scroll_host(edit_scope_list_, theme().panel);
    fill_templates();
    fill_transport(add_transport_, McpTransportKind::Http);
    fill_transport(edit_transport_, McpTransportKind::Http);
    set_mode(Mode::Browse);
    hide();
    return list_ != nullptr && detail_ != nullptr;
}

std::vector<HWND> McpSettingsUi::browse_controls() const {
    return {list_,       detail_,      btn_add_,        btn_add_account_, btn_manage_, btn_reauth_,
            btn_check_,  btn_test_,    btn_disable_,    btn_disconnect_,  btn_remove_};
}

std::vector<HWND> McpSettingsUi::add_controls() const {
    return {add_lbl_template_,  add_template_,  add_lbl_name_,     add_name_,     add_lbl_alias_,
            add_alias_,         add_lbl_transport_, add_transport_, add_lbl_endpoint_, add_endpoint_,
            btn_add_save_,      btn_add_cancel_};
}

std::vector<HWND> McpSettingsUi::edit_controls() const {
    return {edit_lbl_name_,      edit_name_,       edit_lbl_alias_,  edit_alias_,      edit_lbl_transport_,
            edit_transport_,     edit_lbl_endpoint_, edit_endpoint_, edit_enabled_,    edit_scope_all_,
            edit_lbl_scope_,     edit_scope_list_, btn_edit_save_,   btn_edit_cancel_};
}

void McpSettingsUi::destroy() {
    std::vector<HWND> all{heading_, desc_};
    for (const auto& roster : {browse_controls(), add_controls(), edit_controls()}) {
        all.insert(all.end(), roster.begin(), roster.end());
    }
    for (HWND h : all) {
        if (h) {
            DestroyWindow(h);
        }
    }
    heading_ = desc_ = list_ = detail_ = btn_add_ = btn_add_account_ = btn_manage_ = btn_reauth_ = btn_check_ =
        btn_test_ = btn_disable_ = btn_disconnect_ = btn_remove_ = nullptr;
    add_lbl_template_ = add_template_ = add_lbl_name_ = add_name_ = add_lbl_alias_ = add_alias_ =
        add_lbl_transport_ = add_transport_ = add_lbl_endpoint_ = add_endpoint_ = btn_add_save_ = btn_add_cancel_ =
            nullptr;
    edit_lbl_name_ = edit_name_ = edit_lbl_alias_ = edit_alias_ = edit_lbl_transport_ = edit_transport_ =
        edit_lbl_endpoint_ = edit_endpoint_ = edit_enabled_ = edit_scope_all_ = edit_lbl_scope_ = edit_scope_list_ =
            btn_edit_save_ = btn_edit_cancel_ = nullptr;
    parent_ = nullptr;
    visible_ = false;
    row_ids_.clear();
    scope_projects_.clear();
    scope_checked_.clear();
    selected_id_.clear();
    edit_id_.clear();
}

void McpSettingsUi::show() {
    visible_ = true;
    set_mode(mode_);
    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
}

void McpSettingsUi::hide() {
    visible_ = false;
    show_many({heading_, desc_}, SW_HIDE);
    for (const auto& roster : {browse_controls(), add_controls(), edit_controls()}) {
        show_many(roster, SW_HIDE);
    }
}

void McpSettingsUi::set_mode(Mode mode) {
    mode_ = mode;
    if (!visible_) {
        return;
    }
    show_many(browse_controls(), mode_ == Mode::Browse ? SW_SHOW : SW_HIDE);
    show_many(add_controls(), mode_ == Mode::Add ? SW_SHOW : SW_HIDE);
    show_many(edit_controls(), mode_ == Mode::Edit ? SW_SHOW : SW_HIDE);
    show_many({heading_, desc_}, SW_SHOW);
    if (!heading_) {
        return;
    }
    if (mode_ == Mode::Add) {
        SetWindowTextW(heading_, L"Add MCP connection");
        if (desc_) {
            SetWindowTextW(desc_, L"Pick a template and save. Cancel returns to the list.");
        }
    } else if (mode_ == Mode::Edit) {
        SetWindowTextW(heading_, L"Manage MCP connection");
        if (desc_) {
            SetWindowTextW(desc_,
                           L"Edit the name, alias, transport, endpoint, and which projects may use it. "
                           L"Cancel discards changes.");
        }
    } else {
        SetWindowTextW(heading_, L"MCP");
        if (desc_) {
            SetWindowTextW(desc_, L"Manage MCP connections, aliases, and auth state.");
        }
    }
    if (mode_ == Mode::Add) {
        sync_add_transport_row();
    } else if (mode_ == Mode::Edit) {
        sync_scope_enabled();
    }
}

void McpSettingsUi::fill_templates() {
    if (!add_template_) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    for (const auto& t : McpManager::known_templates()) {
        items.push_back({utf16(t.display_name), 0});
    }
    items.push_back({L"Custom", 0});
    ui_kit::select_set_items(add_template_, items);
    ui_kit::select_set_index(add_template_, 0);
}

void McpSettingsUi::fill_transport(HWND select, McpTransportKind kind) {
    if (!select) {
        return;
    }
    // Index order must match transport_of(): 0 = HTTP, 1 = stdio.
    ui_kit::select_set_items(select, {{L"HTTP (remote)", 0}, {L"Stdio (local process)", 1}});
    ui_kit::select_set_index(select, kind == McpTransportKind::Stdio ? 1 : 0);
}

McpTransportKind McpSettingsUi::transport_of(HWND select) const {
    return ui_kit::select_get_index(select) == 1 ? McpTransportKind::Stdio : McpTransportKind::Http;
}

bool McpSettingsUi::add_is_custom() const {
    const int sel = ui_kit::select_get_index(add_template_);
    return sel < 0 || sel >= static_cast<int>(McpManager::known_templates().size());
}

void McpSettingsUi::sync_add_transport_row() {
    // Known templates pin their own transport; only Custom lets the user choose.
    const bool custom = add_is_custom();
    if (add_transport_) {
        EnableWindow(add_transport_, custom ? TRUE : FALSE);
        InvalidateRect(add_transport_, nullptr, TRUE);
    }
    sync_endpoint_labels(add_lbl_endpoint_, add_endpoint_, transport_of(add_transport_));
}

void McpSettingsUi::sync_endpoint_labels(HWND label, HWND field, McpTransportKind kind) {
    const bool stdio = kind == McpTransportKind::Stdio;
    if (label) {
        SetWindowTextW(label, stdio ? L"Command" : L"Endpoint URL");
        InvalidateRect(label, nullptr, TRUE);
    }
    if (field) {
        ui_kit::set_placeholder(field, stdio ? L"C:\\path\\to\\server.exe --stdio" : L"https://mcp.example.com/mcp");
        InvalidateRect(field, nullptr, TRUE);
    }
}

void McpSettingsUi::fill_scope_list(const std::vector<std::string>& scope) {
    scope_projects_ = project_provider_ ? project_provider_() : ProjectList{};
    scope_checked_.assign(scope_projects_.size(), false);
    for (std::size_t i = 0; i < scope_projects_.size(); ++i) {
        for (const auto& pid : scope) {
            if (pid == scope_projects_[i].first) {
                scope_checked_[i] = true;
                break;
            }
        }
    }
    // A scoped id the workspace no longer lists (project closed elsewhere) still gets a row, so
    // saving the form cannot silently widen the connection to every project.
    for (const auto& pid : scope) {
        bool known = false;
        for (const auto& p : scope_projects_) {
            if (p.first == pid) {
                known = true;
                break;
            }
        }
        if (!known) {
            scope_projects_.emplace_back(pid, utf16(pid) + L"  (not currently open)");
            scope_checked_.push_back(true);
        }
    }
    if (!edit_scope_list_) {
        return;
    }
    SendMessageW(edit_scope_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& p : scope_projects_) {
        SendMessageW(edit_scope_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.second.c_str()));
    }
    if (scope_projects_.empty()) {
        // Never leave a blank box: say why the picker is empty instead.
        SendMessageW(edit_scope_list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No projects registered yet — open a folder first"));
    }
    InvalidateRect(edit_scope_list_, nullptr, TRUE);
}

void McpSettingsUi::sync_scope_enabled() {
    if (scope_projects_.empty() && edit_scope_all_) {
        // Nothing to narrow to — "all projects" is the only valid answer, so do not let the user
        // uncheck it into a state Save would reject.
        SendMessageW(edit_scope_all_, BM_SETCHECK, BST_CHECKED, 0);
        EnableWindow(edit_scope_all_, FALSE);
        InvalidateRect(edit_scope_all_, nullptr, TRUE);
    } else if (edit_scope_all_) {
        EnableWindow(edit_scope_all_, TRUE);
    }
    const bool all = edit_scope_all_ && SendMessageW(edit_scope_all_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (edit_scope_list_) {
        EnableWindow(edit_scope_list_, (!all && !scope_projects_.empty()) ? TRUE : FALSE);
        InvalidateRect(edit_scope_list_, nullptr, TRUE);
    }
    if (edit_lbl_scope_) {
        SetWindowTextW(edit_lbl_scope_, all ? L"Project scope — all projects"
                                            : L"Project scope — click a project to include it");
        InvalidateRect(edit_lbl_scope_, nullptr, TRUE);
    }
}

std::vector<std::string> McpSettingsUi::collect_scope() const {
    std::vector<std::string> out;
    if (edit_scope_all_ && SendMessageW(edit_scope_all_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return out;  // empty = all projects
    }
    for (std::size_t i = 0; i < scope_projects_.size() && i < scope_checked_.size(); ++i) {
        if (scope_checked_[i]) {
            out.push_back(scope_projects_[i].first);
        }
    }
    return out;
}

void McpSettingsUi::begin_edit(const McpConnection& c) {
    edit_id_ = c.id;
    SetWindowTextW(edit_name_, c.connection_name.empty() ? c.account_label.c_str() : c.connection_name.c_str());
    SetWindowTextW(edit_alias_, utf16(c.agent_alias).c_str());
    fill_transport(edit_transport_, c.transport_kind);
    SetWindowTextW(edit_endpoint_, c.endpoint_or_cmd.c_str());
    sync_endpoint_labels(edit_lbl_endpoint_, edit_endpoint_, c.transport_kind);
    if (edit_enabled_) {
        SendMessageW(edit_enabled_, BM_SETCHECK, c.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (edit_scope_all_) {
        SendMessageW(edit_scope_all_, BM_SETCHECK, c.project_scope.empty() ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    fill_scope_list(c.project_scope);
    sync_scope_enabled();
}

std::wstring McpSettingsUi::auth_badge(McpAuthState s) const {
    switch (s) {
        case McpAuthState::Healthy:
            return L"Healthy";
        case McpAuthState::Expired:
            return L"Expired";
        case McpAuthState::NeedsReauth:
            return L"Needs reauth";
        case McpAuthState::Offline:
            return L"Offline";
        case McpAuthState::Disabled:
            return L"Disabled";
        case McpAuthState::Unknown:
        default:
            return L"Unknown";
    }
}

std::wstring McpSettingsUi::row_primary(const McpConnection& c) const {
    std::wstring line;
    line += c.display_name.empty() ? utf16(c.service_id) : c.display_name;
    line += L"  ·  ";
    line += c.connection_name.empty() ? (c.account_label.empty() ? L"(unnamed)" : c.account_label) : c.connection_name;
    return line;
}

std::wstring McpSettingsUi::row_secondary(const McpConnection& c) const {
    std::wstring line;
    if (!c.agent_alias.empty()) {
        line += L"@";
        line += utf16(c.agent_alias);
        line += L"  ·  ";
    }
    if (!c.enabled) {
        line += auth_badge(McpAuthState::Disabled);
    } else {
        line += auth_badge(c.auth_state);
    }
    return line;
}

void McpSettingsUi::layout(const RECT& area) {
    area_ = area;
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    const int x0 = col.left + m.pad_outer;
    const int y0 = col.top + m.pad_outer;
    const int x1 = col.right - m.pad_outer;
    const int y1 = col.bottom - m.pad_outer;
    const int w = (std::max)(1, x1 - x0);

    int y = y0;
    MoveWindow(heading_, x0, y, w, dip(parent_, ui_space::kPageHeaderTitleHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderTitleHDip) + m.pad_tight;
    MoveWindow(desc_, x0, y, w, dip(parent_, ui_space::kPageHeaderDescHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderDescHDip) + m.pad_row;

    // place_labeled_row centers the label against the field mid-line; the old hand-rolled
    // MoveWindow here stretched the label to full row height and top-aligned its text.
    auto place_row = [&](HWND label, HWND field) {
        ui_space::place_labeled_row(label, field, col, y, m);
        ui_kit::center_field_text(field);
        y += m.row_h + m.pad_row;
    };
    const int field_x = x0 + m.label_w + m.pad_tight;

    if (mode_ == Mode::Add) {
        place_row(add_lbl_template_, add_template_);
        place_row(add_lbl_name_, add_name_);
        place_row(add_lbl_alias_, add_alias_);
        place_row(add_lbl_transport_, add_transport_);
        place_row(add_lbl_endpoint_, add_endpoint_);
        MoveWindow(btn_add_save_, field_x, y, m.btn_w, m.btn_h, TRUE);
        MoveWindow(btn_add_cancel_, field_x + m.btn_w + m.pad_tight, y, m.btn_w, m.btn_h, TRUE);
        ShowWindow(btn_add_save_, SW_SHOW);
        ShowWindow(btn_add_cancel_, SW_SHOW);
        return;
    }

    if (mode_ == Mode::Edit) {
        place_row(edit_lbl_name_, edit_name_);
        place_row(edit_lbl_alias_, edit_alias_);
        place_row(edit_lbl_transport_, edit_transport_);
        place_row(edit_lbl_endpoint_, edit_endpoint_);
        MoveWindow(edit_enabled_, field_x, y, (std::max)(1, x1 - field_x), m.row_h, TRUE);
        y += m.row_h + m.pad_tight;
        MoveWindow(edit_scope_all_, field_x, y, (std::max)(1, x1 - field_x), m.row_h, TRUE);
        y += m.row_h + m.pad_tight;
        MoveWindow(edit_lbl_scope_, x0, y, w, dip(parent_, ui_space::kPageHeaderDescHDip), TRUE);
        y += dip(parent_, ui_space::kPageHeaderDescHDip) + m.pad_tight;
        // Buttons anchor to the bottom so the scope list absorbs whatever height is left instead of
        // pushing Save off-panel on short windows.
        const int actions_y = y1 - m.btn_h;
        const int list_h = (std::max)(dip(parent_, 64), actions_y - m.pad_row - y);
        MoveWindow(edit_scope_list_, x0, y, w, list_h, TRUE);
        MoveWindow(btn_edit_save_, x0, actions_y, m.btn_w, m.btn_h, TRUE);
        MoveWindow(btn_edit_cancel_, x0 + m.btn_w + m.pad_tight, actions_y, m.btn_w, m.btn_h, TRUE);
        show_many(edit_controls(), SW_SHOW);
        return;
    }

    // Actions anchor to the bottom edge, then list + detail share what is left. Previously the
    // detail pane was placed *after* the two button rows, so the buttons sat mid-panel and the
    // detail text ran off the bottom on short windows.
    const int btn_row_h = m.btn_h;
    const int btn_gap = m.pad_tight;
    const int actions_h = btn_row_h * 2 + btn_gap;
    const int body_top = y;
    const int actions_y = y1 - actions_h;
    const int body_h = (std::max)(dip(parent_, 120), actions_y - m.pad_row - body_top);
    const int detail_h = (std::max)(dip(parent_, 72), body_h * 35 / 100);
    const int list_h = (std::max)(dip(parent_, 88), body_h - detail_h - m.pad_row);

    MoveWindow(list_, x0, body_top, w, list_h, TRUE);
    ShowWindow(list_, SW_SHOW);
    MoveWindow(detail_, x0, body_top + list_h + m.pad_row, w, detail_h, TRUE);
    ui_kit::center_field_text(detail_);
    ShowWindow(detail_, SW_SHOW);

    const HWND row1[] = {btn_add_, btn_add_account_, btn_manage_, btn_reauth_};
    const HWND row2[] = {btn_check_, btn_test_, btn_disable_, btn_disconnect_, btn_remove_};
    auto place_btns = [&](const HWND* btns, int count, int by) {
        int x = x0;
        for (int i = 0; i < count; ++i) {
            int bw = m.btn_w;
            if (btns == row1 && i == 1) {
                bw = dip(parent_, 160);
            } else if (btns == row2 && i == 1) {
                bw = dip(parent_, 140);  // "Test Connection" needs more than the 104dip default
            }
            MoveWindow(btns[i], x, by, bw, btn_row_h, TRUE);
            ShowWindow(btns[i], SW_SHOW);
            x += bw + btn_gap;
        }
    };
    place_btns(row1, 4, actions_y);
    place_btns(row2, 5, actions_y + btn_row_h + btn_gap);
}

void McpSettingsUi::select_connection(const std::string& id, McpManager& mgr) {
    selected_id_ = id;
    refresh(mgr);
}

void McpSettingsUi::refresh(McpManager& mgr) {
    if (!list_) {
        return;
    }
    const int prev = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    row_ids_.clear();
    row_primary_.clear();
    row_secondary_.clear();
    int restore = -1;
    for (const auto& c : mgr.connections()) {
        row_primary_.push_back(row_primary(c));
        row_secondary_.push_back(row_secondary(c));
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row_primary_.back().c_str()));
        if (c.id == selected_id_) {
            restore = static_cast<int>(row_ids_.size());
        }
        row_ids_.push_back(c.id);
    }
    if (restore < 0 && prev >= 0 && prev < static_cast<int>(row_ids_.size())) {
        restore = prev;
    }
    if (restore < 0 && !row_ids_.empty()) {
        restore = 0;
    }
    if (restore >= 0) {
        SendMessageW(list_, LB_SETCURSEL, restore, 0);
        selected_id_ = row_ids_[static_cast<std::size_t>(restore)];
    } else {
        selected_id_.clear();
    }
    if (row_ids_.empty()) {
        // Unselectable placeholder row: row_ids_ stays empty so selected() reports none and the
        // per-connection buttons remain disabled. Without this the list read as a broken blank box.
        row_primary_.push_back(L"No MCP connections yet");
        row_secondary_.push_back(L"Choose Add MCP to connect a service and give it an @alias.");
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row_primary_.back().c_str()));
    }
    update_detail(mgr);
    update_button_state(mgr);
    InvalidateRect(list_, nullptr, TRUE);
}

McpConnection* McpSettingsUi::selected(McpManager& mgr) {
    return mgr.by_id(selected_id_);
}

const McpConnection* McpSettingsUi::selected(const McpManager& mgr) const {
    return mgr.by_id(selected_id_);
}

void McpSettingsUi::update_detail(const McpManager& mgr) {
    if (!detail_) {
        return;
    }
    const McpConnection* c = selected(mgr);
    if (!c) {
        SetWindowTextW(detail_, L"Select an MCP connection, or choose Add MCP.");
        return;
    }
    std::wstring d;
    d += L"Service: ";
    d += c->display_name.empty() ? utf16(c->service_id) : c->display_name;
    d += L"\r\nConnection: ";
    d += c->connection_name.empty() ? L"(unnamed)" : c->connection_name;
    d += L"\r\nAlias: ";
    d += c->agent_alias.empty() ? L"(none)" : (L"@" + utf16(c->agent_alias));
    d += L"\r\nTransport: ";
    d += utf16(mcp_transport_name(c->transport_kind));
    d += c->transport_kind == McpTransportKind::Stdio ? L"\r\nCommand: " : L"\r\nEndpoint: ";
    d += c->endpoint_or_cmd.empty() ? L"(unset)" : c->endpoint_or_cmd;
    if (c->transport_kind == McpTransportKind::Stdio) {
        d += L"\r\nArguments: ";
        if (c->arguments.empty()) d += L"(none)";
        for (size_t i = 0; i < c->arguments.size(); ++i) {
            if (i) d += L" ";
            d += c->arguments[i];
        }
        d += L"\r\nEnvironment: ";
        if (c->environment.empty()) d += L"(none)";
        for (size_t i = 0; i < c->environment.size(); ++i) {
            if (i) d += L", ";
            d += c->environment[i].first + L"=" + c->environment[i].second;
        }
    }
    d += L"\r\nProjects: ";
    if (c->project_scope.empty()) {
        d += L"All projects";
    } else {
        // Show names when the workspace knows them; ids alone read as noise.
        const ProjectList known = project_provider_ ? project_provider_() : ProjectList{};
        bool first = true;
        for (const auto& pid : c->project_scope) {
            std::wstring label = utf16(pid);
            for (const auto& p : known) {
                if (p.first == pid) {
                    label = p.second;
                    break;
                }
            }
            if (!first) {
                d += L", ";
            }
            d += label;
            first = false;
        }
    }
    d += L"\r\nAuth: ";
    d += auth_badge(c->enabled ? c->auth_state : McpAuthState::Disabled);
    if (!c->last_error.empty()) {
        d += L"\r\nError: ";
        d += utf16(c->last_error);
    }
    SetWindowTextW(detail_, d.c_str());
}

void McpSettingsUi::update_button_state(const McpManager& mgr) {
    const McpConnection* c = selected(mgr);
    const BOOL has = c ? TRUE : FALSE;
    EnableWindow(btn_add_account_, has);
    EnableWindow(btn_manage_, has);
    EnableWindow(btn_reauth_, has);
    EnableWindow(btn_check_, has);
    EnableWindow(btn_test_, has);
    EnableWindow(btn_disable_, has);
    EnableWindow(btn_disconnect_, has);
    EnableWindow(btn_remove_, has);
    if (c && btn_disable_) {
        SetWindowTextW(btn_disable_, c->enabled ? L"Disable" : L"Enable");
        InvalidateRect(btn_disable_, nullptr, TRUE);
    }
}

bool McpSettingsUi::persist(McpManager& mgr, const std::wstring& mcp_path) {
    if (mcp_path.empty()) {
        return false;
    }
    // Every caller here mutates state the user just saw change. A silent write failure meant the
    // enable/disable/auth badge held until restart and then snapped back with no explanation.
    if (!mgr.save(mcp_path)) {
        ui_kit::report_save_failure(parent_, L"MCP connections", mcp_path);
        return false;
    }
    return true;
}

bool McpSettingsUi::commit_add(McpManager& mgr, const std::wstring& mcp_path, HWND owner) {
    const int sel = ui_kit::select_get_index(add_template_);
    const auto templates = McpManager::known_templates();
    const std::wstring name = get_text(add_name_);
    const std::wstring alias_w = get_text(add_alias_);
    const std::wstring endpoint = get_text(add_endpoint_);

    if (name.empty()) {
        MessageBoxW(owner, L"Connection name is required.", L"Add MCP", MB_OK | MB_ICONWARNING);
        return false;
    }

    McpConnection c;
    if (sel >= 0 && sel < static_cast<int>(templates.size())) {
        c = McpManager::from_template(templates[static_cast<std::size_t>(sel)], name);
        c.connection_name = name;
        c.account_label = name;
    } else {
        c.service_id = "custom";
        c.display_name = L"Custom";
        c.connection_name = name;
        c.account_label = name;
        // Custom used to be hard-coded to HTTP, so a local stdio server saved as an HTTP endpoint
        // and then failed OAuth. Honour the transport the user picked.
        c.transport_kind = transport_of(add_transport_);
        c.auth_state = McpAuthState::Unknown;
    }
    if (!endpoint.empty()) {
        c.endpoint_or_cmd = endpoint;
    }
    if (c.transport_kind == McpTransportKind::Stdio && c.endpoint_or_cmd.empty()) {
        MessageBoxW(owner, L"A stdio connection needs a command to launch.", L"Add MCP", MB_OK | MB_ICONWARNING);
        return false;
    }

    const std::string alias = utf8(alias_w);
    std::string err;
    if (McpConnection* added = mgr.add(c)) {
        if (!alias.empty()) {
            if (!mgr.set_alias(added->id, alias, &err)) {
                mgr.remove(added->id);
                MessageBoxW(owner, utf16(err.empty() ? "Invalid alias" : err).c_str(), L"Add MCP",
                            MB_OK | MB_ICONWARNING);
                return false;
            }
        }
        selected_id_ = added->id;
        persist(mgr, mcp_path);
        // HTTP templates usually need OAuth before they are usable — offer it immediately.
        if (added->transport_kind == McpTransportKind::Http && !added->endpoint_or_cmd.empty()) {
            const int go = MessageBoxW(owner,
                                       L"Connection saved.\n\n"
                                       L"Sign in with the browser now so this account is ready to use?",
                                       L"Add MCP", MB_YESNO | MB_ICONQUESTION);
            if (go == IDYES) {
                MessageBoxW(owner,
                            L"A browser window will open so you can sign in.\n\n"
                            L"After you approve access, return here.",
                            L"Add MCP", MB_OK | MB_ICONINFORMATION);
                const McpOAuthResult auth = mcp_oauth_authorize(owner, added->endpoint_or_cmd, added->id);
                added->disconnected = false;
                mgr.mark_auth(added->id, auth.state, auth.message);
                persist(mgr, mcp_path);
                if (!auth.ok) {
                    MessageBoxW(owner,
                                utf16(auth.message.empty()
                                          ? "Sign-in did not complete. Use Reauthenticate later."
                                          : auth.message)
                                    .c_str(),
                                L"Add MCP", MB_OK | MB_ICONWARNING);
                }
            } else {
                mgr.mark_auth(added->id, McpAuthState::NeedsReauth, "Sign-in deferred — use Reauthenticate");
                persist(mgr, mcp_path);
            }
        }
        set_mode(Mode::Browse);
        refresh(mgr);
        if (!IsRectEmpty(&area_)) {
            layout(area_);
        }
        return true;
    }
    MessageBoxW(owner, L"Could not add MCP connection (duplicate id or alias).", L"Add MCP", MB_OK | MB_ICONWARNING);
    return false;
}

bool McpSettingsUi::commit_edit(McpManager& mgr, const std::wstring& mcp_path, HWND owner) {
    McpConnection* c = mgr.by_id(edit_id_);
    if (!c) {
        set_mode(Mode::Browse);
        return false;
    }
    const std::wstring name = get_text(edit_name_);
    const std::wstring endpoint = get_text(edit_endpoint_);
    const std::string alias = utf8(get_text(edit_alias_));
    const McpTransportKind transport = transport_of(edit_transport_);

    if (name.empty()) {
        MessageBoxW(owner, L"Connection name is required.", L"Manage MCP", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (transport == McpTransportKind::Stdio && endpoint.empty()) {
        MessageBoxW(owner, L"A stdio connection needs a command to launch.", L"Manage MCP", MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool scope_all = edit_scope_all_ && SendMessageW(edit_scope_all_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const std::vector<std::string> scope = collect_scope();
    if (!scope_all && scope.empty()) {
        // An empty list means "all projects" on disk, so saving it here would silently contradict
        // the unchecked box the user is looking at.
        MessageBoxW(owner,
                    L"Pick at least one project, or check \"Available to all projects\".",
                    L"Manage MCP", MB_OK | MB_ICONWARNING);
        return false;
    }

    // Alias first: it is the only field that can be rejected, and a half-applied save would leave
    // the form showing values the store never accepted.
    const std::string prev_alias = c->agent_alias;
    std::string err;
    if (!mgr.set_alias(c->id, alias, &err)) {
        MessageBoxW(owner, utf16(err.empty() ? "Invalid alias" : err).c_str(), L"Manage MCP", MB_OK | MB_ICONWARNING);
        return false;
    }
    c = mgr.by_id(edit_id_);
    if (!c) {
        return false;
    }

    c->connection_name = name;
    c->account_label = name;
    c->transport_kind = transport;
    c->endpoint_or_cmd = endpoint;
    c->enabled = edit_enabled_ && SendMessageW(edit_enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    c->project_scope = scope;
    if (!c->enabled) {
        c->auth_state = McpAuthState::Disabled;
    } else if (c->auth_state == McpAuthState::Disabled) {
        c->auth_state = McpAuthState::Unknown;
    }

    if (!persist(mgr, mcp_path)) {
        mgr.set_alias(c->id, prev_alias, nullptr);
        return false;
    }
    selected_id_ = edit_id_;
    edit_id_.clear();
    set_mode(Mode::Browse);
    refresh(mgr);
    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
    return true;
}

void McpSettingsUi::run_test(McpManager& mgr, const std::wstring& mcp_path, HWND owner) {
    McpConnection* c = selected(mgr);
    if (!c) {
        return;
    }
    if (c->endpoint_or_cmd.empty()) {
        mgr.mark_auth(c->id, McpAuthState::Offline,
                      c->transport_kind == McpTransportKind::Stdio ? "Command is empty" : "Endpoint is empty");
        persist(mgr, mcp_path);
        refresh(mgr);
        MessageBoxW(owner,
                    c->transport_kind == McpTransportKind::Stdio
                        ? L"Set a command before testing this connection."
                        : L"Set an endpoint URL before testing this connection.",
                    L"Test Connection", MB_OK | MB_ICONWARNING);
        return;
    }
    if (c->transport_kind == McpTransportKind::Stdio) {
        // Config validation only — launching the process is out of scope until the client lands.
        mgr.mark_auth(c->id, McpAuthState::Healthy, {});
        persist(mgr, mcp_path);
        refresh(mgr);
        std::wstring msg = L"Command is set:\n\n";
        msg += c->endpoint_or_cmd;
        msg += L"\n\nLaunching the stdio server to verify it responds comes later; for now only the "
               L"configuration is checked.";
        MessageBoxW(owner, msg.c_str(), L"Test Connection", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const McpOAuthResult check = mcp_oauth_check_now(c->endpoint_or_cmd, c->id);
    mgr.mark_auth(c->id, check.state, check.message);
    persist(mgr, mcp_path);
    refresh(mgr);
    std::wstring msg = L"Endpoint: ";
    msg += c->endpoint_or_cmd;
    msg += L"\r\nResult: ";
    msg += auth_badge(check.state);
    if (!check.message.empty()) {
        msg += L"\r\n";
        msg += utf16(check.message);
    }
    if (check.state == McpAuthState::NeedsReauth || check.state == McpAuthState::Expired) {
        msg += L"\r\n\r\nUse Reauthenticate to sign in again.";
    }
    MessageBoxW(owner, msg.c_str(), L"Test Connection",
                MB_OK | (check.ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

bool McpSettingsUi::handle_command(int id, WORD notify, McpManager& mgr, const std::wstring& mcp_path, HWND owner) {
    if (!visible_) {
        return false;
    }
    if (id == id_add_template_ && ui_kit::select_handle_command(add_template_, notify)) {
        return true;
    }
    if (id == static_cast<int>(IdAddTransport)) {
        if (ui_kit::select_handle_command(add_transport_, notify)) {
            return true;
        }
        if (notify == CBN_SELCHANGE) {
            sync_endpoint_labels(add_lbl_endpoint_, add_endpoint_, transport_of(add_transport_));
            return true;
        }
        return false;
    }
    if (id == static_cast<int>(IdEditTransport)) {
        if (ui_kit::select_handle_command(edit_transport_, notify)) {
            return true;
        }
        if (notify == CBN_SELCHANGE) {
            sync_endpoint_labels(edit_lbl_endpoint_, edit_endpoint_, transport_of(edit_transport_));
            return true;
        }
        return false;
    }
    if (id == static_cast<int>(IdEditEnabled) && notify == BN_CLICKED) {
        return true;  // checkbox subclass already toggled; value is read on Save
    }
    if (id == static_cast<int>(IdEditScopeAll) && notify == BN_CLICKED) {
        sync_scope_enabled();
        return true;
    }
    if (id == static_cast<int>(IdEditScopeList)) {
        if (notify != LBN_SELCHANGE && notify != LBN_DBLCLK) {
            return false;
        }
        // Selection is the click target, not the value: a click toggles that project's checkbox and
        // the listbox selection is dropped so the row reads as a checkbox list, not a single-pick.
        const int sel = static_cast<int>(SendMessageW(edit_scope_list_, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(scope_checked_.size())) {
            scope_checked_[static_cast<std::size_t>(sel)] = !scope_checked_[static_cast<std::size_t>(sel)];
            InvalidateRect(edit_scope_list_, nullptr, TRUE);
        }
        return true;
    }
    if (id == static_cast<int>(IdEditSave)) {
        commit_edit(mgr, mcp_path, owner);
        return true;
    }
    if (id == static_cast<int>(IdEditCancel)) {
        edit_id_.clear();
        set_mode(Mode::Browse);
        refresh(mgr);
        if (!IsRectEmpty(&area_)) {
            layout(area_);
        }
        return true;
    }
    if (id == id_list_) {
        // Only selection events. Accepting every notify (LBN_SETFOCUS / LBN_KILLFOCUS /
        // LBN_SELCANCEL) made mere focus movement report "handled" and trigger a full relayout.
        if (notify != LBN_SELCHANGE && notify != LBN_DBLCLK) {
            return false;
        }
        const int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(row_ids_.size())) {
            selected_id_ = row_ids_[static_cast<std::size_t>(sel)];
        } else {
            selected_id_.clear();
        }
        update_detail(mgr);
        update_button_state(mgr);
        // Double-click opens the edit form as if Manage had been pressed.
        if (notify == LBN_DBLCLK && !selected_id_.empty()) {
            if (const McpConnection* row = selected(mgr)) {
                begin_edit(*row);
                set_mode(Mode::Edit);
                if (!IsRectEmpty(&area_)) {
                    layout(area_);
                }
            }
        }
        return true;
    }
    if (id == id_btn_add_ || id == id_btn_add_account_) {
        fill_templates();
        SetWindowTextW(add_name_, L"");
        SetWindowTextW(add_alias_, L"");
        SetWindowTextW(add_endpoint_, L"");
        fill_transport(add_transport_, McpTransportKind::Http);
        if (id == id_btn_add_account_) {
            if (const McpConnection* c = selected(mgr)) {
                const auto templates = McpManager::known_templates();
                for (std::size_t i = 0; i < templates.size(); ++i) {
                    if (templates[i].service_id == c->service_id) {
                        ui_kit::select_set_index(add_template_, static_cast<int>(i));
                        SetWindowTextW(add_endpoint_, c->endpoint_or_cmd.c_str());
                        break;
                    }
                }
                fill_transport(add_transport_, c->transport_kind);
            }
        } else {
            ui_kit::select_set_index(add_template_, 0);
            const auto templates = McpManager::known_templates();
            if (!templates.empty()) {
                SetWindowTextW(add_endpoint_, utf16(templates[0].suggested_endpoint_or_cmd).c_str());
                fill_transport(add_transport_, templates[0].default_transport);
            }
        }
        set_mode(Mode::Add);
        if (!IsRectEmpty(&area_)) {
            layout(area_);
        }
        return true;
    }
    if (id == id_add_cancel_) {
        set_mode(Mode::Browse);
        if (!IsRectEmpty(&area_)) {
            layout(area_);
        }
        return true;
    }
    if (id == id_add_save_) {
        commit_add(mgr, mcp_path, owner);
        return true;
    }
    if (id == id_add_template_ && (notify == CBN_SELCHANGE || notify == BN_CLICKED)) {
        const int sel = ui_kit::select_get_index(add_template_);
        const auto templates = McpManager::known_templates();
        if (sel >= 0 && sel < static_cast<int>(templates.size())) {
            SetWindowTextW(add_endpoint_, utf16(templates[static_cast<std::size_t>(sel)].suggested_endpoint_or_cmd).c_str());
            fill_transport(add_transport_, templates[static_cast<std::size_t>(sel)].default_transport);
        } else {
            SetWindowTextW(add_endpoint_, L"");
        }
        sync_add_transport_row();
        return true;
    }

    McpConnection* c = selected(mgr);
    if (!c) {
        return id == id_btn_manage_ || id == id_btn_reauth_ || id == id_btn_check_ ||
               id == static_cast<int>(IdTest) || id == id_btn_disable_ || id == id_btn_disconnect_ ||
               id == id_btn_remove_;
    }

    if (id == id_btn_manage_) {
        begin_edit(*c);
        set_mode(Mode::Edit);
        if (!IsRectEmpty(&area_)) {
            layout(area_);
        }
        return true;
    }
    if (id == static_cast<int>(IdTest)) {
        run_test(mgr, mcp_path, owner);
        return true;
    }
    if (id == id_btn_reauth_) {
        if (c->transport_kind != McpTransportKind::Http) {
            MessageBoxW(owner,
                        L"Browser OAuth applies to HTTP MCP connections.\n"
                        L"stdio servers take credentials from their environment.",
                        L"Reauthenticate", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        if (c->endpoint_or_cmd.empty()) {
            MessageBoxW(owner, L"Set an endpoint before signing in.", L"Reauthenticate",
                        MB_OK | MB_ICONWARNING);
            return true;
        }
        MessageBoxW(owner,
                    L"A browser window will open so you can sign in.\n\n"
                    L"After you approve access, return here — Scylla waits for the callback.",
                    L"Reauthenticate", MB_OK | MB_ICONINFORMATION);
        mgr.mark_auth(c->id, McpAuthState::Unknown, "Waiting for browser sign-in…");
        refresh(mgr);
        const McpOAuthResult auth = mcp_oauth_authorize(owner, c->endpoint_or_cmd, c->id);
        c->disconnected = false;
        mgr.mark_auth(c->id, auth.state, auth.message);
        persist(mgr, mcp_path);
        refresh(mgr);
        if (!auth.ok) {
            MessageBoxW(owner, utf16(auth.message.empty() ? "Sign-in failed." : auth.message).c_str(),
                        L"Reauthenticate", MB_OK | MB_ICONWARNING);
        }
        return true;
    }
    if (id == id_btn_check_) {
        if (c->endpoint_or_cmd.empty()) {
            mgr.mark_auth(c->id, McpAuthState::Offline, "Endpoint / command is empty");
        } else if (c->disconnected) {
            mgr.mark_auth(c->id, McpAuthState::NeedsReauth, "Disconnected — reauthenticate before use");
        } else if (!c->enabled) {
            mgr.mark_auth(c->id, McpAuthState::Disabled, "Connection disabled");
        } else if (c->transport_kind == McpTransportKind::Http) {
            const McpOAuthResult check = mcp_oauth_check_now(c->endpoint_or_cmd, c->id);
            mgr.mark_auth(c->id, check.state, check.message);
        } else {
            // stdio: configuration presence only until a live process probe exists.
            mgr.mark_auth(c->id, McpAuthState::Healthy, {});
        }
        persist(mgr, mcp_path);
        refresh(mgr);
        return true;
    }
    if (id == id_btn_disable_) {
        mgr.set_enabled(c->id, !c->enabled);
        persist(mgr, mcp_path);
        refresh(mgr);
        return true;
    }
    if (id == id_btn_disconnect_) {
        mgr.disconnect(c->id);
        persist(mgr, mcp_path);
        refresh(mgr);
        return true;
    }
    if (id == id_btn_remove_) {
        std::wstring named = c->connection_name.empty() ? c->display_name : c->connection_name;
        if (!c->agent_alias.empty()) {
            named += L" (@";
            named += utf16(c->agent_alias);
            named += L")";
        }
        if (ui_kit::confirm_destructive(owner, L"Remove", named.c_str(),
                                        L"The connection is removed from Scylla Workbench and its alias stops "
                                        L"resolving for agents. The external service account is not deleted.")) {
            mgr.remove(c->id);
            selected_id_.clear();
            persist(mgr, mcp_path);
            refresh(mgr);
        }
        return true;
    }
    return false;
}

bool McpSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || mi->CtlType != ODT_LISTBOX) {
        return false;
    }
    if (mi->CtlID == IdEditScopeList) {
        mi->itemHeight = dip(parent_ ? parent_ : GetDesktopWindow(), 28);
        return true;
    }
    if (mi->CtlID != static_cast<UINT>(id_list_)) {
        return false;
    }
    mi->itemHeight = dip(parent_ ? parent_ : GetDesktopWindow(), 40);
    return true;
}

bool McpSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di || di->CtlType != ODT_LISTBOX) {
        return false;
    }
    if (di->hwndItem == edit_scope_list_ && edit_scope_list_) {
        const Theme& t = theme();
        fill_rect(di->hDC, di->rcItem, t.panel);
        if (di->itemID == static_cast<UINT>(-1)) {
            return true;
        }
        const bool disabled = IsWindowEnabled(edit_scope_list_) == FALSE;
        const int h = static_cast<int>(di->rcItem.bottom - di->rcItem.top);
        const int s = dip(parent_ ? parent_ : GetDesktopWindow(), 14);
        const int gx = di->rcItem.left + dip(parent_ ? parent_ : GetDesktopWindow(), 12);
        const int gy = di->rcItem.top + (std::max)(0, (h - s) / 2);
        const bool checked = di->itemID < scope_checked_.size() && scope_checked_[di->itemID];
        const bool has_projects = !scope_projects_.empty();
        if (has_projects) {
            ui_kit::draw_checkbox_glyph(di->hDC, gx, gy, s, checked, false, disabled);
        }
        RECT tr = di->rcItem;
        tr.left = has_projects ? gx + s + dip(parent_ ? parent_ : GetDesktopWindow(), 8) : gx;
        tr.right -= dip(parent_ ? parent_ : GetDesktopWindow(), 12);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, (disabled || !has_projects) ? t.muted : t.text);
        if (font) {
            SelectObject(di->hDC, font);
        }
        const std::wstring label = di->itemID < scope_projects_.size() ? scope_projects_[di->itemID].second
                                                                       : L"No projects registered yet — open a folder first";
        DrawTextW(di->hDC, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return true;
    }
    if (di->hwndItem != list_) {
        return false;
    }
    if (di->itemID == static_cast<UINT>(-1)) {
        fill_rect(di->hDC, di->rcItem, theme().panel);
        return true;
    }
    // The empty-state placeholder lives past the end of row_ids_; never give it a selection strip.
    const bool placeholder = row_ids_.empty();
    const bool sel = !placeholder && (di->itemState & ODS_SELECTED) != 0;
    const wchar_t* primary = L"";
    const wchar_t* secondary = L"";
    if (di->itemID < row_primary_.size()) {
        primary = row_primary_[di->itemID].c_str();
        secondary = row_secondary_[di->itemID].c_str();
    }
    ui_kit::paint_entity_row(di->hDC, di->rcItem, font, primary, secondary, sel, false);
    return true;
}

}  // namespace scyllagpt
