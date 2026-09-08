#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "scyllagpt/knowledge.h"

#include <string>
#include <vector>

namespace scyllagpt {

// Settings → Knowledge & Skills panel (ui_kit).
class KnowledgeSettingsUi {
public:
    enum : UINT {
        IdList = 2401,
        IdAddKnowledge = 2402,
        IdAddSkills = 2403,
        IdEdit = 2404,
        IdRemove = 2405,
        IdName = 2406,
        IdType = 2407,
        IdAccess = 2408,
        IdAlias = 2409,
        IdAgent = 2410,
        IdSaveForm = 2411,
        IdCancelForm = 2412,
        IdLblName = 2413,
        IdLblType = 2414,
        IdLblAccess = 2415,
        IdLblAlias = 2416,
        IdHeading = 2417,
        IdDesc = 2418,
        IdEmptyHost = 2419,
        IdEnabled = 2420,
        IdRefreshHealth = 2421,
        IdManagePerms = 2422,
        IdDetail = 2423,
        IdOverrideList = 2424,
        IdOverridePath = 2425,
        IdOverrideMode = 2426,
        IdOverrideAdd = 2427,
        IdOverrideRemove = 2428,
        IdOverrideBack = 2429,
        IdOverrideBrowse = 2430,
        IdLblOverridePath = 2431,
        IdLblOverrideMode = 2432,
        IdOverrideEmptyHost = 2433,
        IdLast = IdOverrideEmptyHost,
    };

    bool create(HWND parent, HINSTANCE inst, HFONT font);
    void destroy();

    // Re-point the cached font after a DPI change.
    void set_fonts(HFONT font) { font_ = font; }

    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void layout(const RECT& area);
    void reload(KnowledgeStore& store, const std::string& project_id);

    bool on_command(WORD id, WORD notify, HWND owner, KnowledgeStore& store, const std::wstring& persist_path,
                    const std::string& project_id);

    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

    // Enter / Esc contract for fields in this panel (0 = nothing to do).
    UINT default_command(HWND /*field*/) const {
        if (perm_mode_) {
            return IdOverrideAdd;
        }
        return form_mode_ ? IdSaveForm : 0;
    }
    UINT cancel_command() const {
        if (perm_mode_) {
            return IdOverrideBack;
        }
        return form_mode_ ? IdCancelForm : 0;
    }

    void hide_all();

private:
    void show_list_mode();
    void show_form_mode(bool editing);
    void show_perm_mode();
    void fill_list(KnowledgeStore& store, const std::string& project_id);
    void fill_override_list(const KnowledgeSource& s);
    void load_form_from_source(const KnowledgeSource& s);
    void update_detail_text(const KnowledgeSource& s);
    KnowledgeSource read_form(const KnowledgeSource* base) const;
    std::string selected_id() const;
    std::wstring selected_override_path() const;
    bool pick_folder(HWND owner, std::wstring& out, const wchar_t* title);
    void persist(KnowledgeStore& store, const std::wstring& persist_path);
    void update_empty_visibility();
    void update_action_enabled();
    bool open_selected_for_edit(KnowledgeStore& store, HWND owner);
    bool remove_selected(KnowledgeStore& store, HWND owner, const std::wstring& persist_path);

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    bool visible_ = false;
    bool form_mode_ = false;
    bool perm_mode_ = false;
    bool editing_ = false;
    bool empty_ = true;
    bool overrides_empty_ = true;
    std::string editing_id_;
    std::string pending_add_id_;  // staged add; removed if the form is cancelled
    std::string project_id_;
    std::vector<std::string> list_ids_;
    std::vector<std::wstring> list_primary_;
    std::vector<std::wstring> list_secondary_;
    std::vector<std::wstring> override_paths_;
    std::vector<std::wstring> override_primary_;
    std::vector<std::wstring> override_secondary_;

    HWND heading_ = nullptr;
    HWND desc_ = nullptr;
    HWND list_ = nullptr;
    HWND empty_host_ = nullptr;
    HWND btn_add_knowledge_ = nullptr;
    HWND btn_add_skills_ = nullptr;
    HWND btn_edit_ = nullptr;
    HWND btn_remove_ = nullptr;
    HWND btn_refresh_ = nullptr;

    HWND lbl_name_ = nullptr;
    HWND edit_name_ = nullptr;
    HWND lbl_type_ = nullptr;
    HWND select_type_ = nullptr;
    HWND lbl_access_ = nullptr;
    HWND select_access_ = nullptr;
    HWND lbl_alias_ = nullptr;
    HWND edit_alias_ = nullptr;
    HWND check_agent_ = nullptr;
    HWND check_enabled_ = nullptr;
    HWND detail_ = nullptr;
    HWND btn_manage_perms_ = nullptr;
    HWND btn_save_ = nullptr;
    HWND btn_cancel_ = nullptr;

    HWND override_list_ = nullptr;
    HWND override_empty_host_ = nullptr;
    HWND lbl_override_path_ = nullptr;
    HWND edit_override_path_ = nullptr;
    HWND lbl_override_mode_ = nullptr;
    HWND select_override_mode_ = nullptr;
    HWND btn_override_browse_ = nullptr;
    HWND btn_override_add_ = nullptr;
    HWND btn_override_remove_ = nullptr;
    HWND btn_override_back_ = nullptr;
};

}  // namespace scyllagpt
