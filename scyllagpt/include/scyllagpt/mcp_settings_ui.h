#pragma once

#include "scyllagpt/commands.h"
#include "scyllagpt/mcp_manager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace scyllagpt {

// Settings → MCP management surface (ui_kit).
class McpSettingsUi {
public:
    // (project id, display label) pairs used to render the project-scope picker.
    using ProjectList = std::vector<std::pair<std::string, std::wstring>>;

    // Controls added after the original create() signature carry their command ids directly, the
    // same way TerminalSettingsUi does, so window.cpp does not grow another dozen parameters.
    static constexpr UINT IdAddTransport = Cmd_McpAddTransport;
    static constexpr UINT IdTest = Cmd_McpTest;
    static constexpr UINT IdEditName = Cmd_McpEditName;
    static constexpr UINT IdEditAlias = Cmd_McpEditAlias;
    static constexpr UINT IdEditTransport = Cmd_McpEditTransport;
    static constexpr UINT IdEditEndpoint = Cmd_McpEditEndpoint;
    static constexpr UINT IdEditEnabled = Cmd_McpEditEnabled;
    static constexpr UINT IdEditScopeAll = Cmd_McpEditScopeAll;
    static constexpr UINT IdEditScopeList = Cmd_McpEditScopeList;
    static constexpr UINT IdEditSave = Cmd_McpEditSave;
    static constexpr UINT IdEditCancel = Cmd_McpEditCancel;

    bool create(HWND parent, HFONT font, int id_list, int id_detail, int id_btn_add, int id_btn_add_account,
                int id_btn_manage, int id_btn_reauth, int id_btn_check, int id_btn_disable, int id_btn_disconnect,
                int id_btn_remove, int id_add_template, int id_add_name, int id_add_alias, int id_add_endpoint,
                int id_add_save, int id_add_cancel);
    void destroy();

    // Re-point the cached font after a DPI change.
    void set_fonts(HFONT font) { font_ = font; }

    // Registered projects for the scope picker. Pulled lazily so opening Manage always sees the
    // current workspace store instead of a snapshot taken at window creation.
    void set_project_provider(std::function<ProjectList()> provider) { project_provider_ = std::move(provider); }
    void set_active_project_provider(std::function<std::string()> provider) { active_project_provider_ = std::move(provider); }

    void show();
    void hide();
    bool visible() const { return visible_; }

    void layout(const RECT& area);
    void refresh(McpManager& mgr);

    bool handle_command(int id, WORD notify, McpManager& mgr, const std::wstring& mcp_path, HWND owner);

    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

    const std::string& selected_id() const { return selected_id_; }
    HWND list_hwnd() const { return list_; }

    // Select a connection by id (no-op if missing). Used for NeedsReauth deep-links.
    void select_connection(const std::string& id, McpManager& mgr);

    // Enter / Esc contract for fields in this panel (0 = nothing to do).
    UINT default_command(HWND /*field*/) const {
        if (mode_ == Mode::Add) {
            return static_cast<UINT>(id_add_save_);
        }
        if (mode_ == Mode::Edit) {
            return IdEditSave;
        }
        return 0u;
    }
    UINT cancel_command() const {
        if (mode_ == Mode::Add) {
            return static_cast<UINT>(id_add_cancel_);
        }
        if (mode_ == Mode::Edit) {
            return IdEditCancel;
        }
        return 0u;
    }

private:
    enum class Mode { Browse, Add, Edit };

    void set_mode(Mode mode);
    void update_detail(const McpManager& mgr);
    void update_button_state(const McpManager& mgr);
    McpConnection* selected(McpManager& mgr);
    const McpConnection* selected(const McpManager& mgr) const;
    bool persist(McpManager& mgr, const std::wstring& mcp_path);
    bool commit_add(McpManager& mgr, const std::wstring& mcp_path, HWND owner);
    bool commit_edit(McpManager& mgr, const std::wstring& mcp_path, HWND owner);
    void fill_templates();
    void fill_transport(HWND select, McpTransportKind kind);
    McpTransportKind transport_of(HWND select) const;
    // Add-form transport is template-driven except for Custom, which the user picks.
    bool add_is_custom() const;
    void sync_add_transport_row();
    void sync_endpoint_labels(HWND label, HWND field, McpTransportKind kind);
    void begin_edit(const McpConnection& c);
    void fill_scope_list(const std::vector<std::string>& scope);
    void sync_scope_enabled();
    std::vector<std::string> collect_scope() const;
    void run_test(McpManager& mgr, const std::wstring& mcp_path, HWND owner);
    std::wstring row_primary(const McpConnection& c) const;
    std::wstring row_secondary(const McpConnection& c) const;
    std::wstring auth_badge(McpAuthState s) const;
    std::vector<HWND> browse_controls() const;
    std::vector<HWND> add_controls() const;
    std::vector<HWND> edit_controls() const;

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HWND heading_ = nullptr;
    HWND desc_ = nullptr;
    HWND list_ = nullptr;
    HWND detail_ = nullptr;
    HWND btn_add_ = nullptr;
    HWND btn_add_account_ = nullptr;
    HWND btn_manage_ = nullptr;
    HWND btn_reauth_ = nullptr;
    HWND btn_check_ = nullptr;
    HWND btn_test_ = nullptr;
    HWND btn_disable_ = nullptr;
    HWND btn_disconnect_ = nullptr;
    HWND btn_remove_ = nullptr;
    HWND add_lbl_template_ = nullptr;
    HWND add_template_ = nullptr;
    HWND add_lbl_name_ = nullptr;
    HWND add_name_ = nullptr;
    HWND add_lbl_alias_ = nullptr;
    HWND add_alias_ = nullptr;
    HWND add_lbl_transport_ = nullptr;
    HWND add_transport_ = nullptr;
    HWND add_lbl_endpoint_ = nullptr;
    HWND add_endpoint_ = nullptr;
    HWND btn_add_save_ = nullptr;
    HWND btn_add_cancel_ = nullptr;
    HWND edit_lbl_name_ = nullptr;
    HWND edit_name_ = nullptr;
    HWND edit_lbl_alias_ = nullptr;
    HWND edit_alias_ = nullptr;
    HWND edit_lbl_transport_ = nullptr;
    HWND edit_transport_ = nullptr;
    HWND edit_lbl_endpoint_ = nullptr;
    HWND edit_endpoint_ = nullptr;
    HWND edit_enabled_ = nullptr;
    HWND edit_scope_all_ = nullptr;
    HWND edit_lbl_scope_ = nullptr;
    HWND edit_scope_list_ = nullptr;
    HWND btn_edit_save_ = nullptr;
    HWND btn_edit_cancel_ = nullptr;

    int id_list_ = 0;
    int id_detail_ = 0;
    int id_btn_add_ = 0;
    int id_btn_add_account_ = 0;
    int id_btn_manage_ = 0;
    int id_btn_reauth_ = 0;
    int id_btn_check_ = 0;
    int id_btn_disable_ = 0;
    int id_btn_disconnect_ = 0;
    int id_btn_remove_ = 0;
    int id_add_template_ = 0;
    int id_add_name_ = 0;
    int id_add_alias_ = 0;
    int id_add_endpoint_ = 0;
    int id_add_save_ = 0;
    int id_add_cancel_ = 0;

    bool visible_ = false;
    Mode mode_ = Mode::Browse;
    std::string selected_id_;
    std::string edit_id_;
    std::vector<std::string> row_ids_;
    std::vector<std::wstring> row_primary_;
    std::vector<std::wstring> row_secondary_;
    ProjectList scope_projects_;
    std::vector<bool> scope_checked_;
    std::function<ProjectList()> project_provider_;
    std::function<std::string()> active_project_provider_;
    RECT area_{};
};

}  // namespace scyllagpt
