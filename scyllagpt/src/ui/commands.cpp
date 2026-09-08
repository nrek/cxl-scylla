#include "scyllagpt/commands.h"

namespace scyllagpt {

void menu_apply_command_state(HMENU popup, const CommandUiState& st) {
    if (!popup) {
        return;
    }
    auto set_en = [&](int id, bool on) {
        EnableMenuItem(popup, static_cast<UINT>(id), MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    };
    auto set_ck = [&](int id, bool on) {
        CheckMenuItem(popup, static_cast<UINT>(id), MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };

    set_en(Cmd_Save, st.has_active_doc);
    set_en(Cmd_CloseTab, st.has_active_doc);
    set_en(Cmd_Stop, st.generating);
    set_en(Cmd_Find, st.has_active_doc);
    set_en(Cmd_Replace, st.has_active_doc);
    set_en(Cmd_Goto, st.has_active_doc);
    set_en(Cmd_AddFile, st.has_active_doc);
    set_en(Cmd_AddSel, st.has_active_doc);
    set_en(Cmd_ClearCtx, st.has_context_chips);

    set_ck(Cmd_ViewWrap, st.word_wrap);
    set_ck(Cmd_ViewWhitespace, st.show_whitespace);
    set_ck(Cmd_ToggleFiles, st.files_visible);
    set_ck(Cmd_ToggleHistory, st.history_visible);
    set_ck(Cmd_FocusEditor, st.focus_editor);
    set_ck(Cmd_ToggleTerminal, st.terminal_visible);
}

const wchar_t* content_view_title(ContentView view) {
    switch (view) {
        case ContentView::Settings:
            return L"Settings";
        case ContentView::Access:
            return L"Project Security";
        case ContentView::Diagnostics:
            return L"Diagnostics";
        case ContentView::Shortcuts:
            return L"Keyboard Shortcuts";
        case ContentView::GettingStarted:
            return L"Getting Started";
        case ContentView::Keyring:
            return L"Scylla Keyring";
        default:
            return L"";
    }
}

const wchar_t* settings_section_label(SettingsSection section) {
    switch (section) {
        case SettingsSection::Providers:
            return L"AI Providers";
        case SettingsSection::Editor:
            return L"Editor";
        case SettingsSection::Terminal:
            return L"Terminal";
        case SettingsSection::Knowledge:
            return L"Knowledge/Skills";
        case SettingsSection::Mcp:
            return L"MCP";
        case SettingsSection::Strata:
            return L"STRATA";
        case SettingsSection::Security:
            return L"Security";
        case SettingsSection::Advanced:
            return L"Advanced";
        default:
            return L"Settings";
    }
}

}  // namespace scyllagpt
