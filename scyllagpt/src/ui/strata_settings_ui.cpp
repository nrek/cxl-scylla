#include "scyllagpt/strata_settings_ui.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"
#include <algorithm>
#include <chrono>

namespace scyllagpt {
namespace {
std::wstring text(HWND h) {
    if (!h)
        return {};
    std::wstring v(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    GetWindowTextW(h, v.data(), static_cast<int>(v.size()));
    v.resize(wcslen(v.c_str()));
    return v;
}
} // namespace
StrataSettingsUi::~StrataSettingsUi() { destroy(); }
void StrataSettingsUi::destroy() {
    if (pending_.valid())
        pending_.wait(); // Worker owns its bridge, never an HWND.
    for (auto h : controls())
        if (h && IsWindow(h))
            DestroyWindow(h);
    heading_ = desc_ = status_ = workspace_label_ = workspace_ = project_label_ = project_ = nullptr;
    test_btn_ = open_btn_ = refresh_btn_ = search_label_ = search_ = results_ = hint_ = diag_ = nullptr;
    save_binding_btn_ = recent_btn_ = detail_ = nullptr;
    remote_label_ = remote_ = save_remote_ = source_ = projects_btn_ = open_doc_ = close_doc_ = nullptr;
    publish_ = pull_ = search_btn_ = nullptr;
    visible_ = false;
}
bool StrataSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font) {
    parent_ = parent;
    font_ = font;
    auto label = [&](int id, const wchar_t *v) {
        return ui_kit::create_static(parent, inst, id, v, font, false);
    };
    auto button = [&](int id, const wchar_t *v) {
        return ui_kit::create_button(parent, inst, id, v, ui_kit::ButtonKind::Secondary, font);
    };
    heading_ = label(Id_StrataHeading, L"STRATA");
    desc_ = label(Id_StrataDesc,
                  L"Browse projects, plans, blueprints and handoffs. Local knowledge works offline.");
    status_ = label(Id_StrataStatus, L"Local library");
    workspace_label_ = label(Id_StrataWorkspaceLabel, L"Workspace");
    workspace_ = ui_kit::create_path_field(parent, inst, Id_StrataWorkspace, font);
    remote_label_ = label(Id_StrataRemoteLabel, L"Remote API");
    remote_ = ui_kit::create_text_field(parent, inst, Id_StrataRemote, font);
    save_remote_ = button(Id_StrataSaveRemote, L"Save Remote");
    ui_kit::set_placeholder(remote_, L"Optional HTTPS API URL; blank means local only");
    source_ = ui_kit::create_select(parent, inst, Id_StrataSource, font);
    ui_kit::select_set_items(source_, {{L"Local", 0}, {L"Remote", 1}});
    ui_kit::select_set_index(source_, 0);
    projects_btn_ = button(Id_StrataProjects, L"Projects");
    refresh_btn_ = button(Id_StrataRefresh, L"Refresh");
    publish_ = button(Id_StrataPublish, L"Publish");
    pull_ = button(Id_StrataPull, L"Pull");
    search_ = ui_kit::create_text_field(parent, inst, Id_StrataSearch, font);
    ui_kit::set_placeholder(search_, L"Search library, or open a project to narrow results");
    search_btn_ = button(Id_StrataSearchButton, L"Search");
    results_ = ui_kit::create_list(parent, inst, Id_StrataResults, font, false);
    detail_ = ui_kit::create_markdown_view(parent, inst, Id_StrataDetail, font);
    open_doc_ = button(Id_StrataOpen, L"Open");
    close_doc_ = button(Id_StrataClose, L"Close Document");
    save_binding_btn_ = button(Id_StrataSaveBinding, L"Bind Project");
    diag_ = label(Id_StrataDiag, L"");
    hint_ = label(Id_StrataHint, L"Remote credentials remain in STRATA.");
    ui_kit::style_scroll_host(results_, theme().panel);
    update_visibility();
    return results_ && detail_;
}
void StrataSettingsUi::apply_fonts() {
    for (auto h : controls())
        if (h)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}
void StrataSettingsUi::set_visible(bool v) {
    visible_ = v;
    update_visibility();
}
void StrataSettingsUi::update_visibility() {
    const bool busy = pending_.valid();
    for (auto h : controls())
        if (h)
            ShowWindow(h, visible_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(results_, visible_ && !document_open_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(detail_, visible_ && document_open_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(open_doc_, visible_ && !document_open_ ? SW_SHOWNA : SW_HIDE);
    ShowWindow(close_doc_, visible_ && document_open_ ? SW_SHOWNA : SW_HIDE);
    for (auto h : {workspace_, remote_, save_remote_, source_, projects_btn_, refresh_btn_, search_,
                   search_btn_, open_doc_})
        if (h)
            EnableWindow(h, !busy && !document_open_);
    EnableWindow(close_doc_, !busy);
    EnableWindow(results_, !busy);
    EnableWindow(publish_, !busy && !document_open_ && remote_configured_ && !browse_project_.empty());
    EnableWindow(pull_, !busy && !document_open_ && remote_configured_ && !browse_project_.empty());
    EnableWindow(save_binding_btn_, !busy && !document_open_ && !browse_project_.empty());
}
void StrataSettingsUi::layout(const RECT &content) {
    content_ = content;
    if (!heading_)
        return;
    apply_fonts();
    auto m = ui_space::metrics_for(parent_);
    auto col = ui_space::content_column(parent_, content);
    int x = col.left + m.pad_outer, w = (std::max)(1, static_cast<int>(col.right) - m.pad_outer - x),
        y = col.top + m.pad_outer;
    auto row = [&](HWND h, int height) {
        MoveWindow(h, x, y, w, height, TRUE);
        y += height + m.pad_tight;
    };
    row(heading_, m.row_h);
    // Static labels use the system word-wrapping behavior. Reserve two lines so a
    // narrow settings column cannot place the next control over the description.
    const int message_h = m.row_h * 2;
    row(desc_, message_h);
    ui_space::place_labeled_row(workspace_label_, workspace_, col, y, m);
    ui_kit::center_field_text(workspace_);
    y += m.row_h + m.pad_tight;
    int aw = m.btn_w + m.pad_row;
    ui_space::place_labeled_row(remote_label_, remote_, col, y, m, -(aw + m.pad_tight));
    ui_kit::center_field_text(remote_);
    MoveWindow(save_remote_, x + w - aw, y, aw, m.row_h, TRUE);
    y += m.row_h + m.pad_tight;
    int bx = x, bw = (std::max)(1, (w - 4 * m.pad_tight) / 5);
    for (auto h : {source_, projects_btn_, refresh_btn_, publish_, pull_}) {
        MoveWindow(h, bx, y, bw, m.row_h, TRUE);
        bx += bw + m.pad_tight;
    }
    y += m.row_h + m.pad_tight;
    MoveWindow(search_, x, y, (std::max)(1, w - aw - m.pad_tight), m.row_h, TRUE);
    ui_kit::center_field_text(search_);
    MoveWindow(search_btn_, x + w - aw, y, aw, m.row_h, TRUE);
    y += m.row_h + m.pad_tight;
    row(status_, m.row_h);
    // The document/results area shares the page with the close/open row and two
    // wrapped status rows. Budget their actual heights before expanding the list.
    const int fixed_after_results = m.row_h + 2 * message_h + 3 * m.pad_tight + m.pad_outer;
    int height = (std::max)(m.row_h, static_cast<int>(col.bottom) - y - fixed_after_results);
    MoveWindow(results_, x, y, w, height, TRUE);
    MoveWindow(detail_, x, y, w, height, TRUE);
    y += height + m.pad_tight;
    MoveWindow(open_doc_, x, y, aw, m.row_h, TRUE);
    MoveWindow(close_doc_, x, y, aw + m.row_h, m.row_h, TRUE);
    MoveWindow(save_binding_btn_, x + w - aw, y, aw, m.row_h, TRUE);
    y += m.row_h + m.pad_tight;
    row(diag_, message_h);
    row(hint_, message_h);
    update_visibility();
}
void StrataSettingsUi::set_workspace_path(const std::wstring &p) {
    if (workspace_ && text(workspace_) != p) {
        SetWindowTextW(workspace_, p.c_str());
        loaded_workspace_.clear();
    }
}
void StrataSettingsUi::set_project_binding(const std::wstring &) {
} // Human selects library scope independently.
std::wstring StrataSettingsUi::workspace_path() const { return text(workspace_); }
std::wstring StrataSettingsUi::project_binding() const { return utf16(browse_project_); }
void StrataSettingsUi::set_status_text(const std::wstring &v) { SetWindowTextW(status_, v.c_str()); }
void StrataSettingsUi::set_diag_text(const std::wstring &v) { SetWindowTextW(diag_, v.c_str()); }
void StrataSettingsUi::set_detail_text(const std::wstring &v) { ui_kit::set_markdown(detail_, v); }
void StrataSettingsUi::refresh(StrataBridge *, StrataClient *) {
    if (pending_.valid() || loaded_workspace_ == workspace_path())
        return;
    browse_project_.clear();
    remote_mode_ = false;
    document_open_ = false;
    ui_kit::select_set_index(source_, 0);
    begin("projects");
}
void StrataSettingsUi::begin(const std::string &op, const std::string &path) {
    if (pending_.valid())
        return;
    Json p = Json::object();
    p["workspace_root"] = Json::string(utf8(workspace_path()));
    p["operation"] = Json::string(op);
    p["source"] = Json::string(remote_mode_ ? "remote" : "local");
    p["project"] = Json::string(op == "projects" ? "" : browse_project_);
    p["query"] = Json::string(op == "search" ? utf8(text(search_)) : "");
    p["path"] = Json::string(path);
    if (op == "configure")
        p["remote_url"] = Json::string(utf8(text(remote_)));
    pending_op_ = op;
    pending_workspace_ = utf8(workspace_path());
    pending_ = std::async(std::launch::async, [p = std::move(p)]() mutable {
        StrataBridge worker;
        std::string error;
        Json result = worker.library(std::move(p), &error);
        if (!error.empty()) {
            result = Json::object();
            result["error"] = Json::string(error);
        }
        return result;
    });
    set_diag_text(L"Loading…");
    update_visibility();
}
void StrataSettingsUi::show_rows(const Json &r) {
    displayed_remote_ = remote_mode_;
    displayed_project_ = browse_project_;
    displayed_query_ = text(search_);
    project_rows_ = r.at("projects").is_array();
    rows_ = project_rows_ ? r.at("projects") : r.at("hits");
    if (!rows_.is_array())
        rows_ = Json::array();
    SendMessageW(results_, LB_RESETCONTENT, 0, 0);
    for (const auto &item : rows_.array_items()) {
        std::wstring row = project_rows_ ? utf16(item.at("project").as_string()) + L" · " +
                                               std::to_wstring(item.at("total").as_int()) + L" documents"
                                         : utf16(item.at("kind").as_string()) + L" · " +
                                               utf16(item.at("title").as_string()) + L" [" +
                                               utf16(item.at("project").as_string()) + L"]";
        SendMessageW(results_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
    if (!rows_.size())
        SendMessageW(results_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No matching items. Try another project or query."));
    else
        SendMessageW(results_, LB_SETCURSEL, 0, 0);
    set_status_text(std::wstring(remote_mode_ ? L"Remote" : L"Local") + L" · " +
                    (browse_project_.empty() ? L"All projects" : utf16(browse_project_)) + L" · " +
                    std::to_wstring(rows_.size()) + L" items");
}
void StrataSettingsUi::poll() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    Json r;
    try {
        r = pending_.get();
    } catch (...) {
        r = Json::object();
        r["error"] = Json::string("STRATA worker failed");
    }
    if (pending_workspace_ != utf8(workspace_path())) {
        loaded_workspace_.clear();
        refresh(nullptr, nullptr);
        return;
    }
    if (r.has("error")) {
        remote_mode_ = displayed_remote_;
        browse_project_ = displayed_project_;
        ui_kit::select_set_index(source_, remote_mode_ ? 1 : 0);
        SetWindowTextW(search_, displayed_query_.c_str());
        set_diag_text(utf16(r.at("error").as_string()));
        chrome_summary_ = L"STRATA request failed";
    } else {
        loaded_workspace_ = workspace_path();
        chrome_summary_ = L"Library ready";
        if (r.at("config").is_object()) {
            const auto &c = r.at("config");
            remote_configured_ = c.at("remote_configured").as_bool();
            SetWindowTextW(remote_, utf16(c.at("remote_url").as_string()).c_str());
            SetWindowTextW(
                hint_, c.at("config_scope").as_string() == "workspace"
                           ? L"Remote comes from workspace .strata/config.json; credentials stay in STRATA."
                           : L"Remote uses user STRATA config and credentials. Blank URL means local only.");
        }
        if (pending_op_ == "get") {
            const auto &doc = r.at("document");
            if (!doc.is_object())
                set_diag_text(L"Document response missing; results preserved.");
            else {
                auto body = doc.at("body").as_string();
                std::string display;
                for (size_t i = 0; i < body.size(); ++i) {
                    if (body[i] == '\n' && (i == 0 || body[i - 1] != '\r'))
                        display += '\r';
                    display += body[i];
                }
                set_detail_text(utf16(display));
                document_open_ = true;
                set_diag_text(doc.at("body_truncated").as_bool()
                                  ? L"Document capped by STRATA at 200,000 characters."
                                  : L"Read-only document · Close Document returns to your results.");
            }
        } else if (pending_op_ == "publish" || pending_op_ == "pull")
            set_diag_text(utf16(r.at("transfer").dump()));
        else {
            if (r.has("projects") || r.has("hits"))
                show_rows(r);
            set_diag_text(r.has("message")
                              ? utf16(r.at("message").as_string())
                              : (r.at("truncated").as_bool() ? L"Results capped; narrow the search."
                                                             : L"Select a row and Open, or double-click."));
        }
    }
    update_visibility();
    if (document_open_ && visible_)
        SetFocus(detail_);
}
bool StrataSettingsUi::handle_command(int id, WORD notify, StrataBridge *, StrataClient *client,
                                      const std::string &project_id, const std::wstring &settings_path,
                                      HWND owner) {
    if (id == Id_StrataSource) {
        if (ui_kit::select_handle_command(source_, notify))
            return true;
        if (notify == CBN_SELCHANGE) {
            remote_mode_ = ui_kit::select_get_index(source_) == 1;
            if (remote_mode_ && !remote_configured_) {
                remote_mode_ = false;
                ui_kit::select_set_index(source_, 0);
                set_diag_text(L"Configure a remote URL and STRATA credentials first.");
                return true;
            }
            browse_project_.clear();
            SetWindowTextW(search_, L"");
            begin("projects");
            return true;
        }
    }
    if (id == Id_StrataResults) {
        if (notify != LBN_DBLCLK)
            return false;
        id = Id_StrataOpen;
        notify = BN_CLICKED;
    }
    if (notify != BN_CLICKED)
        return false;
    switch (id) {
    case Id_StrataClose:
        document_open_ = false;
        update_visibility();
        SetFocus(results_);
        return true;
    case Id_StrataOpen: {
        int i = static_cast<int>(SendMessageW(results_, LB_GETCURSEL, 0, 0));
        if (i < 0 || i >= static_cast<int>(rows_.size()))
            return true;
        const auto &item = rows_.at(static_cast<size_t>(i));
        if (project_rows_) {
            browse_project_ = item.at("project").as_string();
            SetWindowTextW(search_, L"");
            begin("search");
        } else
            begin("get", item.at("path").as_string());
        return true;
    }
    case Id_StrataProjects:
        browse_project_.clear();
        SetWindowTextW(search_, L"");
        begin("projects");
        return true;
    case Id_StrataSearch:
    case Id_StrataSearchButton:
        begin("search");
        return true;
    case Id_StrataRefresh:
        begin(browse_project_.empty() && text(search_).empty() ? "projects" : "search");
        return true;
    case Id_StrataSaveRemote:
        begin("configure");
        return true;
    case Id_StrataSaveBinding: {
        if (!client || project_id.empty() || browse_project_.empty()) {
            set_diag_text(L"Open a Scylla project and a STRATA library project before binding.");
            return true;
        }
        const auto previous = client->bindings();
        StrataBinding binding;
        if (const auto *prior = client->binding_for(project_id))
            binding = *prior;
        binding.project_id = project_id;
        binding.strata_project = browse_project_;
        client->upsert_binding(binding);
        if (!client->save(settings_path)) {
            client->bindings() = previous;
            set_diag_text(L"Could not save project binding.");
            ui_kit::report_save_failure(owner, L"STRATA project binding", settings_path);
        } else
            set_diag_text(L"Active Scylla project bound to " + utf16(browse_project_) + L".");
        return true;
    }
    case Id_StrataPublish:
    case Id_StrataPull:
        if (browse_project_.empty() || !remote_configured_)
            return true;
        if (ui_kit::confirm_destructive(
                owner, id == Id_StrataPublish ? L"Publish" : L"Pull", utf16(browse_project_).c_str(),
                id == Id_StrataPublish
                    ? L"Upload this project's pending indexed documents to the configured remote."
                    : L"Import this project's remote documents using STRATA's conflict and storage rules."))
            begin(id == Id_StrataPublish ? "publish" : "pull");
        return true;
    default:
        return false;
    }
}
} // namespace scyllagpt
