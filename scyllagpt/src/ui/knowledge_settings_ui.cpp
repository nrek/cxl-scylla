#include "scyllagpt/knowledge_settings_ui.h"

#include "scyllagpt/store.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <shobjidl.h>

#include <algorithm>

namespace scyllagpt {
namespace {

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd ? hwnd : GetDesktopWindow())), 96);
}

std::wstring shorten_path(const std::wstring& path, std::size_t max_chars = 56) {
    if (path.size() <= max_chars) {
        return path;
    }
    if (max_chars < 8) {
        return path.substr(0, max_chars);
    }
    return L"…" + path.substr(path.size() - (max_chars - 1));
}

std::wstring folder_leaf(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos + 1 >= path.size()) {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring get_field_text(HWND edit) {
    if (!edit) {
        return {};
    }
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(len), 0);
    GetWindowTextW(edit, text.data(), len + 1);
    return text;
}

// |child| expressed relative to |root|, or empty when it is the root itself or outside it.
std::wstring relative_within(const std::wstring& root, const std::wstring& child) {
    if (root.empty() || child.size() <= root.size()) {
        return {};
    }
    if (_wcsnicmp(child.c_str(), root.c_str(), root.size()) != 0) {
        return {};
    }
    if (child[root.size()] != L'\\' && child[root.size()] != L'/') {
        return {};
    }
    return normalize_override_path(child.substr(root.size() + 1));
}

}  // namespace

bool KnowledgeSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font) {
    if (!parent) {
        return false;
    }
    parent_ = parent;
    inst_ = inst;
    font_ = font;

    heading_ = ui_kit::create_static(parent, inst, IdHeading, L"Knowledge/Skills", font, false);
    desc_ = ui_kit::create_static(parent, inst, IdDesc,
                                  L"Folders outside the project for plans, skills, and instructions.", font, true);
    list_ = ui_kit::create_entity_list(parent, inst, IdList, font);
    empty_host_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_OWNERDRAW, 0, 0, 0, 0, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IdEmptyHost)), inst, nullptr);
    ui_kit::apply_control_chrome(empty_host_, font);

    btn_add_knowledge_ = ui_kit::create_button(parent, inst, IdAddKnowledge, L"Add Knowledge Folder",
                                               ui_kit::ButtonKind::Primary, font);
    btn_add_skills_ =
        ui_kit::create_button(parent, inst, IdAddSkills, L"Add Skills Folder", ui_kit::ButtonKind::Secondary, font);
    btn_edit_ = ui_kit::create_button(parent, inst, IdEdit, L"Edit", ui_kit::ButtonKind::Secondary, font);
    btn_refresh_ =
        ui_kit::create_button(parent, inst, IdRefreshHealth, L"Refresh", ui_kit::ButtonKind::Secondary, font);
    btn_remove_ = ui_kit::create_button(parent, inst, IdRemove, L"Remove", ui_kit::ButtonKind::Danger, font);

    lbl_name_ = ui_kit::create_static(parent, inst, IdLblName, L"Name", font, false);
    edit_name_ = ui_kit::create_text_field(parent, inst, IdName, font);
    lbl_type_ = ui_kit::create_static(parent, inst, IdLblType, L"Type", font, false);
    select_type_ = ui_kit::create_select(parent, inst, IdType, font);
    lbl_access_ = ui_kit::create_static(parent, inst, IdLblAccess, L"Access", font, false);
    select_access_ = ui_kit::create_select(parent, inst, IdAccess, font);
    lbl_alias_ = ui_kit::create_static(parent, inst, IdLblAlias, L"Alias", font, false);
    edit_alias_ = ui_kit::create_text_field(parent, inst, IdAlias, font);
    ui_kit::set_placeholder(edit_name_, L"Display name for this source");
    ui_kit::set_placeholder(edit_alias_, L"Agent alias without @ (optional)");
    check_agent_ = ui_kit::create_checkbox(parent, inst, IdAgent, L"Available to agent", font);
    check_enabled_ = ui_kit::create_checkbox(parent, inst, IdEnabled, L"Source enabled", font);
    detail_ = ui_kit::create_static(parent, inst, IdDetail, L"", font, true);
    btn_manage_perms_ = ui_kit::create_button(parent, inst, IdManagePerms, L"Manage Permissions",
                                              ui_kit::ButtonKind::Secondary, font);
    btn_save_ = ui_kit::create_button(parent, inst, IdSaveForm, L"Save", ui_kit::ButtonKind::Primary, font);
    btn_cancel_ = ui_kit::create_button(parent, inst, IdCancelForm, L"Cancel", ui_kit::ButtonKind::Secondary, font);

    override_list_ = ui_kit::create_entity_list(parent, inst, IdOverrideList, font);
    override_empty_host_ =
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_OWNERDRAW, 0, 0, 0, 0, parent,
                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IdOverrideEmptyHost)), inst, nullptr);
    ui_kit::apply_control_chrome(override_empty_host_, font);
    lbl_override_path_ = ui_kit::create_static(parent, inst, IdLblOverridePath, L"Folder", font, false);
    edit_override_path_ = ui_kit::create_text_field(parent, inst, IdOverridePath, font);
    ui_kit::set_placeholder(edit_override_path_, L"Path relative to the source root, e.g. private\\drafts");
    lbl_override_mode_ = ui_kit::create_static(parent, inst, IdLblOverrideMode, L"Permission", font, false);
    select_override_mode_ = ui_kit::create_select(parent, inst, IdOverrideMode, font);
    btn_override_browse_ =
        ui_kit::create_button(parent, inst, IdOverrideBrowse, L"Browse…", ui_kit::ButtonKind::Secondary, font);
    btn_override_add_ =
        ui_kit::create_button(parent, inst, IdOverrideAdd, L"Add override", ui_kit::ButtonKind::Primary, font);
    btn_override_remove_ =
        ui_kit::create_button(parent, inst, IdOverrideRemove, L"Remove", ui_kit::ButtonKind::Danger, font);
    btn_override_back_ = ui_kit::create_button(parent, inst, IdOverrideBack, L"Back", ui_kit::ButtonKind::Secondary,
                                               font);

    ui_kit::select_set_items(select_type_, {{L"Automatic", 0},
                                            {L"Knowledge", 1},
                                            {L"Skills", 2},
                                            {L"Instructions", 3}});
    ui_kit::select_set_index(select_type_, 0);
    ui_kit::select_set_items(select_access_, {{L"Read Only", 0}, {L"Read + Write", 1}});
    ui_kit::select_set_index(select_access_, 0);
    // Inherit is listed first because it is the default state for every folder that has no override.
    ui_kit::select_set_items(select_override_mode_, {{L"Inherit", static_cast<LPARAM>(AccessMode::Inherit)},
                                                     {L"Read Only", static_cast<LPARAM>(AccessMode::ReadOnly)},
                                                     {L"Read + Write", static_cast<LPARAM>(AccessMode::ReadWrite)},
                                                     {L"No Access", static_cast<LPARAM>(AccessMode::NoAccess)}});
    ui_kit::select_set_index(select_override_mode_, 1);

    ui_kit::style_scroll_host(list_, theme().panel);
    ui_kit::style_scroll_host(override_list_, theme().panel);
    hide_all();
    return list_ != nullptr;
}

void KnowledgeSettingsUi::destroy() {
    parent_ = nullptr;
    visible_ = false;
}

void KnowledgeSettingsUi::hide_all() {
    const HWND all[] = {heading_,
                        desc_,
                        list_,
                        empty_host_,
                        btn_add_knowledge_,
                        btn_add_skills_,
                        btn_edit_,
                        btn_refresh_,
                        btn_remove_,
                        lbl_name_,
                        edit_name_,
                        lbl_type_,
                        select_type_,
                        lbl_access_,
                        select_access_,
                        lbl_alias_,
                        edit_alias_,
                        check_agent_,
                        check_enabled_,
                        detail_,
                        btn_manage_perms_,
                        btn_save_,
                        btn_cancel_,
                        override_list_,
                        override_empty_host_,
                        lbl_override_path_,
                        edit_override_path_,
                        lbl_override_mode_,
                        select_override_mode_,
                        btn_override_browse_,
                        btn_override_add_,
                        btn_override_remove_,
                        btn_override_back_};
    for (HWND h : all) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
    visible_ = false;
}

void KnowledgeSettingsUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    if (perm_mode_) {
        show_perm_mode();
    } else if (form_mode_) {
        show_form_mode(editing_);
    } else {
        show_list_mode();
    }
}

void KnowledgeSettingsUi::update_empty_visibility() {
    if (!visible_) {
        return;
    }
    if (perm_mode_) {
        if (empty_host_) {
            ShowWindow(empty_host_, SW_HIDE);
        }
        if (overrides_empty_) {
            ShowWindow(override_list_, SW_HIDE);
            ShowWindow(override_empty_host_, SW_SHOW);
            InvalidateRect(override_empty_host_, nullptr, TRUE);
        } else {
            ShowWindow(override_empty_host_, SW_HIDE);
            ShowWindow(override_list_, SW_SHOW);
        }
        update_action_enabled();
        return;
    }
    if (override_empty_host_) {
        ShowWindow(override_empty_host_, SW_HIDE);
    }
    if (form_mode_) {
        if (empty_host_) {
            ShowWindow(empty_host_, SW_HIDE);
        }
        return;
    }
    if (empty_) {
        ShowWindow(list_, SW_HIDE);
        ShowWindow(empty_host_, SW_SHOW);
        InvalidateRect(empty_host_, nullptr, TRUE);
    } else {
        ShowWindow(empty_host_, SW_HIDE);
        ShowWindow(list_, SW_SHOW);
    }
    update_action_enabled();
}

void KnowledgeSettingsUi::update_action_enabled() {
    if (perm_mode_) {
        const bool has_override = !selected_override_path().empty();
        if (btn_override_remove_) {
            EnableWindow(btn_override_remove_, has_override ? TRUE : FALSE);
        }
        return;
    }
    const bool has = !selected_id().empty();
    if (btn_edit_) {
        EnableWindow(btn_edit_, has ? TRUE : FALSE);
    }
    if (btn_remove_) {
        EnableWindow(btn_remove_, has ? TRUE : FALSE);
    }
}

bool KnowledgeSettingsUi::open_selected_for_edit(KnowledgeStore& store, HWND owner) {
    const std::string wid = selected_id();
    if (wid.empty()) {
        MessageBoxW(owner, L"Select a source to edit.", L"Scylla", MB_ICONINFORMATION);
        return true;
    }
    const KnowledgeSource* s = store.by_id(wid);
    if (!s) {
        return true;
    }
    editing_id_ = s->id;
    load_form_from_source(*s);
    show_form_mode(true);
    return true;
}

bool KnowledgeSettingsUi::remove_selected(KnowledgeStore& store, HWND owner,
                                          const std::wstring& persist_path) {
    const std::string wid = selected_id();
    if (wid.empty()) {
        MessageBoxW(owner, L"Select a source to remove.", L"Scylla", MB_ICONINFORMATION);
        return true;
    }
    const KnowledgeSource* s = store.by_id(wid);
    if (!s) {
        return true;
    }
    std::wstring what = s->label;
    what += L"\n";
    what += s->path;
    if (!ui_kit::confirm_destructive(owner, L"Remove source", what.c_str(),
                                     L"The folder and its files are not deleted.")) {
        return true;
    }
    store.remove(wid);
    persist(store, persist_path);
    fill_list(store, project_id_);
    update_empty_visibility();
    return true;
}

namespace {

void show_windows(const std::vector<HWND>& windows, int cmd) {
    for (HWND h : windows) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

}  // namespace

void KnowledgeSettingsUi::show_list_mode() {
    form_mode_ = false;
    perm_mode_ = false;
    show_windows({lbl_name_, edit_name_, lbl_type_, select_type_, lbl_access_, select_access_, lbl_alias_,
                  edit_alias_, check_agent_, check_enabled_, detail_, btn_manage_perms_, btn_save_, btn_cancel_,
                  override_list_, override_empty_host_, lbl_override_path_, edit_override_path_, lbl_override_mode_,
                  select_override_mode_, btn_override_browse_, btn_override_add_, btn_override_remove_,
                  btn_override_back_},
                 SW_HIDE);
    SetWindowTextW(heading_, L"Knowledge/Skills");
    show_windows({heading_, desc_, btn_add_knowledge_, btn_add_skills_, btn_edit_, btn_refresh_, btn_remove_},
                 SW_SHOW);
    update_empty_visibility();
}

void KnowledgeSettingsUi::show_form_mode(bool editing) {
    form_mode_ = true;
    perm_mode_ = false;
    editing_ = editing;
    show_windows({list_, empty_host_, btn_add_knowledge_, btn_add_skills_, btn_edit_, btn_refresh_, btn_remove_,
                  desc_, override_list_, override_empty_host_, lbl_override_path_, edit_override_path_,
                  lbl_override_mode_, select_override_mode_, btn_override_browse_, btn_override_add_,
                  btn_override_remove_, btn_override_back_},
                 SW_HIDE);
    SetWindowTextW(heading_, editing ? L"Edit source" : L"Add source");
    // detail_ before interactive controls so ShowWindow cannot raise it over the checkboxes.
    show_windows({heading_, lbl_name_, edit_name_, lbl_type_, select_type_, lbl_access_, select_access_, lbl_alias_,
                  edit_alias_, detail_},
                 SW_SHOW);
    show_windows({check_agent_, check_enabled_, btn_manage_perms_, btn_save_, btn_cancel_}, SW_SHOW);
    for (HWND h : {check_agent_, check_enabled_, btn_manage_perms_}) {
        if (h) {
            InvalidateRect(h, nullptr, TRUE);
        }
    }
    // Focus the first field so the form is typable without a mouse click.
    if (edit_name_) {
        SetFocus(edit_name_);
        SendMessageW(edit_name_, EM_SETSEL, 0, -1);
    }
}

void KnowledgeSettingsUi::show_perm_mode() {
    form_mode_ = false;
    perm_mode_ = true;
    show_windows({list_, empty_host_, desc_, btn_add_knowledge_, btn_add_skills_, btn_edit_, btn_refresh_,
                  btn_remove_, lbl_name_, edit_name_, lbl_type_, select_type_, lbl_access_, select_access_,
                  lbl_alias_, edit_alias_, check_agent_, check_enabled_, btn_manage_perms_, btn_save_, btn_cancel_},
                 SW_HIDE);
    SetWindowTextW(heading_, L"Folder permissions");
    show_windows({heading_, detail_, lbl_override_path_, edit_override_path_, lbl_override_mode_,
                  select_override_mode_, btn_override_browse_, btn_override_add_, btn_override_remove_,
                  btn_override_back_},
                 SW_SHOW);
    update_empty_visibility();
}

void KnowledgeSettingsUi::layout(const RECT& area) {
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    const RECT col = ui_space::content_column(parent_, area);
    int x = col.left + m.pad_outer;
    int y = col.top + m.pad_outer;
    const int right = col.right - m.pad_outer;
    const int w = (std::max)(1, right - x);

    if (heading_) {
        MoveWindow(heading_, x, y, w, dip(parent_, ui_space::kPageHeaderTitleHDip), TRUE);
        y += dip(parent_, ui_space::kPageHeaderTitleHDip) + m.pad_tight;
    }

    // Align the form's actions with the field column, not the label column, so the primary
    // action sits under the inputs it commits.
    const int fx = x + m.label_w + m.pad_tight;
    const int fw = (std::max)(1, w - m.label_w - m.pad_tight);

    if (form_mode_) {
        ui_space::place_labeled_row(lbl_name_, edit_name_, col, y, m);
        ui_kit::center_field_text(edit_name_);
        y += m.row_h + m.pad_row;
        ui_space::place_labeled_row(lbl_type_, select_type_, col, y, m, m.btn_w * 2);
        y += m.row_h + m.pad_row;
        ui_space::place_labeled_row(lbl_access_, select_access_, col, y, m, m.btn_w * 2);
        y += m.row_h + m.pad_row;
        ui_space::place_labeled_row(lbl_alias_, edit_alias_, col, y, m);
        ui_kit::center_field_text(edit_alias_);
        y += m.row_h + m.pad_row;
        if (check_agent_) {
            MoveWindow(check_agent_, fx, y, fw, m.row_h, TRUE);
            y += m.row_h + m.pad_tight;
        }
        if (check_enabled_) {
            MoveWindow(check_enabled_, fx, y, fw, m.row_h, TRUE);
            y += m.row_h + m.pad_row;
        }
        if (btn_manage_perms_) {
            MoveWindow(btn_manage_perms_, fx, y, m.btn_w * 2, m.btn_h, TRUE);
            y += m.btn_h + m.pad_row;
        }
        if (detail_) {
            // Read-only summary — never steal hits from the checkboxes above (detail_ is created
            // after them, so a stale tall rect would sit on top in z-order).
            EnableWindow(detail_, FALSE);
            const int detail_h = (std::max)(m.row_h, static_cast<int>(col.bottom) - m.pad_outer - m.btn_h -
                                                         m.pad_section - y);
            MoveWindow(detail_, x, y, w, detail_h, TRUE);
            y += detail_h + m.pad_tight;
        }
        const int save_y = (std::max)(y, static_cast<int>(col.bottom) - m.pad_outer - m.btn_h);
        if (btn_save_) {
            MoveWindow(btn_save_, fx, save_y, m.btn_w, m.btn_h, TRUE);
        }
        if (btn_cancel_) {
            MoveWindow(btn_cancel_, fx + m.btn_w + m.pad_tight, save_y, m.btn_w, m.btn_h, TRUE);
        }
        for (HWND h : {check_agent_, check_enabled_, btn_manage_perms_, btn_save_, btn_cancel_}) {
            if (h) {
                RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
            }
        }
        return;
    }

    if (perm_mode_) {
        if (detail_) {
            const int detail_h = dip(parent_, ui_space::kPageHeaderDescHDip) * 3;
            MoveWindow(detail_, x, y, w, detail_h, TRUE);
            y += detail_h + m.pad_row;
        }
        // Reserve the add row and the Back/Remove row from the bottom, then give the list the rest.
        const int back_y = static_cast<int>(col.bottom) - m.pad_outer - m.btn_h;
        const int add_row_y = back_y - m.pad_row - m.row_h;
        const int mode_row_y = add_row_y - m.pad_row - m.row_h;
        const int list_h = (std::max)(1, mode_row_y - m.pad_row - y);
        if (override_list_) {
            MoveWindow(override_list_, x, y, w, list_h, TRUE);
        }
        if (override_empty_host_) {
            MoveWindow(override_empty_host_, x, y, w, list_h, TRUE);
        }
        ui_space::place_labeled_row(lbl_override_mode_, select_override_mode_, col, mode_row_y, m, m.btn_w * 2);

        // Folder field shares its row with Browse…, which fills the relative path from a picker.
        const int browse_w = m.btn_w;
        const int path_w = (std::max)(1, fw - browse_w - m.pad_tight);
        if (lbl_override_path_) {
            MoveWindow(lbl_override_path_, x, add_row_y, m.label_w, m.row_h, TRUE);
        }
        if (edit_override_path_) {
            MoveWindow(edit_override_path_, fx, add_row_y, path_w, m.row_h, TRUE);
            ui_kit::center_field_text(edit_override_path_);
        }
        if (btn_override_browse_) {
            MoveWindow(btn_override_browse_, fx + path_w + m.pad_tight, add_row_y, browse_w, m.row_h, TRUE);
        }

        int bx = x;
        auto place = [&](HWND h, int bw) {
            if (!h) {
                return;
            }
            MoveWindow(h, bx, back_y, bw, m.btn_h, TRUE);
            bx += bw + m.pad_tight;
        };
        place(btn_override_add_, m.btn_w + dip(parent_, 24));
        place(btn_override_remove_, m.btn_w);
        place(btn_override_back_, m.btn_w);
        update_empty_visibility();
        return;
    }

    if (desc_) {
        MoveWindow(desc_, x, y, w, dip(parent_, ui_space::kPageHeaderDescHDip), TRUE);
        y += dip(parent_, ui_space::kPageHeaderDescHDip) + m.pad_row;
    }

    // Reserve action row first so Edit/Remove are never clipped below the pane.
    const int btn_y = static_cast<int>(col.bottom) - m.pad_outer - m.btn_h;
    const int list_h = (std::max)(1, btn_y - m.pad_row - y);
    if (list_) {
        MoveWindow(list_, x, y, w, list_h, TRUE);
    }
    if (empty_host_) {
        MoveWindow(empty_host_, x, y, w, list_h, TRUE);
    }
    update_empty_visibility();

    int bx = x;
    auto place_btn = [&](HWND h, int bw) {
        if (!h) {
            return;
        }
        MoveWindow(h, bx, btn_y, bw, m.btn_h, TRUE);
        ShowWindow(h, SW_SHOW);
        bx += bw + m.pad_tight;
    };
    place_btn(btn_add_knowledge_, m.btn_w + dip(parent_, 40));
    place_btn(btn_add_skills_, m.btn_w + dip(parent_, 20));
    place_btn(btn_edit_, m.btn_w);
    place_btn(btn_refresh_, m.btn_w);
    place_btn(btn_remove_, m.btn_w);
    update_action_enabled();
}

void KnowledgeSettingsUi::fill_list(KnowledgeStore& store, const std::string& project_id) {
    list_ids_.clear();
    list_primary_.clear();
    list_secondary_.clear();
    if (!list_) {
        return;
    }
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);

    auto add_row = [&](const KnowledgeSource& s) {
        list_ids_.push_back(s.id);
        std::wstring primary = s.label.empty() ? folder_leaf(s.path) : s.label;
        if (!s.enabled) {
            primary += L"  (disabled)";
        }
        std::wstring secondary = source_type_label(s.type);
        secondary += L"  ·  ";
        secondary += s.access == AccessMode::ReadWrite ? L"RW" : L"Read";
        secondary += L"  ·  ";
        secondary += source_health_label(s.health);
        if (!s.overrides.empty()) {
            secondary += L"  ·  ";
            secondary += std::to_wstring(s.overrides.size());
            secondary += s.overrides.size() == 1 ? L" override" : L" overrides";
        }
        secondary += L"  ·  ";
        secondary += shorten_path(s.path, 40);
        if (!s.source_alias.empty()) {
            secondary += L"  ·  @";
            secondary += utf16(s.source_alias);
        }
        list_primary_.push_back(primary);
        list_secondary_.push_back(secondary);
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(primary.c_str()));
    };

    if (project_id.empty()) {
        for (const auto& s : store.sources()) {
            add_row(s);
        }
    } else {
        for (const KnowledgeSource* s : store.list_for_project(project_id)) {
            if (s) {
                add_row(*s);
            }
        }
    }
    empty_ = list_ids_.empty();
    if (!empty_ && SendMessageW(list_, LB_GETCURSEL, 0, 0) == LB_ERR) {
        SendMessageW(list_, LB_SETCURSEL, 0, 0);
    }
    InvalidateRect(list_, nullptr, TRUE);
    update_action_enabled();
}

void KnowledgeSettingsUi::reload(KnowledgeStore& store, const std::string& project_id) {
    project_id_ = project_id;
    SetWindowTextW(heading_, L"Knowledge/Skills");
    fill_list(store, project_id);
    // Reopening the panel always lands on the list. Leaving form_/perm_ set here meant a later
    // set_visible() restored a sub-view whose editing_id_ may no longer exist.
    form_mode_ = false;
    perm_mode_ = false;
    if (visible_) {
        show_list_mode();
    }
}

void KnowledgeSettingsUi::fill_override_list(const KnowledgeSource& s) {
    override_paths_.clear();
    override_primary_.clear();
    override_secondary_.clear();
    if (!override_list_) {
        return;
    }
    SendMessageW(override_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& o : s.overrides) {
        override_paths_.push_back(o.relative_path);
        override_primary_.push_back(o.relative_path + L"\\");
        std::wstring secondary = access_mode_label(o.mode);
        if (o.mode == AccessMode::NoAccess) {
            secondary += L"  ·  hidden from listing, search, and agent context";
        }
        override_secondary_.push_back(secondary);
        SendMessageW(override_list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(override_primary_.back().c_str()));
    }
    overrides_empty_ = override_paths_.empty();
    if (!overrides_empty_ && SendMessageW(override_list_, LB_GETCURSEL, 0, 0) == LB_ERR) {
        SendMessageW(override_list_, LB_SETCURSEL, 0, 0);
    }
    InvalidateRect(override_list_, nullptr, TRUE);
}

std::wstring KnowledgeSettingsUi::selected_override_path() const {
    if (!override_list_) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(override_list_, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(override_paths_.size())) {
        return {};
    }
    return override_paths_[static_cast<std::size_t>(sel)];
}

void KnowledgeSettingsUi::update_detail_text(const KnowledgeSource& s) {
    if (!detail_) {
        return;
    }
    std::wstring text = s.path;
    text += L"\r\nStatus: ";
    text += source_health_label(s.health);
    text += L"   ·   Root: ";
    text += access_mode_label(s.access);
    if (!s.overrides.empty()) {
        text += L"   ·   ";
        text += std::to_wstring(s.overrides.size());
        text += s.overrides.size() == 1 ? L" folder override" : L" folder overrides";
    }

    // Skill discovery is a filesystem walk, so it runs only when the detail is (re)built for a
    // Skills source — not on every list repaint.
    if (s.type == SourceType::Skills && s.enabled) {
        const auto skills = detect_skills(s.path);
        text += L"\r\nSkills detected: ";
        text += std::to_wstring(skills.size());
        if (!skills.empty()) {
            text += L" — ";
            const std::size_t shown = std::min<std::size_t>(6, skills.size());
            for (std::size_t i = 0; i < shown; ++i) {
                if (i) {
                    text += L", ";
                }
                text += skills[i].relative_dir.empty() ? folder_leaf(s.path) : folder_leaf(skills[i].relative_dir);
            }
            if (skills.size() > shown) {
                text += L", +";
                text += std::to_wstring(skills.size() - shown);
                text += L" more";
            }
        }
    }
    SetWindowTextW(detail_, text.c_str());
}

std::string KnowledgeSettingsUi::selected_id() const {
    if (!list_) {
        return {};
    }
    const int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(list_ids_.size())) {
        return {};
    }
    return list_ids_[static_cast<std::size_t>(sel)];
}

void KnowledgeSettingsUi::load_form_from_source(const KnowledgeSource& s) {
    SetWindowTextW(edit_name_, s.label.c_str());
    ui_kit::select_set_index(select_type_, static_cast<int>(s.type));
    ui_kit::select_set_index(select_access_, s.access == AccessMode::ReadWrite ? 1 : 0);
    SetWindowTextW(edit_alias_, utf16(s.source_alias).c_str());
    SendMessageW(check_agent_, BM_SETCHECK, s.agent_available ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(check_agent_, nullptr, TRUE);
    SendMessageW(check_enabled_, BM_SETCHECK, s.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(check_enabled_, nullptr, TRUE);
    update_detail_text(s);
}

KnowledgeSource KnowledgeSettingsUi::read_form(const KnowledgeSource* base) const {
    KnowledgeSource s;
    if (base) {
        s = *base;
    }
    const int nlen = GetWindowTextLengthW(edit_name_);
    std::wstring name(static_cast<std::size_t>(nlen), 0);
    GetWindowTextW(edit_name_, name.data(), nlen + 1);
    s.label = name;

    const int tsel = ui_kit::select_get_index(select_type_);
    s.type = static_cast<SourceType>((std::max)(0, (std::min)(3, tsel)));

    const int asel = ui_kit::select_get_index(select_access_);
    s.access = asel == 1 ? AccessMode::ReadWrite : AccessMode::ReadOnly;

    const int alen = GetWindowTextLengthW(edit_alias_);
    std::wstring alias(static_cast<std::size_t>(alen), 0);
    GetWindowTextW(edit_alias_, alias.data(), alen + 1);
    while (!alias.empty() && alias.front() == L'@') {
        alias.erase(alias.begin());
    }
    s.source_alias = utf8(alias);

    s.agent_available = SendMessageW(check_agent_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.enabled = SendMessageW(check_enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.recursive = true;
    return s;
}

bool KnowledgeSettingsUi::pick_folder(HWND owner, std::wstring& out, const wchar_t* title) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (title) {
        dlg->SetTitle(title);
    }
    const HRESULT shown = dlg->Show(owner);
    if (FAILED(shown)) {
        dlg->Release();
        return false;
    }
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item)) || !item) {
        dlg->Release();
        return false;
    }
    PWSTR path = nullptr;
    const HRESULT gn = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(gn) && path) {
        out = path;
        CoTaskMemFree(path);
    }
    item->Release();
    dlg->Release();
    return !out.empty();
}

void KnowledgeSettingsUi::persist(KnowledgeStore& store, const std::wstring& persist_path) {
    if (persist_path.empty()) {
        return;
    }
    if (!store.save(persist_path)) {
        // Silently dropping this made edits look applied and then vanish on restart.
        ui_kit::report_save_failure(parent_, L"Knowledge/Skills sources", persist_path);
    }
}

bool KnowledgeSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || mi->CtlType != ODT_LISTBOX) {
        return false;
    }
    if (mi->CtlID != IdList && mi->CtlID != IdOverrideList) {
        return false;
    }
    mi->itemHeight = dip(parent_, 48);
    return true;
}

bool KnowledgeSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di) {
        return false;
    }
    if (di->CtlType == ODT_STATIC && di->hwndItem == empty_host_) {
        ui_kit::paint_empty_state(di->hDC, di->rcItem, font, font, L"No knowledge sources",
                                  L"Add a Knowledge or Skills folder to get started.");
        return true;
    }
    if (di->CtlType == ODT_STATIC && di->hwndItem == override_empty_host_) {
        ui_kit::paint_empty_state(di->hDC, di->rcItem, font, font, L"No folder overrides",
                                  L"Every folder inherits the source root permission.");
        return true;
    }
    if (di->CtlType != ODT_LISTBOX) {
        return false;
    }
    const bool is_overrides = di->hwndItem == override_list_;
    if (di->hwndItem != list_ && !is_overrides) {
        return false;
    }
    if (di->itemID == static_cast<UINT>(-1)) {
        fill_rect(di->hDC, di->rcItem, theme().panel);
        return true;
    }
    const auto& primaries = is_overrides ? override_primary_ : list_primary_;
    const auto& secondaries = is_overrides ? override_secondary_ : list_secondary_;
    const bool sel = (di->itemState & ODS_SELECTED) != 0;
    const wchar_t* primary = L"";
    const wchar_t* secondary = L"";
    if (di->itemID < primaries.size()) {
        primary = primaries[di->itemID].c_str();
        secondary = secondaries[di->itemID].c_str();
    }
    ui_kit::paint_entity_row(di->hDC, di->rcItem, font, primary, secondary, sel, false);
    return true;
}

bool KnowledgeSettingsUi::on_command(WORD id, WORD notify, HWND owner, KnowledgeStore& store,
                                     const std::wstring& persist_path, const std::string& project_id) {
    if (id == IdType && ui_kit::select_handle_command(select_type_, notify)) {
        return true;
    }
    if (id == IdAccess && ui_kit::select_handle_command(select_access_, notify)) {
        return true;
    }
    if (id == IdOverrideMode && ui_kit::select_handle_command(select_override_mode_, notify)) {
        return true;
    }
    if (id < IdList || id > IdLast) {
        return false;
    }
    project_id_ = project_id;

    auto begin_add = [&](SourceType type, AccessMode access, const wchar_t* title) {
        std::wstring folder;
        if (!pick_folder(owner, folder, title)) {
            return;
        }
        if (is_drive_root_path(folder)) {
            MessageBoxW(owner, L"Drive roots are not allowed as Knowledge/Skills sources.", L"Scylla",
                        MB_ICONWARNING);
            return;
        }
        std::vector<std::string> pids;
        if (!project_id.empty()) {
            pids.push_back(project_id);
        }
        auto* src = store.add_source(folder_leaf(folder), folder, type, access, true, pids, {});
        if (!src) {
            MessageBoxW(owner, L"Could not add that folder (invalid path).", L"Scylla Workbench",
                        MB_OK | MB_ICONERROR);
            return;
        }
        // Staged add: do NOT persist yet. Cancel must be able to undo this, and persisting here
        // meant a cancelled add still left the folder in knowledge.json.
        pending_add_id_ = src->id;
        editing_id_ = src->id;
        load_form_from_source(*src);
        show_form_mode(true);
    };

    switch (id) {
        case IdList:
            if (notify == LBN_SELCHANGE) {
                update_action_enabled();
                InvalidateRect(list_, nullptr, TRUE);
                return true;
            }
            if (notify == LBN_DBLCLK) {
                return open_selected_for_edit(store, owner);
            }
            return false;
        case IdAddKnowledge:
            begin_add(SourceType::Knowledge, AccessMode::ReadWrite, L"Add Knowledge Folder");
            return true;
        case IdAddSkills:
            begin_add(SourceType::Skills, AccessMode::ReadOnly, L"Add Skills Folder");
            return true;
        case IdEdit:
            return open_selected_for_edit(store, owner);
        case IdRefreshHealth: {
            store.refresh_all_health();
            persist(store, persist_path);
            fill_list(store, project_id);
            update_empty_visibility();
            return true;
        }
        case IdRemove:
            return remove_selected(store, owner, persist_path);
        case IdManagePerms: {
            const KnowledgeSource* s = store.by_id(editing_id_);
            if (!s) {
                return true;
            }
            // Commit the pending form first so the permissions view reflects the root access and
            // enabled state the user just chose rather than the last-saved copy.
            KnowledgeSource staged = read_form(s);
            staged.id = s->id;
            staged.path = s->path;
            staged.project_ids = s->project_ids;
            staged.overrides = s->overrides;
            store.update(staged);
            const KnowledgeSource* fresh = store.by_id(editing_id_);
            if (fresh) {
                fill_override_list(*fresh);
                update_detail_text(*fresh);
            }
            show_perm_mode();
            return true;
        }
        case IdOverrideList:
            if (notify == LBN_SELCHANGE) {
                update_action_enabled();
                InvalidateRect(override_list_, nullptr, TRUE);
                // Echo the selection into the edit row so re-picking a mode is one click, not retyping.
                const std::wstring rel = selected_override_path();
                if (!rel.empty()) {
                    SetWindowTextW(edit_override_path_, rel.c_str());
                    const KnowledgeSource* s = store.by_id(editing_id_);
                    if (s) {
                        for (const auto& o : s->overrides) {
                            if (_wcsicmp(o.relative_path.c_str(), rel.c_str()) == 0) {
                                ui_kit::select_set_by_data(select_override_mode_, static_cast<LPARAM>(o.mode));
                                break;
                            }
                        }
                    }
                }
                return true;
            }
            return false;
        case IdOverrideBrowse: {
            const KnowledgeSource* s = store.by_id(editing_id_);
            if (!s) {
                return true;
            }
            std::wstring folder;
            if (!pick_folder(owner, folder, L"Choose a folder inside this source")) {
                return true;
            }
            const std::wstring rel = relative_within(s->path, canonicalize_path(folder));
            if (rel.empty()) {
                MessageBoxW(owner, L"Choose a folder inside the source root. Overrides cannot point outside it.",
                            L"Scylla", MB_ICONWARNING);
                return true;
            }
            SetWindowTextW(edit_override_path_, rel.c_str());
            return true;
        }
        case IdOverrideAdd: {
            const KnowledgeSource* s = store.by_id(editing_id_);
            if (!s) {
                return true;
            }
            const std::wstring typed = get_field_text(edit_override_path_);
            const std::wstring rel = normalize_override_path(typed);
            if (rel.empty()) {
                MessageBoxW(owner,
                            L"Enter a folder path relative to the source root, for example private\\drafts.\n"
                            L"The root itself is controlled by the source's Access setting.",
                            L"Scylla", MB_ICONWARNING);
                return true;
            }
            const auto mode = static_cast<AccessMode>(ui_kit::select_get_data(select_override_mode_));
            if (mode == AccessMode::Inherit) {
                store.remove_override(editing_id_, rel);
            } else if (!store.set_override(editing_id_, rel, mode)) {
                MessageBoxW(owner, L"Could not apply that override.", L"Scylla", MB_ICONERROR);
                return true;
            }
            store.refresh_health(editing_id_);
            persist(store, persist_path);
            SetWindowTextW(edit_override_path_, L"");
            const KnowledgeSource* fresh = store.by_id(editing_id_);
            if (fresh) {
                fill_override_list(*fresh);
                update_detail_text(*fresh);
            }
            update_empty_visibility();
            return true;
        }
        case IdOverrideRemove: {
            const std::wstring rel = selected_override_path();
            if (rel.empty()) {
                MessageBoxW(owner, L"Select an override to remove.", L"Scylla", MB_ICONINFORMATION);
                return true;
            }
            store.remove_override(editing_id_, rel);
            store.refresh_health(editing_id_);
            persist(store, persist_path);
            const KnowledgeSource* fresh = store.by_id(editing_id_);
            if (fresh) {
                fill_override_list(*fresh);
                update_detail_text(*fresh);
            }
            update_empty_visibility();
            return true;
        }
        case IdOverrideBack: {
            const KnowledgeSource* s = store.by_id(editing_id_);
            if (s) {
                load_form_from_source(*s);
            }
            show_form_mode(true);
            return true;
        }
        case IdSaveForm: {
            KnowledgeSource* existing = store.by_id(editing_id_);
            if (!existing) {
                show_list_mode();
                reload(store, project_id);
                return true;
            }
            KnowledgeSource next = read_form(existing);
            next.id = existing->id;
            next.path = existing->path;
            next.project_ids = existing->project_ids;
            // Changing the root permission must never silently flatten the folder tree.
            next.overrides = existing->overrides;
            if (!store.update(next)) {
                MessageBoxW(owner, L"Could not update source.", L"Scylla", MB_ICONERROR);
                return true;
            }
            store.refresh_health(next.id);
            persist(store, persist_path);
            editing_id_.clear();
            pending_add_id_.clear();
            SetWindowTextW(heading_, L"Knowledge/Skills");
            reload(store, project_id);
            show_list_mode();
            return true;
        }
        case IdCancelForm:
            // Undo a staged add so Cancel genuinely cancels.
            if (!pending_add_id_.empty()) {
                store.remove(pending_add_id_);
                pending_add_id_.clear();
            }
            editing_id_.clear();
            SetWindowTextW(heading_, L"Knowledge/Skills");
            show_list_mode();
            reload(store, project_id);
            return true;
        case IdAgent:
        case IdEnabled:
            // Subclass already toggled ScyllaChecked. Returning true would rebuild_tree+layout and
            // can hide/show the control mid-click — treat as handled-in-place.
            return false;
        default:
            // Report "not handled" for labels, headings and stray notify codes. Claiming these
            // made the host rebuild the file tree and relayout for every incidental message.
            return false;
    }
}

}  // namespace scyllagpt
