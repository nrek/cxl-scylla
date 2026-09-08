#pragma once

#include "scyllagpt/strata_bridge.h"
#include "scyllagpt/strata_client.h"

#include <future>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum StrataSettingsCtrlId : int {
    Id_StrataStatus = 3400,
    Id_StrataWorkspaceLabel,
    Id_StrataWorkspace,
    Id_StrataProjectLabel,
    Id_StrataProject,
    Id_StrataTest,
    Id_StrataOpenApp,
    Id_StrataRefresh,
    Id_StrataSearchLabel,
    Id_StrataSearch,
    Id_StrataResults,
    Id_StrataHint,
    Id_StrataHeading,
    Id_StrataDesc,
    Id_StrataDiag,
    Id_StrataSaveBinding,
    Id_StrataRecent,
    Id_StrataDetail,
    Id_StrataRemoteLabel,
    Id_StrataRemote,
    Id_StrataSaveRemote,
    Id_StrataSource,
    Id_StrataProjects,
    Id_StrataOpen,
    Id_StrataClose,
    Id_StrataPublish,
    Id_StrataPull,
    Id_StrataSearchButton,
};

class StrataSettingsUi {
  public:
    StrataSettingsUi() = default;
    ~StrataSettingsUi();

    StrataSettingsUi(const StrataSettingsUi &) = delete;
    StrataSettingsUi &operator=(const StrataSettingsUi &) = delete;

    bool create(HWND parent, HINSTANCE inst, HFONT font);
    void destroy();

    // Re-point the cached font after a DPI change.
    void set_fonts(HFONT font) { font_ = font; }

    void set_visible(bool visible);
    void layout(const RECT &content);
    bool visible() const { return visible_; }

    void set_workspace_path(const std::wstring &path);
    void set_project_binding(const std::wstring &project);
    std::wstring workspace_path() const;
    std::wstring project_binding() const;

    void refresh(StrataBridge *bridge, StrataClient *http_fallback);
    void poll();
    bool handle_command(int id, WORD notify, StrataBridge *bridge, StrataClient *http_fallback,
                        const std::string &project_id, const std::wstring &strata_path, HWND owner);

    HWND hwnd_status() const { return status_; }

    // Cached one-liner for the window status bar. Empty until a
    // refresh has run — the status bar must never trigger a blocking bridge round trip.
    const std::wstring &chrome_summary() const { return chrome_summary_; }

    // Enter submits the focused search/configuration control.
    UINT default_command(HWND field) const {
        if (field && field == search_) {
            return Id_StrataSearch;
        }
        if (field && field == project_) {
            return Id_StrataSaveBinding;
        }
        if (field && field == remote_)
            return Id_StrataSaveRemote;
        if (field && field == results_)
            return Id_StrataOpen;
        return 0;
    }
    UINT cancel_command() const { return document_open_ ? Id_StrataClose : 0; }

  private:
    // One source of truth for the show/hide/font/destroy sweeps — four hand-maintained
    // copies of this list is how new controls end up invisible or unfonted.
    std::vector<HWND> controls() const {
        return {heading_,      desc_,     status_,      workspace_label_, workspace_,    project_label_,
                project_,      test_btn_, open_btn_,    refresh_btn_,     recent_btn_,   save_binding_btn_,
                search_label_, search_,   results_,     detail_,          hint_,         diag_,
                remote_label_, remote_,   save_remote_, source_,          projects_btn_, open_doc_,
                close_doc_,    publish_,  pull_,        search_btn_};
    }

    void apply_fonts();
    void set_status_text(const std::wstring &text);
    void set_diag_text(const std::wstring &text);
    void set_detail_text(const std::wstring &text);
    void begin(const std::string &operation, const std::string &path = {});
    void show_rows(const Json &result);
    void update_visibility();

    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    bool visible_ = false;

    HWND heading_ = nullptr;
    HWND desc_ = nullptr;
    HWND status_ = nullptr;
    HWND workspace_label_ = nullptr;
    HWND workspace_ = nullptr;
    HWND project_label_ = nullptr;
    HWND project_ = nullptr;
    HWND test_btn_ = nullptr;
    HWND open_btn_ = nullptr;
    HWND refresh_btn_ = nullptr;
    HWND recent_btn_ = nullptr;
    HWND save_binding_btn_ = nullptr;
    HWND search_label_ = nullptr;
    HWND search_ = nullptr;
    HWND results_ = nullptr;
    HWND detail_ = nullptr;
    HWND hint_ = nullptr;
    HWND diag_ = nullptr;

    std::wstring chrome_summary_;
    HWND remote_label_ = nullptr, remote_ = nullptr, save_remote_ = nullptr, source_ = nullptr;
    HWND projects_btn_ = nullptr, open_doc_ = nullptr, close_doc_ = nullptr;
    HWND publish_ = nullptr, pull_ = nullptr, search_btn_ = nullptr;
    RECT content_{};
    std::future<Json> pending_;
    std::string pending_op_, pending_workspace_;
    std::string browse_project_;
    Json rows_ = Json::array();
    bool project_rows_ = true, document_open_ = false, remote_mode_ = false;
    bool remote_configured_ = false;
    bool displayed_remote_ = false;
    std::string displayed_project_;
    std::wstring displayed_query_;
    std::wstring loaded_workspace_;
};

} // namespace scyllagpt
