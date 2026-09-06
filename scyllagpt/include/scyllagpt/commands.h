#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class ContentView {
    Editor,
    Settings,
    Access,
    Diagnostics,
    Shortcuts,
    GettingStarted,
};

enum class SettingsSection {
    Providers,
    Editor,
    Advanced,
};

// Shared command / control IDs (menu, accelerators, toolbar, center-pane).
enum CmdId : int {
    Cmd_SignIn = 1001,
    Cmd_SignOut,
    Cmd_NewChat,
    Cmd_Send,
    Cmd_Stop,
    Cmd_Settings,
    Cmd_OpenFolder,
    Cmd_OpenFile,
    Cmd_CloseTab,
    Cmd_Goto,
    Cmd_Replace,
    Cmd_FocusComposer,
    Cmd_ToggleFiles,
    Cmd_ToggleHistory,
    Cmd_FocusEditor,
    Cmd_TabEditor,
    Cmd_TabAgent,
    Cmd_Models,
    Cmd_Transcript,
    Cmd_Composer,
    Cmd_Status,
    Cmd_Account,
    Cmd_HomeHint,
    Cmd_Threads,
    Cmd_Tree,
    Cmd_Filter,
    Cmd_Search,
    Cmd_Editor,
    Cmd_EditorStatus,
    Cmd_HdrFiles,
    Cmd_HdrEditor,
    Cmd_HdrAgent,
    Cmd_HdrHistory,
    Cmd_Project,
    Cmd_AgentHint,
    Cmd_Tabs,
    Cmd_Find,
    Cmd_ToggleFind,
    Cmd_Save,
    Cmd_Scope,
    Cmd_Ctx,
    Cmd_AddFile,
    Cmd_AddSel,
    Cmd_CodexExe = 2001,
    Cmd_PinChat,
    Cmd_ArchiveChat,
    Cmd_RenameChat,
    Cmd_ChatTabs,
    Cmd_RemoveCtx,
    Cmd_RefreshFiles,
    Cmd_CopyRuntime,
    Cmd_EmptyOpenFile,
    Cmd_EmptyOpenFolder,
    Cmd_Exit = 2100,
    Cmd_EditUndo,
    Cmd_EditRedo,
    Cmd_EditCut,
    Cmd_EditCopy,
    Cmd_EditPaste,
    Cmd_EditSelectAll,
    Cmd_ViewWrap,
    Cmd_ViewWhitespace,
    Cmd_ClearCtx,
    Cmd_AccessShow,
    Cmd_AccessFolders,
    Cmd_HelpAbout,
    Cmd_HelpShortcuts,
    Cmd_HelpDiag,
    Cmd_AiProviders,
    Cmd_PermInfo,
    Cmd_HelpGettingStarted,
    Cmd_ContentBack = 2300,
    Cmd_ContentNav,
    Cmd_ContentBody,
    Cmd_SetOaStatus,
    Cmd_SetOaSignIn,
    Cmd_SetOaSignOut,
    Cmd_SetClStatus,
    Cmd_SetClKey,
    Cmd_SetClCode,
    Cmd_SetClDisc,
    Cmd_SetDefLabel,
    Cmd_SetDefCombo,
    Cmd_SetCodex,
    Cmd_SetCopyRuntime,
    Cmd_SetWrap,
    Cmd_SetWhitespace,
    Cmd_SetEnterSends,
    Cmd_GsOpenFolder,
    Cmd_GsProviders,
};

struct CommandUiState {
    bool has_dirty_doc = false;
    bool has_active_doc = false;
    bool generating = false;
    bool word_wrap = false;
    bool show_whitespace = false;
    bool files_visible = false;
    bool history_visible = false;
    bool focus_editor = false;
    bool has_selection_context = false;
    bool has_context_chips = false;
};

void menu_apply_command_state(HMENU popup, const CommandUiState& st);

const wchar_t* content_view_title(ContentView view);
const wchar_t* settings_section_label(SettingsSection section);

}  // namespace scyllagpt
