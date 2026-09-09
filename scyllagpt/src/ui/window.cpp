#include "scyllagpt/window.h"
#include "scyllagpt/workflow.h"
#include "scyllagpt/scylla_query_skill.h"
#include "scyllagpt/composer_tokens.h"
#include "scyllagpt/composer_metrics.h"
#include "scyllagpt/chat_history.h"
#include "scyllagpt/chat_log_view.h"
#include "scyllagpt/chat_list.h"
#include "scyllagpt/agent_files.h"
#include "scyllagpt/resource.h"

#include "scyllagpt/broker_mcp.h"
#include "scyllagpt/broker_service.h"
#include "scyllagpt/connection_broker.h"
#include "scyllagpt/document.h"
#include "scyllagpt/editor_host.h"
#include "scyllagpt/commands.h"
#include "scyllagpt/ssh_query_executor.h"
#include "scyllagpt/connections_settings_ui.h"
#include "scyllagpt/environment_settings_ui.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/keyring_ui.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/knowledge_settings_ui.h"
#include "scyllagpt/layout.h"
#include "scyllagpt/mcp_manager.h"
#include "scyllagpt/mcp_oauth.h"
#include "scyllagpt/mcp_settings_ui.h"
#include "scyllagpt/providers_settings_ui.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/project_environment.h"
#include "scyllagpt/project_connection.h"
#include "scyllagpt/security_overview_ui.h"
#include "scyllagpt/security_policy_ui.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/session.h"
#include "scyllagpt/store.h"
#include "scyllagpt/strata_bridge.h"
#include "scyllagpt/strata_client.h"
#include "scyllagpt/strata_settings_ui.h"
#include "scyllagpt/terminal_host.h"
#include "scyllagpt/terminal_profiles.h"
#include "scyllagpt/terminal_session.h"
#include "scyllagpt/terminal_settings_ui.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_gallery.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/utf.h"
#include "scyllagpt/workbench_panel.h"

#include "Scintilla.h"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

namespace scyllagpt {
namespace {

constexpr UINT WM_SCYLLA_LINE = WM_APP + 41;
constexpr UINT WM_SCYLLA_CLOSE_SEL = WM_APP + 42;
constexpr UINT WM_SCYLLA_PICK_SEL = WM_APP + 45;
constexpr UINT WM_SCYLLA_OPEN_SEL = WM_APP + 46;
constexpr UINT WM_SCYLLA_TERMINAL_OUT = WM_APP + 40;
constexpr UINT WM_SCYLLA_MCP_AUTH = WM_APP + 47;
// Defer panel chrome layout off BN_CLICKED / LBUTTONDOWN — SetWindowPos/RedrawWindow on the
// control still inside its notify handler crashes (surface tabs after hosts parented under content_).
constexpr UINT WM_SCYLLA_RELAYOUT = WM_APP + 48;
// Sent (never posted) by a broker pipe worker so the Keyring-touching half of a query runs on the
// UI thread. SendMessage blocks the worker, which is exactly the handoff we want.
constexpr UINT WM_SCYLLA_BROKER_PREPARE = WM_APP + 49;
// Posted by the connection-test worker with an owned result string once the SSH round trip returns.
constexpr UINT WM_SCYLLA_TEST_RESULT = WM_APP + 50;
constexpr WPARAM kRelayoutEnsurePanel = 1;
constexpr wchar_t kSelectorClass[] = L"ScyllaGPTSelectorPopup";

constexpr COLORREF kWindow = RGB(0x0C, 0x0F, 0x12);   // theme.app_bg
constexpr COLORREF kFiles = RGB(0x10, 0x13, 0x17);    // theme.panel / navigation
constexpr COLORREF kEditor = RGB(0x10, 0x13, 0x17);   // settings content = panel (no striping)
constexpr COLORREF kAgent = RGB(0x14, 0x18, 0x1D);    // theme.surface
constexpr COLORREF kHistory = RGB(0x10, 0x13, 0x17);
constexpr COLORREF kInput = RGB(0x0F, 0x13, 0x17);
constexpr COLORREF kText = RGB(0xD7, 0xDC, 0xE2);
constexpr COLORREF kMuted = RGB(0x72, 0x7C, 0x87);
constexpr COLORREF kAccent = RGB(0xE6, 0x94, 0x05);
constexpr COLORREF kYouBody = RGB(0xA2, 0xAB, 0xB5);
constexpr COLORREF kAsst = RGB(0xD7, 0xDC, 0xE2);
constexpr COLORREF kBorder = RGB(0x30, 0x37, 0x40);
constexpr COLORREF kRule = RGB(0x26, 0x2C, 0x33);

enum {
    ID_SIGNIN = Cmd_SignIn,
    ID_SIGNOUT = Cmd_SignOut,
    ID_NEW = Cmd_NewChat,
    ID_SEND = Cmd_Send,
    ID_CANCEL = Cmd_Stop,
    ID_SETTINGS = Cmd_Settings,
    ID_OPEN_FOLDER = Cmd_OpenFolder,
    ID_OPEN_FILE = Cmd_OpenFile,
    ID_CLOSE_TAB = Cmd_CloseTab,
    ID_GOTO = Cmd_Goto,
    ID_REPLACE = Cmd_Replace,
    ID_FOCUS_COMPOSER = Cmd_FocusComposer,
    ID_TOGGLE_FILES = Cmd_ToggleFiles,
    ID_TOGGLE_HISTORY = Cmd_ToggleHistory,
    ID_FOCUS = Cmd_FocusEditor,
    ID_TAB_EDITOR = Cmd_TabEditor,
    ID_TAB_AGENT = Cmd_TabAgent,
    ID_MODELS = Cmd_Models,
    ID_TRANSCRIPT = Cmd_Transcript,
    ID_COMPOSER = Cmd_Composer,
    ID_STATUS = Cmd_Status,
    ID_ACCOUNT = Cmd_Account,
    ID_HOMEHINT = Cmd_HomeHint,
    ID_THREADS = Cmd_Threads,
    ID_WORKFLOW = 29004,
    ID_KNOWLEDGE_TREE = 29001,
    ID_KNOWLEDGE_HEADER = 29002,
    ID_KNOWLEDGE_MANAGE = 29003,
    ID_MARKDOWN_VIEW = 29004,
    ID_MARKDOWN_TOGGLE = 29005,
    ID_TREE = Cmd_Tree,
    ID_FILTER = Cmd_Filter,
    ID_SEARCH = Cmd_Search,
    ID_EDITOR = Cmd_Editor,
    ID_EDITOR_STATUS = Cmd_EditorStatus,
    ID_HDR_FILES = Cmd_HdrFiles,
    ID_HDR_EDITOR = Cmd_HdrEditor,
    ID_HDR_AGENT = Cmd_HdrAgent,
    ID_HDR_HISTORY = Cmd_HdrHistory,
    ID_PROJECT = Cmd_Project,
    ID_AGENT_HINT = Cmd_AgentHint,
    ID_TABS = Cmd_Tabs,
    ID_FIND = Cmd_Find,
    ID_TOGGLE_FIND = Cmd_ToggleFind,
    ID_SAVE = Cmd_Save,
    ID_SCOPE = Cmd_Scope,
    ID_CTX = Cmd_Ctx,
    ID_ADD_FILE = Cmd_AddFile,
    ID_ADD_SEL = Cmd_AddSel,
    ID_CODEX_EXE = Cmd_CodexExe,
    ID_PIN_CHAT = Cmd_PinChat,
    ID_ARCHIVE_CHAT = Cmd_ArchiveChat,
    ID_RENAME_CHAT = Cmd_RenameChat,
    ID_CHAT_TABS = Cmd_ChatTabs,
    ID_REMOVE_CTX = Cmd_RemoveCtx,
    ID_REFRESH_FILES = Cmd_RefreshFiles,
    ID_COPY_RUNTIME = Cmd_CopyRuntime,
    ID_EMPTY_OPEN_FILE = Cmd_EmptyOpenFile,
    ID_EMPTY_OPEN_FOLDER = Cmd_EmptyOpenFolder,
    ID_EXIT = Cmd_Exit,
    ID_EDIT_UNDO = Cmd_EditUndo,
    ID_EDIT_REDO = Cmd_EditRedo,
    ID_EDIT_CUT = Cmd_EditCut,
    ID_EDIT_COPY = Cmd_EditCopy,
    ID_EDIT_PASTE = Cmd_EditPaste,
    ID_EDIT_SELECT_ALL = Cmd_EditSelectAll,
    ID_VIEW_WRAP = Cmd_ViewWrap,
    ID_VIEW_WHITESPACE = Cmd_ViewWhitespace,
    ID_TOGGLE_TERMINAL = Cmd_ToggleTerminal,
    ID_NEW_TERMINAL = Cmd_NewTerminal,
    ID_CLEAR_CTX = Cmd_ClearCtx,
    ID_ACCESS_SHOW = Cmd_AccessShow,
    ID_ACCESS_FOLDERS = Cmd_AccessFolders,
    ID_ACCESS_KEYRING = Cmd_AccessKeyring,
    ID_HELP_ABOUT = Cmd_HelpAbout,
    ID_HELP_SHORTCUTS = Cmd_HelpShortcuts,
    ID_HELP_DIAG = Cmd_HelpDiag,
    ID_AI_PROVIDERS = Cmd_AiProviders,
    ID_PERM_INFO = Cmd_PermInfo,
    ID_HELP_GETTING_STARTED = Cmd_HelpGettingStarted,
    ID_CONTENT_BACK = Cmd_ContentBack,
    ID_CONTENT_NAV = Cmd_ContentNav,
    ID_CONTENT_BODY = Cmd_ContentBody,
    ID_SET_CODEX = Cmd_SetCodex,
    ID_SET_COPY_RUNTIME = Cmd_SetCopyRuntime,
    ID_SET_WRAP = Cmd_SetWrap,
    ID_SET_WHITESPACE = Cmd_SetWhitespace,
    ID_SET_ENTER_SENDS = Cmd_SetEnterSends,
    ID_GS_OPEN_FOLDER = Cmd_GsOpenFolder,
    ID_GS_PROVIDERS = Cmd_GsProviders,
    ID_TERMINAL = Cmd_Terminal,
    ID_PANEL_PROBLEMS = Cmd_PanelShowProblems,
    ID_PANEL_OUTPUT = Cmd_PanelShowOutput,
    ID_PANEL_PORTS = Cmd_PanelShowPorts,
    ID_UI_GALLERY = Cmd_UiGallery,
    ID_MCP_LIST = Cmd_McpList,
    ID_MCP_DETAIL = Cmd_McpDetail,
    ID_MCP_ADD = Cmd_McpAdd,
    ID_MCP_ADD_ACCOUNT = Cmd_McpAddAccount,
    ID_MCP_MANAGE = Cmd_McpManage,
    ID_MCP_REAUTH = Cmd_McpReauth,
    ID_MCP_CHECK = Cmd_McpCheck,
    ID_MCP_DISABLE = Cmd_McpDisable,
    ID_MCP_DISCONNECT = Cmd_McpDisconnect,
    ID_MCP_REMOVE = Cmd_McpRemove,
    ID_MCP_ADD_TEMPLATE = Cmd_McpAddTemplate,
    ID_MCP_ADD_NAME = Cmd_McpAddName,
    ID_MCP_ADD_ALIAS = Cmd_McpAddAlias,
    ID_MCP_ADD_ENDPOINT = Cmd_McpAddEndpoint,
    ID_MCP_ADD_SAVE = Cmd_McpAddSave,
    ID_MCP_ADD_CANCEL = Cmd_McpAddCancel,
    ID_MCP_ALIAS_POPUP = Cmd_McpAliasPopup,
};

struct TreeNode {
    std::wstring path;
    bool dir = false;
    bool loaded = false;
    enum class Kind { Project, KnowledgeHeader, KnowledgeSource, KnowledgeEntry };
    Kind kind = Kind::Project;
    std::string source_id;
};

struct OpenDoc {
    std::wstring path;
    std::wstring text;
    TextEnc enc = TextEnc::Utf8;
    bool crlf = false;
    std::uint64_t hash = 0;
    bool dirty = false;
    bool preview = true;
    bool readonly = false;
    EditorViewState view{};
    IndentInfo indent{};
    std::string language;
    bool large_file = false;
    bool from_knowledge = false;
    bool markdown_source = false;
    std::string knowledge_source_id;
    std::wstring knowledge_label;
};

struct AttachedImage {
    std::string id;
    std::wstring path;
    HBITMAP thumb = nullptr;
};

struct Ui {
    Session session;
    HWND wnd = nullptr;
    HWND brand = nullptr;
    HWND project = nullptr;
    HWND openfolder = nullptr;
    HWND toggle_files = nullptr;
    HWND toggle_history = nullptr;
    HWND focus = nullptr;
    HWND settings = nullptr;
    HWND account = nullptr;
    HWND signin = nullptr;
    HWND signout = nullptr;
    HWND hdr_files = nullptr;
    HWND filter = nullptr;
    HWND tree = nullptr;
    HWND knowledge_tree = nullptr;
    HWND knowledge_header = nullptr;
    HWND knowledge_manage = nullptr;
    bool knowledge_collapsed = false;
    HWND hdr_editor = nullptr;
    HWND tabs = nullptr;
    HWND find = nullptr;
    HWND find_toggle = nullptr;
    HWND save = nullptr;
    bool find_open = false;
    HWND editor = nullptr;
    HWND markdown_view = nullptr;
    HWND markdown_toggle = nullptr;
    long markdown_stream_start = -1;
    bool agent_heading_pending = false;
    HWND editor_status = nullptr;
    HWND gutter = nullptr;
    HWND empty_editor = nullptr;
    HWND empty_open_file = nullptr;
    HWND empty_open_folder = nullptr;
    HWND empty_agent = nullptr;
    HWND empty_chats = nullptr;
    HWND composer_panel = nullptr;
    HWND composer_cue = nullptr;
    HWND hdr_agent = nullptr;
    HWND chat_tabs = nullptr;
    HWND models = nullptr;
    HWND neu = nullptr;
    HWND agent_hint = nullptr;
    HWND transcript = nullptr;
    HWND chat_log = nullptr;
    HWND activity = nullptr;
    std::wstring activity_text;
    HWND composer = nullptr;
    int composer_lines = kComposerMinLines;  // visible text lines; grows with the draft
    HWND send = nullptr;
    HWND cancel = nullptr;
    HWND hdr_history = nullptr;
    HWND scope = nullptr;
    HWND search = nullptr;
    HWND threads = nullptr;
    HWND ctx = nullptr;
    HWND add_file = nullptr;
    HWND workflow = nullptr;
    bool suppress_submit_char = false;
    HWND pin = nullptr;
    HWND archive = nullptr;
    HWND tab_editor = nullptr;
    HWND tab_agent = nullptr;
    HWND status = nullptr;
    HWND homehint = nullptr;
    HWND tips = nullptr;
    HWND sel_list = nullptr;
    HWND sel_face = nullptr;
    int sel_owner = 0;
    DWORD sel_closed_tick = 0;
    std::vector<std::wstring> sel_items;
    std::vector<ModelChoice> sel_models;
    int sel_cur = 0;
    int sel_hot = -1;
    int sel_scroll = 0;
    int sel_visible = 0;
    int sel_row = 28;
    RECT composer_box{};
    RECT thumb_row{};
    std::vector<AttachedImage> images;
    std::unordered_map<std::wstring, std::vector<DisplayAttachment>> message_attachments;
    HWND attachment_tray = nullptr;
    HWND attachment_lightbox = nullptr;
    WNDPROC tabs_prev = nullptr;
    WNDPROC chat_tabs_prev = nullptr;
    WNDPROC panel_prev = nullptr;
    HFONT font = nullptr;
    HFONT font_small = nullptr;
    HFONT font_semi = nullptr;
    HFONT font_title = nullptr;
    HFONT font_mono = nullptr;
    HBRUSH bg = nullptr;
    HBRUSH files = nullptr;
    HBRUSH editor_br = nullptr;
    HBRUSH agent_br = nullptr;
    HBRUSH history_br = nullptr;
    HBRUSH input_br = nullptr;
    std::string shown_stream;
    // -1 until the first chrome refresh; avoids repainting the empty-state pane on every delta.
    int chat_empty_state = -1;
    bool ime_composing = false;
    WNDPROC composer_prev = nullptr;
    WNDPROC filter_prev = nullptr;
    WNDPROC search_prev = nullptr;
    WNDPROC find_prev = nullptr;
    int drag = 0;  // 1 files|editor, 2 editor|agent, 3 agent|history, 4 terminal, 5 knowledge
    int drag_origin = 0;
    int files_w0 = 0;
    int agent_w0 = 0;
    int history_w0 = 0;
    int terminal_h0 = 0;
    int knowledge_h0 = 0;
    int knowledge_height = 0;
    int knowledge_max_height = 0;
    RECT split1{};
    RECT split2{};
    RECT split3{};
    RECT split_term{};
    RECT split_knowledge{};
    PaneLayout panes{};
    int narrow_tab = 0;
    bool focus_restore_files = false;
    bool focus_restore_history = false;
    std::wstring editor_path;
    std::vector<std::string> thread_ids;
    std::vector<std::wstring> chat_groups;
    bool restore_chat_pending = true;
    std::vector<OpenDoc> docs;
    int active_doc = -1;
    bool suppress_edit = false;

    ContentView content_view = ContentView::Editor;
    SettingsSection settings_section = SettingsSection::Providers;
    SecuritySubpage security_subpage = SecuritySubpage::Overview;
    int claude_auth_polls = 0;  // remaining WM_TIMER ticks to re-query `claude auth status`
    HWND content_host = nullptr;
    HWND content_back = nullptr;
    HWND content_title = nullptr;
    HWND content_nav = nullptr;
    HWND content_body = nullptr;
    ProvidersSettingsUi providers_ui;
    HWND set_codex = nullptr;
    HWND set_copy_runtime = nullptr;
    HWND set_wrap = nullptr;
    HWND set_whitespace = nullptr;
    HWND set_enter_sends = nullptr;
    HWND set_ui_gallery = nullptr;
    HWND gs_open_folder = nullptr;
    HWND gs_providers = nullptr;

    TerminalSessionManager terminal_sessions;
    WorkbenchPanel workbench_panel;
    TerminalSettingsUi terminal_ui;
    std::vector<TerminalProfile> terminal_profiles;
    int terminal_profile_sel = 0;
    bool panel_collapsed = false;
    int panel_h_before_maximize = 0;
    // Last dirty-document set pushed to the Problems surface. refresh_tabs runs on every keystroke,
    // so the surface is only rebuilt when the set actually changes.
    std::wstring problems_signature;

    KnowledgeStore knowledge;
    KnowledgeSettingsUi knowledge_ui;
    McpManager mcp;
    McpSettingsUi mcp_ui;
    std::vector<AgentFile> mention_files;
    HWND mcp_alias_popup = nullptr;
    std::vector<std::string> mcp_alias_completions;
    std::vector<std::string> mcp_auth_queue;  // connection ids awaiting silent freshness check
    bool mcp_auth_busy = false;
    int mcp_auth_tick = 0;  // periodic re-queue (~5 min at 2s timer)
    unsigned chat_busy_frame = 0;
    StrataClient strata;
    StrataBridge strata_bridge;
    StrataSettingsUi strata_ui;
    KeyringUi keyring_ui;
    ProjectEnvironmentManager environments;
    ProjectConnectionManager connections;
    // Trusted query broker. The executor and broker hold references, so they are constructed once
    // the Keyring and connection manager exist rather than inline with the struct.
    std::unique_ptr<SshQueryExecutor> query_executor;
    std::unique_ptr<ConnectionBroker> query_broker;
    BrokerPipeService broker_service;
    // Connections → Test connection runs off-thread. Held (not detached) so shutdown can join before
    // the broker it borrows is destroyed.
    std::thread connection_test;
    bool connection_test_running = false;
    EnvironmentSettingsUi environment_ui;
    ConnectionsSettingsUi connections_ui;
    SecurityOverviewUi security_overview;
    SecurityPolicyUi security_policy;
    HWND sec_tab_overview = nullptr;
    HWND sec_tab_keyring = nullptr;
    HWND sec_tab_environments = nullptr;
    HWND sec_tab_connections = nullptr;
    HWND sec_tab_policy = nullptr;
    // Status-bar chip for the active project's environment (click = pick / manage).
    HWND status_env = nullptr;
    UiGallery ui_gallery;
};

Ui* g_ui = nullptr;

std::wstring get_window_text(HWND h);
void do_find(Ui* ui);
void do_goto(Ui* ui);
void layout(Ui* ui);
void append_attachment_footer(Ui* ui, const std::vector<DisplayAttachment>& attachments);
void show_attachment_tray(Ui* ui, const std::wstring& key, HWND source, long position);
void close_attachment_windows(Ui* ui);
void apply_stream(Ui* ui);
void refresh_settings_data(Ui* ui);
void center_single_line_edit(HWND edit, HFONT font);

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd)), 96);
}

// Whether the agent can actually call scylla_query this session. The /scylla-query skill keys every
// claim it makes off this one answer, so it must reflect the live service rather than intent.
bool broker_query_tool_available(Ui* ui) {
    return ui && ui->broker_service.running() && ui->session.broker_mcp_server_registered();
}

// Crosses the worker→UI boundary for WM_SCYLLA_BROKER_PREPARE. Owned by the worker for the whole
// SendMessage, so the UI thread may write through the pointers but must not retain them.
struct BrokerPrepareBridge {
    ConnectionQueryRequest request;
    PreparedOperation* prepared = nullptr;
    bool accepted = false;
};

// UI-thread half of a brokered query: bind the active project, resolve the alias, classify the SQL,
// ask the user when policy demands it, and authorize Keyring values for this one operation. Never
// blocks on the network, so the UI stays responsive.
void broker_prepare_on_ui(Ui* ui, BrokerPrepareBridge* bridge) {
    if (!ui || !bridge || !bridge->prepared || !ui->query_broker) return;
    bridge->request.project_id = ui->session.store.active_project_id;
    if (bridge->request.project_id.empty()) {
        bridge->prepared->response.status = BrokerStatus::ConnectionNotFound;
        bridge->prepared->response.safe_message =
            "No Scylla project is active, so no saved connection can be resolved.";
        return;
    }
    *bridge->prepared = ui->query_broker->prepare(bridge->request);
    if (bridge->prepared->ready) {
        bridge->accepted = true;
        return;
    }
    if (bridge->prepared->response.status != BrokerStatus::ApprovalRequired) return;

    std::wstring prompt = L"The agent asked to run a statement that changes data or schema on \"";
    prompt += utf16(bridge->request.alias);
    prompt += L"\".\n\n";
    prompt += utf16(bridge->prepared->response.assessment.reason.empty()
                        ? bridge->request.sql
                        : bridge->prepared->response.assessment.reason + "\n\n" + bridge->request.sql);
    prompt += L"\n\nRun it?";
    if (MessageBoxW(ui->wnd, prompt.c_str(), L"Scylla — approve brokered query",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;  // keep the ApprovalRequired refusal the agent already has
    }
    ConnectionQueryRequest approved = bridge->request;
    approved.approved = true;
    *bridge->prepared = ui->query_broker->prepare(approved);
    bridge->accepted = bridge->prepared->ready;
}

// Worker-thread entry point handed to BrokerPipeService. Marshals the Keyring-touching phase to the
// UI thread, then runs the blocking SSH execution here.
std::string broker_handle_query(Ui* ui, const QueryToolCall& call) {
    BrokerResponse refused;
    refused.status = BrokerStatus::Disabled;
    refused.safe_message = "The Scylla query broker is not available.";
    if (!ui || !ui->wnd || !ui->query_broker) return encode_broker_response(refused);

    BrokerPrepareBridge bridge;
    bridge.request.operation_id = ProjectConnectionManager::make_id();
    bridge.request.alias = call.connection_alias;
    bridge.request.sql = call.sql;
    PreparedOperation prepared;
    bridge.prepared = &prepared;
    SendMessageW(ui->wnd, WM_SCYLLA_BROKER_PREPARE, 0, reinterpret_cast<LPARAM>(&bridge));
    if (!bridge.accepted) return encode_broker_response(prepared.response);
    return encode_broker_response(ui->query_broker->finish(std::move(prepared)));
}

std::wstring current_executable_path() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return length > 0 ? std::wstring(buffer, length) : std::wstring();
}

// Brings up the broker and tells the agent runtime about it. Until this succeeds the scylla_query
// tool simply does not exist, which is what keeps /scylla-query honest.
void start_query_broker(Ui* ui) {
    if (!ui || ui->broker_service.running()) return;
    const std::wstring exe = current_executable_path();
    if (exe.empty()) return;

    ui->query_executor = std::make_unique<SshQueryExecutor>(
        join_path(ui->session.paths.appdata, L"broker-run"));
    ui->query_broker = std::make_unique<ConnectionBroker>(ui->connections, ui->keyring_ui.keyring(),
                                                         *ui->query_executor);
    std::string error;
    if (!ui->broker_service.start([ui](const QueryToolCall& call) { return broker_handle_query(ui, call); },
                                  &error)) {
        ui->query_broker.reset();
        ui->query_executor.reset();
        return;
    }

    // The helper is this same executable in a secret-free child mode. It receives only the pipe name
    // and the session token; the Keyring stays in this process.
    CodexMcpServer server;
    server.name = "scylla-query";
    server.stdio = true;
    server.command = exe;
    server.arguments = {L"--mcp-query-broker"};
    // Token only: the helper derives the pipe name from it. Emitting the literal pipe path here
    // would be corrupted by the config writer's backslash-to-slash rewriting.
    server.environment = {{utf16(kBrokerTokenEnvVar), utf16(ui->broker_service.token())}};
    ui->session.set_broker_mcp_server(std::move(server));
    ui->session.sync_lockdown_config();
}

// Result of a Connections → Test connection run, handed to the UI thread by pointer.
struct ConnectionTestResult {
    bool ok = false;
    std::wstring message;
};

// Runs the smallest possible real query through the broker so a saved alias can be proven end to end
// before the agent depends on it. Keyring authorization happens here on the UI thread; the SSH round
// trip runs on a detached worker so the window keeps painting.
void start_connection_test(Ui* ui, const std::string& alias) {
    if (!ui || alias.empty()) return;
    if (!ui->query_broker) {
        ui->connections_ui.report_test_result(false, L"The query broker is not running.");
        return;
    }
    if (ui->connection_test_running) {
        ui->connections_ui.report_test_result(false, L"A connection test is already running.");
        return;
    }

    ConnectionQueryRequest request;
    request.operation_id = ProjectConnectionManager::make_id();
    request.project_id = ui->session.store.active_project_id;
    request.alias = alias;
    request.sql = "SELECT 1";
    PreparedOperation prepared = ui->query_broker->prepare(request);
    if (!prepared.ready) {
        ui->connections_ui.report_test_result(false, utf16(prepared.response.safe_message));
        return;
    }

    ui->connections_ui.report_test_result(true, L"Connecting…");
    if (ui->connection_test.joinable()) ui->connection_test.join();  // finished worker from a prior run
    const HWND wnd = ui->wnd;
    ConnectionBroker* broker = ui->query_broker.get();
    ui->connection_test_running = true;
    ui->connection_test = std::thread([wnd, broker, moved = std::move(prepared)]() mutable {
        const BrokerResponse response = broker->finish(std::move(moved));
        auto* result = new ConnectionTestResult{};
        result->ok = response.status == BrokerStatus::Ok;
        result->message = result->ok ? L"Connection succeeded — SELECT 1 returned a row."
                                     : utf16(response.safe_message);
        if (!PostMessageW(wnd, WM_SCYLLA_TEST_RESULT, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    });
}

void stop_query_broker(Ui* ui) {
    if (!ui) return;
    // Join before the broker goes away: the test worker holds a raw pointer into it.
    if (ui->connection_test.joinable()) ui->connection_test.join();
    ui->connection_test_running = false;
    ui->broker_service.stop();
    ui->session.set_broker_mcp_server({});
    ui->query_broker.reset();
    ui->query_executor.reset();
}

int px_to_dip(HWND hwnd, int px) {
    return MulDiv(px, 96, static_cast<int>(GetDpiForWindow(hwnd)));
}

// Height of one composer line, taken from the RichEdit's own default character format so it
// tracks whatever face and size set_rich_colors installed rather than a parallel constant.
int composer_line_height(Ui* ui) {
    const int fallback = dip(ui->wnd, 19);
    if (!ui->composer) return fallback;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_FACE;
    SendMessageW(ui->composer, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    if (cf.yHeight <= 0) return fallback;
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(cf.yHeight, static_cast<int>(GetDpiForWindow(ui->wnd)), 72 * 20);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, cf.szFaceName[0] ? cf.szFaceName : L"Segoe UI", LF_FACESIZE);
    HFONT font = CreateFontIndirectW(&lf);
    if (!font) return fallback;
    HDC dc = GetDC(ui->composer);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm{};
    const bool measured = GetTextMetricsW(dc, &tm) != FALSE;
    if (old) SelectObject(dc, old);
    ReleaseDC(ui->composer, dc);
    DeleteObject(font);
    const int line = tm.tmHeight + tm.tmExternalLeading;
    return (measured && line > 0) ? line : fallback;
}

// Caches the composer's visible line count. Returns true when the height requirement moved, which
// is the only case worth a relayout.
bool sync_composer_growth(Ui* ui) {
    if (!ui || !ui->composer) return false;
    const auto wrapped = static_cast<int>(SendMessageW(ui->composer, EM_GETLINECOUNT, 0, 0));
    const int lines = composer_visible_lines(wrapped);
    if (lines == ui->composer_lines) return false;
    ui->composer_lines = lines;
    return true;
}

HFONT make_font_dip(HWND hwnd, int dips, bool bold, const wchar_t* face) {
    LOGFONTW lf{};
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    lf.lfHeight = -dip(hwnd, dips);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    return CreateFontIndirectW(&lf);
}

void set_rich_colors(HWND edit, COLORREF bg, int size_dip, const wchar_t* face) {
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, bg);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = kText;
    lstrcpynW(cf.szFaceName, face, LF_FACESIZE);
    cf.yHeight = MulDiv(size_dip, 20 * 72, 96);
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
}

void center_single_line_edit(HWND edit, HFONT font) {
    if (!edit) {
        return;
    }
    RECT cr{};
    GetClientRect(edit, &cr);
    if (cr.right <= cr.left || cr.bottom <= cr.top) {
        return;
    }
    HDC dc = GetDC(edit);
    HFONT old = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) {
        SelectObject(dc, old);
    }
    ReleaseDC(edit, dc);
    const int text_h = tm.tmHeight;
    const int box_h = cr.bottom - cr.top;
    const int top = (std::max)(0, (box_h - text_h) / 2);
    const int pad_x = g_ui ? dip(g_ui->wnd, 8) : 8;
    // Symmetric top/bottom so caret sits on the optical mid-line.
    RECT fr{pad_x, top, cr.right - pad_x, box_h - top};
    SendMessageW(edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&fr));
}

void refresh_editor_status(Ui* ui);
void load_doc_into_editor(Ui* ui, OpenDoc& d);

void refresh_editor_status(Ui* ui) {
    if (!ui->editor_status) {
        return;
    }
    if (ui->active_doc < 0 || ui->active_doc >= static_cast<int>(ui->docs.size())) {
        SetWindowTextW(ui->editor_status, L"");
        InvalidateRect(ui->editor_status, nullptr, TRUE);
        return;
    }
    auto& d = ui->docs[ui->active_doc];
    int line = 1;
    int col = 1;
    editor_status(ui->editor, &line, &col);
    const wchar_t* enc = L"UTF-8";
    if (d.enc == TextEnc::Utf8Bom) {
        enc = L"UTF-8 BOM";
    } else if (d.enc == TextEnc::Utf16Le) {
        enc = L"UTF-16 LE";
    }
    std::wstring lang = d.language.empty() ? L"text" : utf16(d.language);
    wchar_t indent[32]{};
    if (d.indent.use_tabs) {
        wcscpy_s(indent, L"Tabs");
    } else {
        swprintf_s(indent, L"Spaces:%d", d.indent.tab_width);
    }
    wchar_t buf[256]{};
    swprintf_s(buf, L"%s · %s · %d:%d · %s · %s%s", lang.c_str(), indent, line, col, enc, d.crlf ? L"CRLF" : L"LF",
               d.large_file ? L" · Large" : L"");
    SetWindowTextW(ui->editor_status, buf);
    InvalidateRect(ui->editor_status, nullptr, TRUE);
}

void load_doc_into_editor(Ui* ui, OpenDoc& d) {
    ui->suppress_edit = true;
    editor_set_readonly(ui->editor, false);
    editor_set_text(ui->editor, d.text);
    d.indent = editor_detect_indent(d.text);
    d.language = detect_language_id(d.path);
    if (d.language == "markdown" && !d.large_file) ui_kit::set_markdown(ui->markdown_view, d.text);
    editor_apply_indent(ui->editor, d.indent);
    editor_apply_chrome(ui->editor, ui->font_mono, 13, static_cast<int>(GetDpiForWindow(ui->wnd)));
    editor_set_language(ui->editor, d.path, d.large_file);
    editor_set_readonly(ui->editor, d.readonly || d.large_file);
    editor_restore_view(ui->editor, d.view);
    ui->suppress_edit = false;
    refresh_editor_status(ui);
}

LRESULT CALLBACK search_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    if (!ui) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    WNDPROC prev = hwnd == ui->filter ? ui->filter_prev : (hwnd == ui->search ? ui->search_prev : ui->find_prev);
    if (!prev) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (hwnd == ui->find && msg == WM_KEYDOWN && wparam == VK_RETURN) {
        std::wstring q = get_window_text(ui->find);
        if (!q.empty() && q[0] >= L'0' && q[0] <= L'9') {
            do_goto(ui);
        } else {
            do_find(ui);
        }
        return 0;
    }
    // Multiline-as-single-line: never insert a newline (filter/search swallow Enter).
    if ((hwnd == ui->filter || hwnd == ui->search) &&
        ((msg == WM_KEYDOWN && wparam == VK_RETURN) || (msg == WM_CHAR && (wparam == L'\r' || wparam == L'\n')))) {
        return 0;
    }
    if (hwnd == ui->find && msg == WM_CHAR && (wparam == L'\r' || wparam == L'\n')) {
        return 0;
    }
    if (hwnd == ui->find && msg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS | CallWindowProcW(prev, hwnd, msg, wparam, lparam);
    }
    // Classic themeless EDIT draws a light 3D edge (white corner ticks). Paint a flat dark frame.
    if (msg == WM_NCPAINT) {
        HDC dc = GetWindowDC(hwnd);
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        const int w = wr.right - wr.left;
        const int h = wr.bottom - wr.top;
        RECT outer{0, 0, w, h};
        HBRUSH fill = CreateSolidBrush(kInput);
        FillRect(dc, &outer, fill);
        DeleteObject(fill);
        HBRUSH edge = CreateSolidBrush(kBorder);
        FrameRect(dc, &outer, edge);
        DeleteObject(edge);
        ReleaseDC(hwnd, dc);
        return 0;
    }
    if (msg == WM_SIZE || msg == WM_SETFONT) {
        const LRESULT result = CallWindowProcW(prev, hwnd, msg, wparam, lparam);
        center_single_line_edit(hwnd, ui->font);
        return result;
    }
    const LRESULT result = CallWindowProcW(prev, hwnd, msg, wparam, lparam);
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(hwnd, nullptr, TRUE);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    }
    if (msg == WM_PAINT && GetWindowTextLengthW(hwnd) == 0 && GetFocus() != hwnd) {
        const wchar_t* cue = hwnd == ui->filter ? L"Find a file…" : (hwnd == ui->search ? L"Search chats…" : L"Find or line…");
        RECT r{};
        GetClientRect(hwnd, &r);
        r.left += dip(ui->wnd, 9);
        r.right -= dip(ui->wnd, 8);
        HDC dc = GetDC(hwnd);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme().muted);
        SelectObject(dc, ui->font);
        DrawTextW(dc, cue, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ReleaseDC(hwnd, dc);
    }
    return result;
}

bool at_bottom(HWND edit) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(edit, SB_VERT, &si);
    if (si.nMax <= static_cast<int>(si.nPage)) {
        return true;
    }
    return si.nPos + static_cast<int>(si.nPage) + 24 >= si.nMax;
}

void append_rich(HWND edit, const std::wstring& text, COLORREF color, bool bold) {
    const BOOL follow = at_bottom(edit);
    CHARRANGE end{-1, -1};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_FACE | CFM_SIZE | CFM_BACKCOLOR | CFM_ITALIC | CFM_LINK | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.crTextColor = color;
    cf.dwEffects = (bold ? CFE_BOLD : 0) | CFE_AUTOBACKCOLOR;
    lstrcpynW(cf.szFaceName, L"Segoe UI", LF_FACESIZE);
    cf.yHeight = MulDiv(12, 20 * 72, 96);
    if (!bold) {
        cf.yHeight = MulDiv(14, 20 * 72, 96);
    }
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    if (follow) {
        SendMessageW(edit, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

void append_divider(HWND edit) {
    // RichEdit has no full-width horizontal-rule primitive. Size the text rule
    // from the current client width so it follows the chat pane at every size.
    RECT client{};
    GetClientRect(edit, &client);
    HDC dc = GetDC(edit);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const int char_width = (std::max)(1, static_cast<int>(metrics.tmAveCharWidth));
    int count = (std::max)(1, static_cast<int>((client.right - client.left) / char_width) - 2);
    // Box-drawing glyphs can be wider than the font's average character. Measure
    // the actual rule and trim it until it fits; never let it wrap.
    SIZE extent{};
    std::wstring rule(static_cast<std::size_t>(count), L'─');
    while (count > 1) {
        GetTextExtentPoint32W(dc, rule.c_str(), static_cast<int>(rule.size()), &extent);
        if (extent.cx <= client.right - client.left - 2) break;
        rule.pop_back();
        --count;
    }
    if (previous) SelectObject(dc, previous);
    ReleaseDC(edit, dc);
    // Hairline rule: low-contrast box-drawing line, not a bubble/border.
    append_rich(edit, L"\r\n" + rule + L"\r\n\r\n", kRule, false);
}

void append_user_message(HWND edit, const std::wstring& text) {
    ShowWindow(edit, SW_SHOW);
    append_rich(edit, L"You\r\n", kText, true);
    ui_kit::append_markdown(edit, text);
}

void append_agent_message(HWND edit, const std::wstring& text) {
    ShowWindow(edit, SW_SHOW);
    append_rich(edit, L"Agent\r\n", kAccent, true);
    ui_kit::append_markdown(edit, text);
}

std::wstring get_window_text(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(n), 0);
    GetWindowTextW(h, s.data(), n + 1);
    return s;
}

std::wstring folder_name(const std::wstring& path) {
    if (path.empty()) {
        return L"No folder";
    }
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos + 1 >= path.size()) {
        return path;
    }
    return path.substr(pos + 1);
}

bool pick_folders(HWND owner, std::vector<std::wstring>& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    dlg->SetTitle(L"Open project folders or repositories");
    const HRESULT shown = dlg->Show(owner);
    if (FAILED(shown)) {
        dlg->Release();
        return false;
    }
    IShellItemArray* items = nullptr;
    if (FAILED(dlg->GetResults(&items)) || !items) {
        dlg->Release();
        return false;
    }
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    items->Release();
    dlg->Release();
    return !out.empty();
}

int name_cmp(const std::wstring& a, const std::wstring& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, SORT_DIGITSASNUMBERS, a.c_str(), -1, b.c_str(), -1, nullptr, nullptr,
                            0);
}

HTREEITEM insert_tree_item(HWND tree, HTREEITEM parent, const std::wstring& name, TreeNode* node, bool expandable) {
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    ins.item.pszText = const_cast<wchar_t*>(name.c_str());
    ins.item.lParam = reinterpret_cast<LPARAM>(node);
    ins.item.cChildren = expandable ? 1 : 0;
    return TreeView_InsertItem(tree, &ins);
}

void fill_dir(HWND tree, HTREEITEM parent, const std::wstring& dir, const std::wstring& filter,
              TreeNode::Kind kind = TreeNode::Kind::Project, const std::string& source_id = {},
              const KnowledgeStore* knowledge = nullptr) {
    std::wstring glob = dir;
    if (!glob.empty() && glob.back() != L'\\' && glob.back() != L'/') {
        glob += L'\\';
    }
    glob += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        auto* n = new TreeNode{};
        n->loaded = true;
        n->kind = kind;
        n->source_id = source_id;
        insert_tree_item(tree, parent, L"(unavailable)", n, false);
        return;
    }
    struct Ent {
        std::wstring name;
        std::wstring path;
        bool dir;
        bool reparse;
    };
    std::vector<Ent> dirs;
    std::vector<Ent> files;
    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        std::wstring name = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && unresolved_environment_directory(name)) {
            continue;
        }
        if (!filter.empty()) {
            std::wstring low = name;
            std::wstring f = filter;
            CharLowerBuffW(low.data(), static_cast<DWORD>(low.size()));
            CharLowerBuffW(f.data(), static_cast<DWORD>(f.size()));
            if (low.find(f) == std::wstring::npos) {
                continue;
            }
        }
        Ent e;
        e.name = name;
        e.path = dir + L"\\" + name;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        // Knowledge cascade: root R/W|RO applies to descendants; No Access overrides are omitted.
        if (knowledge && !source_id.empty()) {
            const EffectiveAccess eff = knowledge->resolve_effective_access(source_id, e.path);
            if (!eff.readable) {
                continue;
            }
        }
        if (e.dir) {
            dirs.push_back(std::move(e));
        } else {
            files.push_back(std::move(e));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    auto less = [](const Ent& a, const Ent& b) { return name_cmp(a.name, b.name) == CSTR_LESS_THAN; };
    std::sort(dirs.begin(), dirs.end(), less);
    std::sort(files.begin(), files.end(), less);
    for (const auto& e : dirs) {
        auto* n = new TreeNode{};
        n->path = e.path;
        n->dir = true;
        n->loaded = e.reparse;  // reparse: do not expand
        n->kind = kind;
        n->source_id = source_id;
        insert_tree_item(tree, parent, e.reparse ? (e.name + L" ↗") : e.name, n, !e.reparse);
    }
    for (const auto& e : files) {
        auto* n = new TreeNode{};
        n->path = e.path;
        n->dir = false;
        n->loaded = true;
        n->kind = kind;
        n->source_id = source_id;
        insert_tree_item(tree, parent, e.name, n, false);
    }
}

void rebuild_tree(Ui* ui) {
    std::vector<std::pair<std::string, std::wstring>> expanded;
    std::vector<std::wstring> expanded_project_roots;
    const bool had_project_items = TreeView_GetRoot(ui->tree) != nullptr;
    for (HTREEITEM item = TreeView_GetRoot(ui->tree); item; item = TreeView_GetNextSibling(ui->tree, item)) {
        TVITEMW value{};
        value.mask = TVIF_PARAM | TVIF_STATE;
        value.stateMask = TVIS_EXPANDED;
        value.hItem = item;
        if (TreeView_GetItem(ui->tree, &value)) {
            const auto* node = reinterpret_cast<TreeNode*>(value.lParam);
            if (node && (value.state & TVIS_EXPANDED)) expanded_project_roots.push_back(node->path);
        }
    }
    std::function<void(HTREEITEM)> remember = [&](HTREEITEM item) {
        for (; item; item = TreeView_GetNextSibling(ui->knowledge_tree, item)) {
            TVITEMW value{};
            value.mask = TVIF_PARAM | TVIF_STATE;
            value.stateMask = TVIS_EXPANDED;
            value.hItem = item;
            TreeView_GetItem(ui->knowledge_tree, &value);
            auto* node = reinterpret_cast<TreeNode*>(value.lParam);
            if (node && (value.state & TVIS_EXPANDED)) expanded.emplace_back(node->source_id, node->path);
            remember(TreeView_GetChild(ui->knowledge_tree, item));
        }
    };
    remember(TreeView_GetRoot(ui->knowledge_tree));
    TreeView_DeleteAllItems(ui->tree);
    TreeView_DeleteAllItems(ui->knowledge_tree);
    std::vector<std::wstring> project_roots;
    if (const auto* active = ui->session.store.active()) project_roots = active->roots;
    if (project_roots.empty() && !ui->session.settings.project_folder.empty())
        project_roots.push_back(ui->session.settings.project_folder);
    for (const auto& project : project_roots) {
        const DWORD attrs = GetFileAttributesW(project.c_str());
        auto* root = new TreeNode{};
        root->loaded = true;
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            root->path = project;
            root->dir = true;
            HTREEITEM item = insert_tree_item(ui->tree, TVI_ROOT, folder_name(project), root, true);
            fill_dir(ui->tree, item, project, get_window_text(ui->filter));
            const bool was_expanded = std::any_of(expanded_project_roots.begin(), expanded_project_roots.end(),
                [&](const auto& path) { return _wcsicmp(path.c_str(), project.c_str()) == 0; });
            if (!had_project_items || was_expanded) TreeView_Expand(ui->tree, item, TVE_EXPAND);
        } else insert_tree_item(ui->tree, TVI_ROOT, L"Folder missing — use Open folder", root, false);
    }
    // Separate KNOWLEDGE / SKILLS sections — never merge into project src tree. Disabled sources
    // are configuration only and stay out of the explorer entirely.
    const std::string pid = ui->session.store.active_project_id;
    const auto sources = ui->knowledge.list_enabled_for_project(pid);
    std::vector<const KnowledgeSource*> knowledge_sources;
    std::vector<const KnowledgeSource*> skills_sources;
    for (const KnowledgeSource* s : sources) {
        if (!s) {
            continue;
        }
        (s->type == SourceType::Skills ? skills_sources : knowledge_sources).push_back(s);
    }

    auto add_source = [&](const KnowledgeSource* source, HTREEITEM parent) {
        auto* node = new TreeNode{};
        node->path = source->path;
        node->dir = true;
        node->kind = TreeNode::Kind::KnowledgeSource;
        node->source_id = source->id;
        std::wstring label = source->label.empty() ? folder_name(source->path) : source->label;
        if (source->access == AccessMode::ReadOnly) label += L" (read-only)";
        insert_tree_item(ui->knowledge_tree, parent, label, node, true);
    };
    for (const auto* source : knowledge_sources) add_source(source, TVI_ROOT);
    if (!skills_sources.empty()) {
        auto* header = new TreeNode{};
        header->dir = header->loaded = true;
        header->kind = TreeNode::Kind::KnowledgeHeader;
        HTREEITEM skills = insert_tree_item(ui->knowledge_tree, TVI_ROOT, L"Skills", header, true);
        for (const auto* source : skills_sources) add_source(source, skills);
        TreeView_Expand(ui->knowledge_tree, skills, TVE_EXPAND);
    }
    if (sources.empty()) {
        auto* empty = new TreeNode{};
        empty->loaded = true;
        empty->kind = TreeNode::Kind::KnowledgeHeader;
        insert_tree_item(ui->knowledge_tree, TVI_ROOT, L"Add folders in Knowledge settings", empty, false);
    }
    refresh_thin_scrollbar(ui->knowledge_tree);
    std::function<void(HTREEITEM)> restore = [&](HTREEITEM item) {
        for (; item; item = TreeView_GetNextSibling(ui->knowledge_tree, item)) {
            TVITEMW value{};
            value.mask = TVIF_PARAM;
            value.hItem = item;
            TreeView_GetItem(ui->knowledge_tree, &value);
            auto* node = reinterpret_cast<TreeNode*>(value.lParam);
            if (node && std::find(expanded.begin(), expanded.end(), std::make_pair(node->source_id, node->path)) != expanded.end()) {
                TreeView_Expand(ui->knowledge_tree, item, TVE_EXPAND);
                restore(TreeView_GetChild(ui->knowledge_tree, item));
            }
        }
    };
    restore(TreeView_GetRoot(ui->knowledge_tree));
    refresh_thin_scrollbar(ui->tree);
}

bool file_is_binary(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return true;
    }
    char buf[4096];
    DWORD rd = 0;
    ReadFile(h, buf, sizeof(buf), &rd, nullptr);
    CloseHandle(h);
    for (DWORD i = 0; i < rd; ++i) {
        if (buf[i] == 0) {
            return true;
        }
    }
    return false;
}

void pull_editor(Ui* ui);
void refresh_tabs(Ui* ui);
void refresh_chat_tabs(Ui* ui);
void layout(Ui* ui);
void close_selector(Ui* ui);
void open_selector(Ui* ui, HWND face, int id, bool refetch_models = true);
void reopen_model_selector(Ui* ui, HWND face);
void refill_model_selector(Ui* ui);
void refresh_threads(Ui* ui);
void refresh_chrome(Ui* ui);
void refresh_settings_data(Ui* ui);
void refresh_models(Ui* ui);
void apply_selector(Ui* ui, int owner, int sel);
std::wstring model_choice_label(const ModelChoice& m);
void do_open_folder(Ui* ui);
void do_add_authorized_folders(Ui* ui);
void persist_store(Ui* ui);
void browse_codex(Ui* ui);
void open_ai_providers_settings(Ui* ui);
void apply_editor_prefs(Ui* ui);
void open_chat_index(Ui* ui, int index);
std::wstring prompt_text(HWND parent, const wchar_t* caption, const std::wstring& initial);
bool rename_chat_id(Ui* ui, const std::string& thread_id);
LRESULT CALLBACK chat_tabs_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void go_back_content(Ui* ui);
void refresh_workbench_problems(Ui* ui);

void pull_editor(Ui* ui) {
    if (ui->active_doc < 0 || ui->active_doc >= static_cast<int>(ui->docs.size())) {
        return;
    }
    editor_save_view(ui->editor, &ui->docs[ui->active_doc].view);
    ui->docs[ui->active_doc].text = editor_get_text(ui->editor);
}

void show_doc(Ui* ui, int i) {
    if (i < 0 || i >= static_cast<int>(ui->docs.size())) {
        return;
    }
    pull_editor(ui);
    ui->active_doc = i;
    load_doc_into_editor(ui, ui->docs[i]);
    refresh_tabs(ui);
    layout(ui);
}

bool open_document(Ui* ui, const std::wstring& path, bool pin) {
    const std::wstring full = canonicalize_path(path);
    // Folder overrides are enforced here, before the file is read — a No Access subtree must not
    // be openable just because it is reachable from the explorer.
    const KnowledgeSource* owning_source =
        ui->knowledge.source_for_path(full, ui->session.store.active_project_id);
    EffectiveAccess knowledge_access;
    if (owning_source) {
        knowledge_access = ui->knowledge.resolve_effective_access(owning_source->id, full);
        if (!knowledge_access.readable) {
            const wchar_t* msg = L"This path is not readable under the knowledge source permissions.";
            if (!owning_source->enabled) {
                msg = L"This knowledge source is disabled.";
            } else if (knowledge_access.from_override && knowledge_access.mode == AccessMode::NoAccess) {
                msg = L"This path is under a No Access folder override for this knowledge source.";
            }
            MessageBoxW(ui->wnd, msg, L"Scylla", MB_ICONWARNING);
            return false;
        }
    }
    for (std::size_t i = 0; i < ui->docs.size(); ++i) {
        if (_wcsicmp(ui->docs[i].path.c_str(), full.c_str()) == 0) {
            if (pin) {
                ui->docs[i].preview = false;
            }
            show_doc(ui, static_cast<int>(i));
            return true;
        }
    }
    LoadedText loaded = load_text_file(full);
    if (loaded.binary) {
        MessageBoxW(ui->wnd, loaded.error.empty() ? L"Binary or unsupported file" : loaded.error.c_str(), L"Scylla",
                    MB_ICONERROR);
        return false;
    }
    if (loaded.too_large && loaded.text.empty()) {
        MessageBoxW(ui->wnd, loaded.error.empty() ? L"File is larger than the view limit" : loaded.error.c_str(),
                    L"Scylla", MB_ICONERROR);
        return false;
    }
    if (!loaded.error.empty() && loaded.text.empty() && !loaded.encoding_uncertain) {
        MessageBoxW(ui->wnd, loaded.error.c_str(), L"Scylla", MB_ICONERROR);
        return false;
    }
    pull_editor(ui);
    OpenDoc d;
    d.path = full;
    d.text = loaded.text;
    d.enc = loaded.enc;
    d.crlf = loaded.crlf;
    d.hash = loaded.hash;
    d.preview = !pin;
    d.readonly = loaded.encoding_uncertain;
    if (loaded.too_large) {
        d.large_file = true;
        d.readonly = true;
    }
    if (owning_source) {
        d.from_knowledge = true;
        d.knowledge_source_id = owning_source->id;
        d.knowledge_label =
            owning_source->label.empty() ? folder_name(owning_source->path) : owning_source->label;
        if (!knowledge_access.writable) {
            d.readonly = true;
        }
    }
    if (!pin) {
        for (std::size_t i = 0; i < ui->docs.size(); ++i) {
            if (ui->docs[i].preview && !ui->docs[i].dirty) {
                ui->docs[i] = std::move(d);
                ui->active_doc = static_cast<int>(i);
                load_doc_into_editor(ui, ui->docs[i]);
                refresh_tabs(ui);
                layout(ui);
                return true;
            }
        }
    }
    ui->docs.push_back(std::move(d));
    ui->active_doc = static_cast<int>(ui->docs.size()) - 1;
    load_doc_into_editor(ui, ui->docs.back());
    refresh_tabs(ui);
    layout(ui);
    return true;
}

void refresh_tabs(Ui* ui) {
    TabCtrl_DeleteAllItems(ui->tabs);
    for (std::size_t i = 0; i < ui->docs.size(); ++i) {
        std::wstring title = folder_name(ui->docs[i].path);
        bool duplicate = false;
        for (std::size_t j = 0; j < ui->docs.size(); ++j) {
            if (i != j && _wcsicmp(title.c_str(), folder_name(ui->docs[j].path).c_str()) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            const auto slash = ui->docs[i].path.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                title += L"  ·  " + folder_name(ui->docs[i].path.substr(0, slash));
            }
        }
        if (ui->docs[i].dirty) {
            title += L"*";
        }
        if (ui->docs[i].preview) {
            title += L" ·";
        }
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = const_cast<wchar_t*>(title.c_str());
        TabCtrl_InsertItem(ui->tabs, static_cast<int>(i), &it);
    }
    if (ui->active_doc >= 0) {
        TabCtrl_SetCurSel(ui->tabs, ui->active_doc);
        auto& d = ui->docs[ui->active_doc];
        SetWindowTextW(ui->hdr_editor, breadcrumbs(d.path).c_str());
        InvalidateRect(ui->hdr_editor, nullptr, TRUE);
        ui->editor_path = d.path;
    }
    // The tab strip and the Problems surface report the same dirty set; keep them in step.
    refresh_workbench_problems(ui);
}

struct PromptOut {
    std::wstring value;
    bool accepted = false;
};

LRESULT CALLBACK prompt_wnd_proc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 12, 16, 320, 28,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 160, 60, 80, 28, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 252, 60, 80, 28, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), nullptr, nullptr);
        return 0;
    }
    if (m == WM_COMMAND) {
        const int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            auto* out = reinterpret_cast<PromptOut*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (id == IDOK && out) {
                wchar_t buf[512]{};
                GetWindowTextW(GetDlgItem(hwnd, 100), buf, 512);
                out->value = buf;
                out->accepted = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (m == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

std::wstring prompt_text(HWND parent, const wchar_t* caption, const std::wstring& initial) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = prompt_wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ScyllaGPTPrompt";
        RegisterClassExW(&wc);
        reg = true;
    }
    PromptOut out;
    out.value = initial;
    HWND prompt = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME, L"ScyllaGPTPrompt", caption,
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, 360, 140, parent, nullptr,
                                  GetModuleHandleW(nullptr), &out);
    if (!prompt) {
        return initial;
    }
    RECT pr{};
    GetWindowRect(parent ? parent : GetDesktopWindow(), &pr);
    SetWindowPos(prompt, HWND_TOP, pr.left + 80, pr.top + 120, 360, 140, SWP_SHOWWINDOW);
    SetWindowTextW(GetDlgItem(prompt, 100), initial.c_str());
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(parent, WM_GETFONT, 0, 0));
    if (font) {
        SendMessageW(GetDlgItem(prompt, 100), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(GetDlgItem(prompt, IDOK), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(GetDlgItem(prompt, IDCANCEL), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (parent) {
        EnableWindow(parent, FALSE);
    }
    MSG msg{};
    while (IsWindow(prompt) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(prompt, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (parent) {
        EnableWindow(parent, TRUE);
        SetFocus(parent);
    }
    if (!out.accepted) {
        return initial;
    }
    while (!out.value.empty() && (out.value.back() == L' ' || out.value.back() == L'\t')) {
        out.value.pop_back();
    }
    return out.value.empty() ? initial : out.value;
}

void apply_editor_prefs(Ui* ui) {
    if (!ui || !ui->editor) {
        return;
    }
    editor_set_word_wrap(ui->editor, ui->session.settings.word_wrap);
    editor_set_whitespace(ui->editor, ui->session.settings.show_whitespace);
}

void edit_target_action(Ui* ui, int id) {
    HWND focus = GetFocus();
    const bool on_editor = focus == ui->editor;
    wchar_t focus_class[64]{};
    if (focus) GetClassNameW(focus, focus_class, ARRAYSIZE(focus_class));
    const bool on_edit_control = focus &&
        (_wcsicmp(focus_class, L"Edit") == 0 || _wcsicmp(focus_class, MSFTEDIT_CLASS) == 0);
    if (on_editor) {
        switch (id) {
            case ID_EDIT_UNDO:
                editor_undo(ui->editor);
                break;
            case ID_EDIT_REDO:
                editor_redo(ui->editor);
                break;
            case ID_EDIT_CUT:
                editor_cut(ui->editor);
                break;
            case ID_EDIT_COPY:
                editor_copy(ui->editor);
                break;
            case ID_EDIT_PASTE:
                editor_paste(ui->editor);
                break;
            case ID_EDIT_SELECT_ALL:
                editor_select_all(ui->editor);
                break;
            default:
                break;
        }
        return;
    }
    // Ctrl+V and the Edit menu are app-level accelerators. Forward them to whichever native
    // edit control owns focus, including Settings and Keyring fields.
    if (on_edit_control) {
        switch (id) {
            case ID_EDIT_UNDO:
                SendMessageW(focus, EM_UNDO, 0, 0);
                break;
            case ID_EDIT_REDO:
                SendMessageW(focus, EM_REDO, 0, 0);
                break;
            case ID_EDIT_CUT:
                SendMessageW(focus, WM_CUT, 0, 0);
                break;
            case ID_EDIT_COPY:
                SendMessageW(focus, WM_COPY, 0, 0);
                break;
            case ID_EDIT_PASTE:
                SendMessageW(focus, WM_PASTE, 0, 0);
                break;
            case ID_EDIT_SELECT_ALL:
                SendMessageW(focus, EM_SETSEL, 0, -1);
                break;
            default:
                break;
        }
    }
}

HMENU build_menu_bar() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    // Labels must match the accelerator table: Ctrl+O opens a file, Ctrl+Shift+O a folder.
    AppendMenuW(file, MF_STRING, ID_OPEN_FILE, L"Open File…\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_OPEN_FOLDER, L"Open Folder…\tCtrl+Shift+O");
    AppendMenuW(file, MF_STRING, ID_SAVE, L"Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_CLOSE_TAB, L"Close Tab\tCtrl+W");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_SETTINGS, L"Settings…");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"Exit");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, ID_EDIT_UNDO, L"Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, ID_EDIT_REDO, L"Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_EDIT_CUT, L"Cut\tCtrl+X");
    AppendMenuW(edit, MF_STRING, ID_EDIT_COPY, L"Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, ID_EDIT_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(edit, MF_STRING, ID_EDIT_SELECT_ALL, L"Select All\tCtrl+A");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_FIND, L"Find…\tCtrl+F");
    AppendMenuW(edit, MF_STRING, ID_REPLACE, L"Replace…\tCtrl+H");
    AppendMenuW(edit, MF_STRING, ID_GOTO, L"Go to Line…\tCtrl+G");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, ID_TOGGLE_FILES, L"File Explorer");
    AppendMenuW(view, MF_STRING, ID_TOGGLE_HISTORY, L"Chat History");
    AppendMenuW(view, MF_STRING, ID_FOCUS, L"Focus Editor");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_TOGGLE_TERMINAL, L"Terminal\tCtrl+`");
    AppendMenuW(view, MF_STRING, ID_NEW_TERMINAL, L"New Terminal");
    AppendMenuW(view, MF_STRING, ID_PANEL_PROBLEMS, L"Problems");
    AppendMenuW(view, MF_STRING, ID_PANEL_OUTPUT, L"Output");
    AppendMenuW(view, MF_STRING, ID_PANEL_PORTS, L"Ports");
    AppendMenuW(view, MF_STRING, ID_VIEW_WRAP, L"Word Wrap");
    AppendMenuW(view, MF_STRING, ID_VIEW_WHITESPACE, L"Show Whitespace");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");

    HMENU agent = CreatePopupMenu();
    AppendMenuW(agent, MF_STRING, ID_NEW, L"New Chat");
    AppendMenuW(agent, MF_STRING, ID_CANCEL, L"Stop Generation");
    AppendMenuW(agent, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(agent, MF_STRING, ID_ADD_FILE, L"Add File to Context");
    AppendMenuW(agent, MF_STRING, ID_ADD_SEL, L"Add Selection to Context");
    AppendMenuW(agent, MF_STRING, ID_CLEAR_CTX, L"Clear Context");
    AppendMenuW(agent, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(agent, MF_STRING, ID_AI_PROVIDERS, L"Manage Agent Providers…");
    AppendMenuW(agent, MF_STRING, ID_PERM_INFO, L"Permission modes…");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(agent), L"&Agent");

    HMENU access = CreatePopupMenu();
    AppendMenuW(access, MF_STRING, ID_ACCESS_SHOW, L"Project Security");
    AppendMenuW(access, MF_STRING, ID_ACCESS_FOLDERS, L"Authorized Folders…");
    AppendMenuW(access, MF_STRING, ID_ACCESS_KEYRING, L"Keyring…");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(access), L"A&ccess");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP_GETTING_STARTED, L"Getting Started");
    AppendMenuW(help, MF_STRING, ID_HELP_SHORTCUTS, L"Keyboard Shortcuts");
    AppendMenuW(help, MF_STRING, ID_HELP_DIAG, L"Diagnostics");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"About Scylla Workbench");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return bar;
}

CommandUiState capture_command_state(Ui* ui) {
    CommandUiState st;
    st.has_active_doc = ui->active_doc >= 0;
    st.has_dirty_doc = st.has_active_doc && ui->docs[ui->active_doc].dirty;
    st.generating = ui->session.state == AppState::Generating;
    st.word_wrap = ui->session.settings.word_wrap;
    st.show_whitespace = ui->session.settings.show_whitespace;
    st.files_visible = ui->panes.show_files;
    st.history_visible = ui->panes.show_history;
    st.focus_editor = ui->session.settings.focus_editor;
    st.terminal_visible = ui->session.settings.terminal_visible;
    st.has_context_chips = !ui->session.context_chips.empty();
    return st;
}

void hide_settings_controls(Ui* ui) {
    const HWND ctrls[] = {ui->content_nav, ui->set_codex, ui->set_copy_runtime, ui->set_wrap, ui->set_whitespace,
                          ui->set_enter_sends, ui->set_ui_gallery, ui->gs_open_folder, ui->gs_providers};
    for (HWND h : ctrls) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
    ui->providers_ui.set_visible(false);
    ui->knowledge_ui.set_visible(false);
    ui->mcp_ui.hide();
    ui->strata_ui.set_visible(false);
    ui->keyring_ui.show(false);
    ui->environment_ui.set_visible(false);
    // Leave the Connections page alone when it is the page being laid out. layout() calls this on every
    // pass and then re-shows the active subpage, so hiding ~60 controls just to restore them flashed the
    // whole panel on each click. The page's own visibility is diffed, so this stays correct either way.
    if (!(ui->content_view == ContentView::Settings && ui->settings_section == SettingsSection::Security &&
          ui->security_subpage == SecuritySubpage::Connections)) {
        ui->connections_ui.set_visible(false);
    }
    ui->security_overview.set_visible(false);
    ui->security_policy.set_visible(false);
    for (HWND tab : {ui->sec_tab_overview, ui->sec_tab_keyring, ui->sec_tab_environments,
                     ui->sec_tab_connections, ui->sec_tab_policy}) {
        if (tab) {
            ShowWindow(tab, SW_HIDE);
        }
    }
    ui->terminal_ui.set_visible(false);
    ui->ui_gallery.show(false);
}

void apply_security_subpage(Ui* ui) {
    if (!ui) {
        return;
    }
    const bool on_security =
        ui->content_view == ContentView::Settings && ui->settings_section == SettingsSection::Security;
    if (!on_security) {
        return;
    }
    ui->security_overview.set_visible(ui->security_subpage == SecuritySubpage::Overview);
    ui->environment_ui.set_visible(ui->security_subpage == SecuritySubpage::Environments);
    ui->connections_ui.set_visible(ui->security_subpage == SecuritySubpage::Connections);
    ui->security_policy.set_visible(ui->security_subpage == SecuritySubpage::Policy);
    // Keyring full page under Security, or Access→Keyring ContentView.
    const bool show_kr = ui->security_subpage == SecuritySubpage::Keyring;
    ui->keyring_ui.show(show_kr);
    for (HWND tab : {ui->sec_tab_overview, ui->sec_tab_keyring, ui->sec_tab_environments,
                     ui->sec_tab_connections, ui->sec_tab_policy}) {
        if (tab) {
            ShowWindow(tab, SW_SHOW);
            InvalidateRect(tab, nullptr, TRUE);
        }
    }
}

void set_security_subpage(Ui* ui, SecuritySubpage page) {
    if (!ui) {
        return;
    }
    ui->security_subpage = page;
    if (page == SecuritySubpage::Keyring) {
        std::wstring pname;
        for (const auto& p : ui->session.store.projects) {
            if (p.id == ui->session.store.active_project_id) {
                pname = p.name;
                break;
            }
        }
        ui->keyring_ui.ensure_app_vault_bound();
        ui->keyring_ui.set_active_project(ui->session.store.active_project_id, pname);
    }
    refresh_settings_data(ui);
    apply_security_subpage(ui);
    layout(ui);
}

void refresh_settings_pane(Ui* ui) {
    if (!ui || ui->content_view != ContentView::Settings) {
        return;
    }
    if (ui->settings_section == SettingsSection::Providers) {
        ui->providers_ui.refresh(ui->session);
    }

    InvalidateRect(ui->set_wrap, nullptr, TRUE);
    InvalidateRect(ui->set_whitespace, nullptr, TRUE);
    InvalidateRect(ui->set_enter_sends, nullptr, TRUE);
    ui->session.sync_claude_models();
    refresh_models(ui);
}

std::wstring user_profile_home() {
    wchar_t home[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return home;
}

int settings_section_nav_index(SettingsSection section) {
    switch (section) {
        case SettingsSection::Providers:
            return 0;
        case SettingsSection::Editor:
            return 1;
        case SettingsSection::Terminal:
            return 2;
        case SettingsSection::Knowledge:
            return 3;
        case SettingsSection::Mcp:
            return 4;
        case SettingsSection::Strata:
            return 5;
        case SettingsSection::Security:
            return 6;
        case SettingsSection::Advanced:
            return 7;
        default:
            return 0;
    }
}

SettingsSection settings_section_from_nav(int sel) {
    switch (sel) {
        case 0:
            return SettingsSection::Providers;
        case 1:
            return SettingsSection::Editor;
        case 2:
            return SettingsSection::Terminal;
        case 3:
            return SettingsSection::Knowledge;
        case 4:
            return SettingsSection::Mcp;
        case 5:
            return SettingsSection::Strata;
        case 6:
            return SettingsSection::Security;
        default:
            return SettingsSection::Advanced;
    }
}

bool settings_section_uses_body(SettingsSection section) {
    (void)section;
    return false;  // Terminal / Knowledge / MCP / Strata use interactive panels
}

std::wstring keyring_status_label(Ui* ui, const std::string& /*project_id*/) {
    if (ui) {
        return ui->keyring_ui.status_label();
    }
    return Keyring::app_vault_exists() ? L"Locked" : L"None";
}

void reload_terminal_profiles(Ui* ui) {
    if (!ui) {
        return;
    }
    const auto discovered = discover_terminal_profiles();
    const auto custom = load_custom_terminal_profiles(ui->session.paths.terminals_path);
    ui->terminal_profiles =
        merge_terminal_profiles(discovered, custom, ui->session.settings.terminal_profile_enabled);
}

void wire_workbench_panel(Ui* ui);

// Environment chooser for an explicit "new terminal" gesture. Returns false when the menu was
// dismissed; on success |out| holds the chosen environment id (empty = launch with none).
// Silently yields the project default when there is nothing to choose between.
bool prompt_terminal_environment(Ui* ui, std::string* out) {
    if (!ui || !ui->wnd || !out) {
        return false;
    }
    out->clear();
    const std::string pid = ui->session.store.active_project_id;
    const ProjectEnvironmentState* st = pid.empty() ? nullptr : ui->environments.state_for(pid);
    const std::string active = pid.empty() ? std::string{} : ui->environments.active_environment_id(pid);
    if (!st || st->environments.empty()) {
        *out = active;
        return true;
    }

    constexpr UINT kIdDefault = 1;
    constexpr UINT kIdNone = 2;
    constexpr UINT kIdFirstEnv = 100;
    std::wstring default_label = L"Project default — ";
    if (const auto* e = active.empty() ? nullptr : ui->environments.find_environment(pid, active)) {
        default_label += utf16(e->name);
    } else {
        default_label += L"None";
    }

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, kIdDefault, default_label.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    UINT item = kIdFirstEnv;
    for (const auto& e : st->environments) {
        UINT flags = MF_STRING;
        if (e.id == active) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(m, flags, item++, utf16(e.name).c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kIdNone, L"No environment");

    POINT p{};
    GetCursorPos(&p);
    const int chosen = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0,
                                      ui->wnd, nullptr);
    DestroyMenu(m);
    if (chosen == 0) {
        return false;  // dismissed — do not launch
    }
    if (chosen == static_cast<int>(kIdDefault)) {
        *out = active;
        return true;
    }
    if (chosen == static_cast<int>(kIdNone)) {
        return true;  // out already empty
    }
    const auto idx = static_cast<std::size_t>(chosen - static_cast<int>(kIdFirstEnv));
    if (idx >= st->environments.size()) {
        return false;
    }
    *out = st->environments[idx].id;
    return true;
}

// Resolves |env_id| into a launchable environment block and applies Execution Policy → Human
// terminals to protected values. Returns false when the launch must be abandoned (resolve error or
// a policy refusal). |env_block| must outlive the spawn: opts->environment points into it.
bool resolve_terminal_environment(Ui* ui, const std::string& env_id, TerminalCreateOpts* opts,
                                  std::wstring* env_block) {
    if (!ui || !ui->wnd || !opts || !env_block) {
        return false;
    }
    const std::string pid = ui->session.store.active_project_id;
    if (pid.empty() || env_id.empty()) {
        return true;
    }
    auto resolved = ui->environments.resolve_for_terminal(pid, env_id, ui->keyring_ui.keyring(),
                                                          /*agent_terminal=*/false);
    if (resolved.status != EnvResolveStatus::Ok) {
        std::wstring msg = L"Environment resolve failed: ";
        msg += utf16(resolved.message.empty() ? env_resolve_status_string(resolved.status)
                                              : resolved.message);
        SetWindowTextW(ui->status, msg.c_str());
        ui->workbench_panel.append_output(L"terminal", msg);
        MessageBoxW(ui->wnd, msg.c_str(), L"Scylla Environments", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (resolved.includes_protected) {
        // Execution Policy → Human terminals decides whether this is silent, confirmed, or
        // refused. Human Only is marked either way: the process really does hold secrets.
        const PolicyMode policy = ui->session.settings.execution_policy.human_terminals;
        if (policy == PolicyMode::Block) {
            SetWindowTextW(ui->status, L"Execution Policy blocks protected values in terminals.");
            ui->workbench_panel.append_output(
                L"terminal", L"Blocked by Execution Policy: protected environment values are not "
                             L"allowed in terminals.");
            MessageBoxW(ui->wnd,
                        L"This environment injects Keyring-backed secrets, and Execution "
                        L"Policy blocks protected values in terminals.\r\n\r\nChange it under "
                        L"Settings → Security → Execution Policy, or launch with no "
                        L"environment.",
                        L"Blocked by Execution Policy", MB_OK | MB_ICONWARNING);
            return false;
        }
        if (policy == PolicyMode::Ask) {
            const int ans =
                MessageBoxW(ui->wnd,
                            L"This environment injects Keyring-backed secrets.\r\n\r\n"
                            L"The terminal will be marked Human Only — do not use it for agent "
                            L"tasks.\r\n\r\nContinue?",
                            L"Protected environment", MB_YESNO | MB_ICONWARNING);
            if (ans != IDYES) {
                return false;
            }
        }
        opts->human_only = true;
    }
    if (!resolved.entries.empty()) {
        *env_block = ProjectEnvironmentManager::merge_into_process_env(resolved.entries);
        opts->environment = env_block;
    }
    opts->environment_id = env_id;
    if (const auto* env = ui->environments.find_environment(pid, env_id)) {
        opts->environment_name = utf16(env->name);
    }
    return true;
}

// Enabled profile matching |id|, or nullptr. Restart/Duplicate need the session's own profile back,
// not the workspace default.
const TerminalProfile* find_enabled_terminal_profile(Ui* ui, const std::string& id) {
    if (!ui || id.empty()) {
        return nullptr;
    }
    for (const auto& p : ui->terminal_profiles) {
        if (p.id == id && p.enabled) {
            return &p;
        }
    }
    return nullptr;
}

void ensure_terminal_panel(Ui* ui);
bool prompt_terminal_environment(Ui* ui, std::string* out);
bool new_terminal_session(Ui* ui, const std::string& profile_id_override = {},
                          const std::string* env_override = nullptr,
                          const std::wstring* cwd_override = nullptr);

HWND terminal_host_parent(Ui* ui) {
    if (!ui) {
        return nullptr;
    }
    // Do not call ensure_terminal_panel here — layout() already did, and ensure→set_surface→callback
    // must not re-enter layout while chrome/hosts are mid-move.
    if (HWND c = ui->workbench_panel.content_hwnd()) {
        return c;
    }
    if (HWND p = ui->workbench_panel.hwnd()) {
        return p;
    }
    return ui->wnd;
}

void request_new_terminal(Ui* ui) {
    if (!ui) {
        return;
    }
    ensure_terminal_panel(ui);
    ui->session.settings.terminal_visible = true;
    ui->panel_collapsed = false;
    ui->workbench_panel.set_collapsed(false);
    ui->workbench_panel.set_surface(PanelSurface::Terminal);
    std::string env_id;
    if (!prompt_terminal_environment(ui, &env_id)) {
        layout(ui);
        return;
    }
    new_terminal_session(ui, {}, &env_id);
    layout(ui);
    ui->terminal_sessions.focus_active();
    refresh_chrome(ui);
}

// |env_override| nullptr = use the project's active environment (auto-created panel sessions).
// |cwd_override| nullptr = resolve from the profile; Duplicate passes the source session's cwd so
// the copy lands in the same directory even if the profile resolves differently now.
bool new_terminal_session(Ui* ui, const std::string& profile_id_override,
                          const std::string* env_override,
                          const std::wstring* cwd_override) {
    if (!ui || !ui->wnd) {
        return false;
    }
    if (ui->terminal_profiles.empty()) {
        reload_terminal_profiles(ui);
    }
    const TerminalProfile* prof = find_enabled_terminal_profile(ui, profile_id_override);
    if (!prof) {
        prof = find_default_terminal_profile(ui->terminal_profiles,
                                            ui->session.settings.default_terminal_profile_id);
    }
    if (!prof) {
        SetWindowTextW(ui->status, L"No enabled terminal profile. Configure Settings → Terminal.");
        ui->workbench_panel.append_output(
            L"terminal", L"Spawn refused: no enabled terminal profile (Settings → Terminal).");
        return false;
    }
    const std::wstring cwd =
        (cwd_override && !cwd_override->empty())
            ? *cwd_override
            : resolve_profile_cwd(*prof, ui->session.project_root, user_profile_home());

    TerminalCreateOpts opts;
    std::wstring env_block;
    const std::string pid = ui->session.store.active_project_id;
    const std::string env_id =
        env_override ? *env_override : ui->environments.active_environment_id(pid);
    if (!resolve_terminal_environment(ui, env_id, &opts, &env_block)) {
        return false;
    }

    std::wstring err;
    const bool spawned = ui->terminal_sessions.create_session(
        terminal_host_parent(ui), ui->wnd, GetModuleHandleW(nullptr), ID_TERMINAL, *prof, cwd, opts, &err);
    // Wipe resolved secret material from the local block once CreateProcess is done with it —
    // including the failure path, where a live block would otherwise outlive its use.
    if (!env_block.empty()) {
        SecureZeroMemory(env_block.data(), env_block.size() * sizeof(wchar_t));
    }
    if (!spawned) {
        if (!err.empty()) {
            SetWindowTextW(ui->status, err.c_str());
        }
        std::wstring log = L"Spawn failed: ";
        log += prof->name;
        log += L" in ";
        log += cwd.empty() ? L"(no cwd)" : cwd;
        if (!err.empty()) {
            log += L" — ";
            log += err;
        }
        ui->workbench_panel.append_output(L"terminal", log);
        return false;
    }
    ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
    ui->workbench_panel.set_enabled_profiles(enabled_terminal_profiles(ui->terminal_profiles));
    return true;
}

// Problems v0. There is no language-server or linter pipeline yet, so the surface reports the
// workbench findings it can actually vouch for: documents with unsaved edits. |ref| carries the
// OpenDoc index so activating a row can jump to that tab.
void refresh_workbench_problems(Ui* ui) {
    if (!ui) {
        return;
    }
    std::vector<WorkbenchProblem> items;
    std::wstring signature;
    for (std::size_t i = 0; i < ui->docs.size(); ++i) {
        const auto& d = ui->docs[i];
        if (!d.dirty) {
            continue;
        }
        const std::size_t slash = d.path.find_last_of(L"\\/");
        const std::wstring leaf =
            (slash == std::wstring::npos) ? d.path : d.path.substr(slash + 1);
        WorkbenchProblem p;
        p.primary = L"Unsaved: " + (leaf.empty() ? std::wstring(L"(untitled)") : leaf);
        p.secondary = d.path;
        p.ref = static_cast<int>(i);
        signature += std::to_wstring(i) + L"|" + d.path + L"\n";
        items.push_back(std::move(p));
    }
    if (signature == ui->problems_signature) {
        return;  // typing in an already-dirty document must not churn the list control
    }
    ui->problems_signature = signature;
    ui->workbench_panel.set_problems(std::move(items));
}

void ensure_terminal_panel(Ui* ui) {
    if (!ui || !ui->wnd) {
        return;
    }
    if (!ui->workbench_panel.created()) {
        wire_workbench_panel(ui);
    }
    const bool show = ui->session.settings.terminal_visible;
    ui->workbench_panel.set_visible(show);
    ui->terminal_sessions.set_panel_visible(show && !ui->panel_collapsed &&
                                           ui->workbench_panel.surface() == PanelSurface::Terminal);
    if (show) {
        ui->workbench_panel.set_surface(parse_panel_surface(ui->session.settings.panel_surface));
        if (ui->terminal_sessions.count() == 0 &&
            ui->workbench_panel.surface() == PanelSurface::Terminal) {
            new_terminal_session(ui, {});
        }
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        ui->workbench_panel.set_enabled_profiles(enabled_terminal_profiles(ui->terminal_profiles));
    }
}

void toggle_terminal(Ui* ui) {
    if (!ui) {
        return;
    }
    ui->session.settings.terminal_visible = !ui->session.settings.terminal_visible;
    if (!ui->session.settings.terminal_visible) ui->terminal_sessions.destroy_all();
    ui->panel_collapsed = false;
    ui->workbench_panel.set_collapsed(false);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    ensure_terminal_panel(ui);
    layout(ui);
}

void show_panel_surface(Ui* ui, PanelSurface surface) {
    if (!ui) {
        return;
    }
    ui->session.settings.terminal_visible = true;
    ui->session.settings.panel_surface = panel_surface_string(surface);
    ui->panel_collapsed = false;
    ui->workbench_panel.set_collapsed(false);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    ensure_terminal_panel(ui);
    ui->workbench_panel.set_surface(surface);
    layout(ui);
}

void poll_terminal(Ui* ui) {
    if (!ui) {
        return;
    }
    // Repaint the tab strip only when a session's alive state actually flips. Calling
    // refresh_session_tabs unconditionally would invalidate the panel on every timer tick.
    std::size_t alive_before = 0;
    for (const auto& s : ui->terminal_sessions.sessions()) {
        alive_before += s.alive ? 1u : 0u;
    }
    ui->terminal_sessions.poll_all();
    std::size_t alive_after = 0;
    for (const auto& s : ui->terminal_sessions.sessions()) {
        alive_after += s.alive ? 1u : 0u;
    }
    if (alive_after != alive_before) {
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
    }
}

void wire_workbench_panel(Ui* ui) {
    if (!ui || ui->workbench_panel.created()) {
        return;
    }
    ui->workbench_panel.create(ui->wnd, GetModuleHandleW(nullptr), ui->font, ui->font_small);
    ui->workbench_panel.set_on_hide([ui]() {
        ui->session.settings.terminal_visible = false;
        ui->terminal_sessions.destroy_all();
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        save_settings(ui->session.paths.settings_path, ui->session.settings);
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, kRelayoutEnsurePanel, 0);
    });
    ui->workbench_panel.set_on_collapse([ui]() {
        ui->panel_collapsed = !ui->panel_collapsed;
        ui->workbench_panel.set_collapsed(ui->panel_collapsed);
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, kRelayoutEnsurePanel, 0);
    });
    ui->workbench_panel.set_on_maximize([ui]() {
        if (ui->workbench_panel.maximized()) {
            if (ui->panel_h_before_maximize > 0) {
                ui->session.settings.terminal_h = ui->panel_h_before_maximize;
            }
            ui->workbench_panel.set_maximized(false);
        } else {
            ui->panel_h_before_maximize = ui->session.settings.terminal_h;
            ui->session.settings.terminal_h = 480;
            ui->workbench_panel.set_maximized(true);
        }
        save_settings(ui->session.paths.settings_path, ui->session.settings);
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, 0, 0);
    });
    // A new / switched / closed session changes the Env and Human Only status segments and where
    // typing should go. refresh_chrome + focus_active keep both honest.
    // An explicit + offers the environment chooser first; a dismissed menu cancels the launch.
    ui->workbench_panel.set_on_new_terminal([ui]() {
        request_new_terminal(ui);
    });
    ui->workbench_panel.set_on_new_with_profile([ui](const std::string& id) {
        ensure_terminal_panel(ui);
        ui->session.settings.terminal_visible = true;
        ui->panel_collapsed = false;
        ui->workbench_panel.set_collapsed(false);
        ui->workbench_panel.set_surface(PanelSurface::Terminal);
        std::string env_id;
        if (!prompt_terminal_environment(ui, &env_id)) {
            return;
        }
        new_terminal_session(ui, id, &env_id);
        layout(ui);
        ui->terminal_sessions.focus_active();
        refresh_chrome(ui);
    });
    ui->workbench_panel.set_on_close_session([ui](int index, bool confirm) {
        // |confirm| is set by the tab context menu's Close item; the X button and middle-click
        // stay immediate, matching the editor tab strip.
        if (confirm) {
            const auto& titles = ui->workbench_panel.session_titles();
            const std::wstring name = (index >= 0 && index < static_cast<int>(titles.size()))
                                          ? titles[static_cast<std::size_t>(index)]
                                          : std::wstring(L"this terminal");
            if (!ui_kit::confirm_destructive(ui->wnd, L"Close", name.c_str(),
                                             L"Anything still running in it is terminated.")) {
                return;
            }
        }
        ui->terminal_sessions.close_session_at(index);
        if (ui->terminal_sessions.count() == 0) {
            ui->session.settings.terminal_visible = false;
            save_settings(ui->session.paths.settings_path, ui->session.settings);
        }
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, 0, 1);
    });
    ui->workbench_panel.set_on_activate_session([ui](int index) {
        ui->terminal_sessions.set_active(index);
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        // Defer layout/focus — same reentrancy risk as surface BN_CLICKED (raise_chrome / hosts).
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, 0, 1);
    });
    // Instance-level rename: the tab title changes, the profile behind it does not.
    ui->workbench_panel.set_on_rename_session([ui](int index) {
        auto* s = ui->terminal_sessions.session_at(index);
        if (!s) {
            return;
        }
        const std::wstring cur = s->title;
        // prompt_text echoes |initial| back on Cancel, so an unchanged value means "no rename".
        const std::wstring next = prompt_text(ui->wnd, L"Rename terminal", cur);
        if (next.empty() || next == cur) {
            return;
        }
        ui->terminal_sessions.rename_session_at(index, next);
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        refresh_chrome(ui);
    });
    // Restart respawns the same profile in the same cwd, keeping the tab's id / title / position.
    // The environment is re-resolved so a rotated secret lands in the new process.
    ui->workbench_panel.set_on_restart_session([ui](int index) {
        auto* s = ui->terminal_sessions.session_at(index);
        if (!s) {
            return;
        }
        const std::wstring title = s->title;
        if (s->alive &&
            !ui_kit::confirm_destructive(ui->wnd, L"Restart", title.c_str(),
                                         L"The running shell is terminated before the new one starts.")) {
            return;
        }
        if (ui->terminal_profiles.empty()) {
            reload_terminal_profiles(ui);
        }
        const std::string profile_id = s->profile_id;
        const std::string env_id = s->environment_id;
        const std::wstring cwd_hint = s->cwd;
        const TerminalProfile* prof = find_enabled_terminal_profile(ui, profile_id);
        if (!prof) {
            prof = find_default_terminal_profile(ui->terminal_profiles,
                                                ui->session.settings.default_terminal_profile_id);
        }
        if (!prof) {
            SetWindowTextW(ui->status, L"No enabled terminal profile. Configure Settings → Terminal.");
            ui->workbench_panel.append_output(L"terminal",
                                              L"Restart refused: no enabled terminal profile.");
            return;
        }
        const std::wstring cwd =
            cwd_hint.empty() ? resolve_profile_cwd(*prof, ui->session.project_root, user_profile_home())
                             : cwd_hint;
        TerminalCreateOpts opts;
        std::wstring env_block;
        if (!resolve_terminal_environment(ui, env_id, &opts, &env_block)) {
            return;
        }
        std::wstring err;
        const bool ok = ui->terminal_sessions.restart_session_at(
            index, terminal_host_parent(ui), ui->wnd, GetModuleHandleW(nullptr), ID_TERMINAL, *prof, cwd, opts,
            &err);
        if (!env_block.empty()) {
            SecureZeroMemory(env_block.data(), env_block.size() * sizeof(wchar_t));
        }
        if (ok) {
            ui->workbench_panel.append_output(L"terminal", L"Restarted " + title + L" in " + cwd);
        } else {
            std::wstring log = L"Restart failed: " + title;
            if (!err.empty()) {
                log += L" — " + err;
                SetWindowTextW(ui->status, err.c_str());
            }
            ui->workbench_panel.append_output(L"terminal", log);
        }
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
        layout(ui);
        ui->terminal_sessions.focus_active();
        refresh_chrome(ui);
    });
    // Duplicate = a fresh session on the same profile / environment / cwd. No env chooser: the
    // gesture already says which environment is wanted.
    ui->workbench_panel.set_on_duplicate_session([ui](int index) {
        auto* s = ui->terminal_sessions.session_at(index);
        if (!s) {
            return;
        }
        // create_session grows the session vector, which invalidates |s| — copy first.
        const std::string profile_id = s->profile_id;
        const std::string env_id = s->environment_id;
        const std::wstring cwd = s->cwd;
        new_terminal_session(ui, profile_id, &env_id, &cwd);
        layout(ui);
        ui->terminal_sessions.focus_active();
        refresh_chrome(ui);
    });
    ui->workbench_panel.set_on_problems_refresh([ui]() {
        // Dirty state lives in the editor control until it is pulled back into the OpenDoc.
        pull_editor(ui);
        ui->problems_signature.clear();  // an explicit Refresh always rebuilds the rows
        refresh_workbench_problems(ui);
    });
    ui->workbench_panel.set_on_problem_activate([ui](int ref) {
        // |ref| is the OpenDoc index the row was built from, not the list position.
        if (ref < 0 || ref >= static_cast<int>(ui->docs.size())) {
            return;
        }
        go_back_content(ui);  // a Problems row is only reachable from the editor surface
        show_doc(ui, ref);
        refresh_chrome(ui);
    });
    ui->workbench_panel.set_on_surface_changed([ui]() {
        ui->session.settings.panel_surface = panel_surface_string(ui->workbench_panel.surface());
        save_settings(ui->session.paths.settings_path, ui->session.settings);
        // Must not layout/raise_chrome while the surface BUTTON is still in BN_CLICKED.
        PostMessageW(ui->wnd, WM_SCYLLA_RELAYOUT, kRelayoutEnsurePanel, 0);
    });
}

std::wstring terminal_settings_body(Ui* /*ui*/) {
    return L"";  // interactive TerminalSettingsUi
}

void ensure_terminal_host(Ui* ui) {
    // Back-compat alias — panel show path.
    ensure_terminal_panel(ui);
}

std::wstring knowledge_settings_body(Ui* ui) {
    (void)ui;
    return L"";  // Knowledge/Skills uses KnowledgeSettingsUi (not prose body).
}

std::wstring mcp_settings_body(Ui* ui) {
    const std::string pid = ui->session.store.active_project_id;
    std::wstring msg = L"MCP\r\n\r\n";
    msg += L"Store: ";
    msg += ui->session.paths.mcp_path.empty() ? L"(n/a)" : ui->session.paths.mcp_path;
    msg += L"\r\n\r\nCONNECTIONS";
    if (!pid.empty()) {
        msg += L" (project-scoped + global)\r\n";
    } else {
        msg += L"\r\n";
    }
    if (pid.empty()) {
        if (ui->mcp.connections().empty()) {
            msg += L"(none)\r\n";
        } else {
            for (const auto& c : ui->mcp.connections()) {
                msg += L"• ";
                msg += c.display_name.empty() ? utf16(c.service_id) : c.display_name;
                msg += c.enabled ? L"" : L" (disabled)";
                msg += L"\r\n";
            }
        }
    } else {
        const auto conns = static_cast<const McpManager&>(ui->mcp).list_for_project(pid);
        if (conns.empty()) {
            msg += L"(none for this project)\r\n";
        } else {
            for (const McpConnection* c : conns) {
                if (!c) {
                    continue;
                }
                msg += L"• ";
                msg += c->display_name.empty() ? utf16(c->service_id) : c->display_name;
                msg += c->enabled ? L"" : L" (disabled)";
                msg += L"\r\n";
            }
        }
    }
    msg += L"\r\nKNOWN TEMPLATES\r\n";
    for (const auto& t : McpManager::known_templates()) {
        msg += L"• ";
        msg += utf16(t.display_name);
        msg += L" (";
        msg += utf16(t.service_id);
        msg += L")\r\n";
    }
    return msg;
}

std::wstring strata_settings_body(Ui* ui) {
    (void)ui;
    return L"";  // STRATA uses StrataSettingsUi (native bridge panel).
}

void populate_settings_section_body(Ui* ui) {
    if (!ui || ui->content_view != ContentView::Settings) {
        return;
    }
    if (ui->settings_section == SettingsSection::Mcp) {
        ui->mcp_ui.refresh(ui->mcp);
        return;
    }
    if (ui->settings_section == SettingsSection::Knowledge) {
        ui->knowledge_ui.reload(ui->knowledge, ui->session.store.active_project_id);
        return;
    }
    if (!ui->content_body || !settings_section_uses_body(ui->settings_section)) {
        return;
    }
    std::wstring text;
    switch (ui->settings_section) {
        case SettingsSection::Terminal:
            text = terminal_settings_body(ui);
            break;
        case SettingsSection::Strata:
            text = strata_settings_body(ui);
            break;
        default:
            break;
    }
    SetWindowTextW(ui->content_body, text.c_str());
}

std::wstring access_body_text(Ui* ui) {
    const std::string pid = ui->session.store.active_project_id;
    const std::string policy = normalize_agent_terminal_policy(ui->session.settings.agent_terminal_policy);
    const auto knowledge_roots = static_cast<const KnowledgeStore&>(ui->knowledge).list_for_project(pid);
    const auto mcp_conns =
        pid.empty() ? std::vector<const McpConnection*>{}
                    : static_cast<const McpManager&>(ui->mcp).list_for_project(pid);
    const StrataHealth health = ui->strata.health_check();
    const StrataBinding* binding = pid.empty() ? nullptr : ui->strata.binding_for(pid);

    std::wstring msg = L"PROJECT SECURITY\r\n\r\n";
    msg += L"ACTIVE PROJECT\r\n";
    msg += ui->session.project_root.empty() ? L"(none — open a folder)" : ui->session.project_root;
    msg += L"\r\nId: ";
    msg += pid.empty() ? L"(none)" : utf16(pid);

    msg += L"\r\n\r\nFILES GRANT\r\n";
    msg += ui->session.has_project_grant() ? L"Allow edits (workspace-write + sandboxed shell)"
                                          : L"Read only (no project grant)";
    msg += L"\r\nAsk before tool use — approval_policy on-request (always)";

    msg += L"\r\n\r\nAGENT TERMINAL POLICY\r\n";
    msg += utf16(policy);
    msg += L"\r\n(Human View → Terminal is independent of this policy.)";

    msg += L"\r\n\r\nKNOWLEDGE ROOTS\r\n";
    msg += std::to_wstring(knowledge_roots.size());
    msg += L" configured for this project";

    msg += L"\r\n\r\nMCP CONNECTIONS\r\n";
    msg += std::to_wstring(mcp_conns.size());
    msg += L" in scope for this project";

    msg += L"\r\n\r\nSTRATA (native memory)\r\n";
    msg += L"Local: ";
    msg += ui->strata_bridge.running() || health.ok ? L"available" : L"not connected";
    msg += L"\r\nShared: central pending";
    if (binding) {
        msg += L"\r\nBinding: ";
        msg += utf16(binding->org);
        msg += L"/";
        msg += utf16(binding->strata_project);
    } else {
        msg += L"\r\nBinding: (none)";
    }

    msg += L"\r\n\r\nSCYLLA KEYRING\r\n";
    msg += keyring_status_label(ui, pid);
    msg += L"\r\n";
    msg += Keyring::default_app_vault_path();
    msg += L"\r\n(Access → Keyring… to create, unlock, or manage secrets. "
           L"Agent sees references only — never values.)";

    msg += L"\r\n\r\nAUTHORIZED FOLDERS\r\n";
    if (const auto* project = ui->session.store.active(); project && !project->roots.empty()) {
        for (const auto& root : project->roots) msg += L"• " + root + L"\r\n";
    } else msg += L"(none)\r\n";
    msg += L"\r\nUse Access → Authorized Folders… to add one or more peer repositories.";
    return msg;
}

std::wstring diagnostics_body_text(Ui* ui) {
    const std::wstring runtime =
        ui->session.runtime_path.empty() ? ui->session.settings.codex_path : ui->session.runtime_path;
    std::wstring msg = L"RUNTIME\r\n";
    msg += runtime.empty() ? L"(not set)" : runtime;
    msg += L"\r\n\r\nCODEX_HOME (isolated)\r\n";
    msg += ui->session.paths.codex_home.empty() ? L"(n/a)" : ui->session.paths.codex_home;
    msg += L"\r\n\r\nOPENAI\r\n";
    if (ui->session.account.signed_in) {
        msg += utf16(ui->session.account.email);
        msg += L"\r\nType: ";
        msg += utf16(ui->session.account.type.empty() ? "ChatGPT" : ui->session.account.type);
    } else {
        msg += L"Not signed in";
    }
    const ProviderId def = provider_id_from_string(ui->session.settings.default_provider);
    msg += L"\r\n\r\nDEFAULT PROVIDER\r\n";
    msg += utf16(provider_id_string(def));
    const auto claude = claude_provider_status();
    msg += L"\r\n\r\nCLAUDE\r\n";
    msg += utf16(claude.auth_label);
    msg += L"\r\nCLI: ";
    const std::wstring cli = discover_claude_cli();
    msg += cli.empty() ? L"not on PATH" : cli;
    return msg;
}

std::wstring shortcuts_body_text() {
    return L"FILES\r\n"
           L"Ctrl+S             Save\r\n"
           L"Ctrl+W             Close tab\r\n"
           L"Ctrl+O             Open file\r\n"
           L"Ctrl+Shift+O       Open folder\r\n"
           L"\r\nSEARCH\r\n"
           L"Ctrl+F             Find\r\n"
           L"Ctrl+H             Replace\r\n"
           L"Ctrl+G             Go to line\r\n"
           L"\r\nSCYLLA\r\n"
           L"Enter              Submit the focused form\r\n"
           L"Escape             Cancel form · close Find · back from Settings\r\n"
           L"Ctrl+`             Toggle Workbench Panel (sessions keep running)\r\n"
           L"View → Problems/Output/Ports   Show panel surface\r\n"
           L"Ctrl+Enter         Send message\r\n"
           L"\r\nMenus: File / Edit / View / Agent / Access / Help";
}

std::wstring getting_started_body_text() {
    return L"GETTING STARTED\r\n\r\n"
           L"1. Open a project folder (sets the agent grant)\r\n"
           L"2. Sign in under Settings → Agent Providers (OpenAI and/or Claude)\r\n"
           L"3. Choose a model in the header (OpenAI · … or Claude · …)\r\n"
           L"4. Open a file in the editor\r\n"
           L"5. Ask about your code in the Agent pane\r\n"
           L"\r\nNote: Agent Send uses Codex for OpenAI and Claude Code print mode for Claude.";
}

void show_editor_content(Ui* ui);
void layout(Ui* ui);

void populate_content_body(Ui* ui) {
    if (!ui->content_body) {
        return;
    }
    std::wstring text;
    switch (ui->content_view) {
        case ContentView::Access:
            text = access_body_text(ui);
            break;
        case ContentView::Diagnostics:
            text = diagnostics_body_text(ui);
            break;
        case ContentView::Shortcuts:
            text = shortcuts_body_text();
            break;
        case ContentView::GettingStarted:
            text = getting_started_body_text();
            break;
        default:
            text.clear();
            break;
    }
    SetWindowTextW(ui->content_body, text.c_str());
}

void show_content_view(Ui* ui, ContentView view, SettingsSection section = SettingsSection::Providers) {
    if (!ui) {
        return;
    }
    if (ui->content_view == ContentView::Editor && view != ContentView::Editor) {
        pull_editor(ui);
    }
    ui->content_view = view;
    ui->settings_section = section;
    if (view == ContentView::Editor) {
        show_editor_content(ui);
        layout(ui);
        if (ui->active_doc >= 0 && ui->editor) {
            SetFocus(ui->editor);
        }
        return;
    }
    if (ui->content_title) {
        std::wstring title = content_view_title(view);
        if (view == ContentView::Settings) {
            title += L"  /  ";
            title += settings_section_label(section);
        }
        SetWindowTextW(ui->content_title, title.c_str());
    }
    if (view == ContentView::Settings && ui->content_nav) {
        SendMessageW(ui->content_nav, LB_RESETCONTENT, 0, 0);
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Agent Providers"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Editor"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Terminal"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Knowledge/Skills"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"MCP"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"STRATA"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Security"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Advanced"));
        SendMessageW(ui->content_nav, LB_SETCURSEL, settings_section_nav_index(section), 0);
        refresh_settings_pane(ui);
        populate_settings_section_body(ui);
        if (section == SettingsSection::Security) {
            std::wstring pname;
            for (const auto& p : ui->session.store.projects) {
                if (p.id == ui->session.store.active_project_id) {
                    pname = p.name;
                    break;
                }
            }
            ui->keyring_ui.ensure_app_vault_bound();
            ui->keyring_ui.set_active_project(ui->session.store.active_project_id, pname);
            ui->keyring_ui.maybe_prompt_migration(ui->wnd);
            apply_security_subpage(ui);
        }
    }
    if (view == ContentView::Keyring) {
        std::wstring pname;
        for (const auto& p : ui->session.store.projects) {
            if (p.id == ui->session.store.active_project_id) {
                pname = p.name;
                break;
            }
        }
        ui->keyring_ui.ensure_app_vault_bound();
        ui->keyring_ui.set_active_project(ui->session.store.active_project_id, pname);
        ui->keyring_ui.show(true);
        ui->keyring_ui.maybe_prompt_migration(ui->wnd);
    } else if (!(view == ContentView::Settings && section == SettingsSection::Security &&
                 ui->security_subpage == SecuritySubpage::Keyring)) {
        // Keep Keyring visible only on Access→Keyring or Security→Keyring.
        if (!(view == ContentView::Settings && section == SettingsSection::Security)) {
            ui->keyring_ui.show(false);
        }
    }
    if (view != ContentView::Settings && view != ContentView::Keyring) {
        populate_content_body(ui);
    }
    refresh_settings_data(ui);
    layout(ui);
}

void show_editor_content(Ui* ui) {
    ui->content_view = ContentView::Editor;
    if (ui->content_host) {
        ShowWindow(ui->content_host, SW_HIDE);
    }
    hide_settings_controls(ui);
    if (ui->content_body) {
        ShowWindow(ui->content_body, SW_HIDE);
    }
    if (ui->content_back) {
        ShowWindow(ui->content_back, SW_HIDE);
    }
    if (ui->content_title) {
        ShowWindow(ui->content_title, SW_HIDE);
    }
}

void go_back_content(Ui* ui) {
    if (ui && ui->content_view != ContentView::Editor) {
        show_content_view(ui, ContentView::Editor);
    }
}

void show_about(HWND parent) {
    MessageBoxW(parent,
                L"Scylla Workbench\nNative Windows client for OpenAI (ChatGPT / Codex) and Claude.\n\n"
                L"Sign in to either or both under Settings → Agent Providers. "
                L"Pick the active model from the header (OpenAI · … / Claude · …) or Default provider.\n\n"
                L"Agent chat Send uses Codex for OpenAI and Claude Code print mode for Claude.",
                L"About Scylla Workbench", MB_OK | MB_ICONINFORMATION);
}

void open_ai_providers_settings(Ui* ui) {
    show_content_view(ui, ContentView::Settings, SettingsSection::Providers);
}

void open_settings_section(Ui* ui, SettingsSection section) {
    show_content_view(ui, ContentView::Settings, section);
}

struct McpAuthJob {
    HWND hwnd = nullptr;
    std::wstring endpoint;
    std::string connection_id;
    std::wstring display_name;
};

struct McpAuthPosted {
    std::string connection_id;
    std::wstring display_name;
    McpOAuthResult result;
};

DWORD WINAPI mcp_auth_worker(LPVOID param) {
    std::unique_ptr<McpAuthJob> job(static_cast<McpAuthJob*>(param));
    if (!job || !job->hwnd) {
        return 0;
    }
    auto* posted = new McpAuthPosted{};
    posted->connection_id = job->connection_id;
    posted->display_name = job->display_name;
    posted->result = mcp_oauth_freshness_check(job->endpoint, job->connection_id);
    PostMessageW(job->hwnd, WM_SCYLLA_MCP_AUTH, 0, reinterpret_cast<LPARAM>(posted));
    return 0;
}

void queue_mcp_auth_checks(Ui* ui, bool force_all) {
    if (!ui) {
        return;
    }
    for (const auto& c : ui->mcp.connections()) {
        if (c.transport_kind != McpTransportKind::Http || !c.enabled || c.disconnected ||
            c.endpoint_or_cmd.empty()) {
            continue;
        }
        if (!force_all && c.auth_state == McpAuthState::NeedsReauth) {
            // Already known to need interactive sign-in — don't probe forever.
            continue;
        }
        if (std::find(ui->mcp_auth_queue.begin(), ui->mcp_auth_queue.end(), c.id) ==
            ui->mcp_auth_queue.end()) {
            ui->mcp_auth_queue.push_back(c.id);
        }
    }
}

void pump_mcp_auth_queue(Ui* ui) {
    if (!ui || ui->mcp_auth_busy || ui->mcp_auth_queue.empty()) {
        return;
    }
    const std::string id = ui->mcp_auth_queue.front();
    ui->mcp_auth_queue.erase(ui->mcp_auth_queue.begin());
    const McpConnection* c = ui->mcp.by_id(id);
    if (!c || c->transport_kind != McpTransportKind::Http || !c->enabled || c->disconnected ||
        c->endpoint_or_cmd.empty()) {
        return;
    }
    auto* job = new McpAuthJob{};
    job->hwnd = ui->wnd;
    job->endpoint = c->endpoint_or_cmd;
    job->connection_id = c->id;
    job->display_name = !c->connection_name.empty() ? c->connection_name : c->display_name;
    ui->mcp_auth_busy = true;
    HANDLE th = CreateThread(nullptr, 0, mcp_auth_worker, job, 0, nullptr);
    if (th) {
        CloseHandle(th);
    } else {
        ui->mcp_auth_busy = false;
        delete job;
    }
}

void handle_mcp_auth_result(Ui* ui, McpAuthPosted* posted) {
    std::unique_ptr<McpAuthPosted> hold(posted);
    if (!ui || !hold) {
        return;
    }
    ui->mcp_auth_busy = false;
    ui->mcp.mark_auth(hold->connection_id, hold->result.state, hold->result.message);
    ui->mcp.save(ui->session.paths.mcp_path);
    if (ui->content_view == ContentView::Settings && ui->settings_section == SettingsSection::Mcp) {
        ui->mcp_ui.refresh(ui->mcp);
    }
    if (hold->result.state != McpAuthState::NeedsReauth) {
        pump_mcp_auth_queue(ui);
        return;
    }
    std::wstring name = hold->display_name.empty() ? L"MCP connection" : hold->display_name;
    std::wstring msg = L"\"" + name +
                       L"\" needs sign-in again.\n\nOpen Settings → MCP to reauthenticate?";
    const int go = MessageBoxW(ui->wnd, msg.c_str(), L"MCP authentication", MB_YESNO | MB_ICONWARNING);
    if (go == IDYES) {
        open_settings_section(ui, SettingsSection::Mcp);
        ui->mcp_ui.select_connection(hold->connection_id, ui->mcp);
        const int reauth =
            MessageBoxW(ui->wnd, L"Start browser sign-in now?", L"Reauthenticate",
                        MB_YESNO | MB_ICONQUESTION);
        if (reauth == IDYES) {
            const McpConnection* c = ui->mcp.by_id(hold->connection_id);
            if (c && c->transport_kind == McpTransportKind::Http && !c->endpoint_or_cmd.empty()) {
                MessageBoxW(ui->wnd,
                            L"A browser window will open so you can sign in.\n\n"
                            L"After you approve access, return here.",
                            L"Reauthenticate", MB_OK | MB_ICONINFORMATION);
                const McpOAuthResult auth =
                    mcp_oauth_authorize(ui->wnd, c->endpoint_or_cmd, c->id);
                if (auto* mut = ui->mcp.by_id(c->id)) {
                    mut->disconnected = false;
                }
                ui->mcp.mark_auth(c->id, auth.state, auth.message);
                ui->mcp.save(ui->session.paths.mcp_path);
                ui->mcp_ui.refresh(ui->mcp);
                if (!auth.ok) {
                    MessageBoxW(ui->wnd,
                                utf16(auth.message.empty() ? "Sign-in failed." : auth.message).c_str(),
                                L"Reauthenticate", MB_OK | MB_ICONWARNING);
                }
            }
        }
    }
    pump_mcp_auth_queue(ui);
}

void refresh_chat_tabs(Ui* ui) {
    if (!ui->chat_tabs) {
        return;
    }
    TabCtrl_DeleteAllItems(ui->chat_tabs);
    int sel = 0;
    for (std::size_t i = 0; i < ui->thread_ids.size(); ++i) {
        std::wstring title = L"New Chat";
        if (auto* c = ui->session.store.by_thread(ui->thread_ids[i])) {
            title = utf16(c->title);
            if (title.empty()) {
                title = L"New Chat";
            }
        } else {
            for (const auto& t : ui->session.threads) {
                if (t.id == ui->thread_ids[i]) {
                    title = utf16(t.name);
                    break;
                }
            }
            if (title.empty()) {
                title = L"New Chat";
            }
        }
        // Clamp display label at 12 characters; shorter titles show in full.
        if (title.size() > 12) {
            title = title.substr(0, 12) + L"…";
        }
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = const_cast<wchar_t*>(title.c_str());
        TabCtrl_InsertItem(ui->chat_tabs, static_cast<int>(i), &it);
        if (ui->thread_ids[i] == ui->session.active_thread_id) {
            sel = static_cast<int>(i);
        }
    }
    if (!ui->thread_ids.empty()) {
        TabCtrl_SetCurSel(ui->chat_tabs, sel);
    }
}

bool rename_chat_id(Ui* ui, const std::string& thread_id) {
    if (!ui || thread_id.empty()) {
        return false;
    }
    auto* c = ui->session.store.by_thread(thread_id);
    if (!c) {
        return false;
    }
    const std::wstring cur = utf16(c->title.empty() ? "New Chat" : c->title);
    const std::wstring next = prompt_text(ui->wnd, L"Rename chat", cur);
    if (next.empty() || next == cur) {
        return false;
    }
    c->title = utf8(next);
    c->title_manual = true;
    persist_store(ui);
    refresh_threads(ui);
    return true;
}

void save_active(Ui* ui) {
    if (ui->active_doc < 0) {
        return;
    }
    OpenDoc& d = ui->docs[ui->active_doc];
    if (d.readonly) {
        MessageBoxW(ui->wnd, L"This buffer is read-only (encoding uncertain). Copy out or convert later.", L"Scylla",
                    MB_ICONWARNING);
        return;
    }
    const std::wstring text = editor_get_text(ui->editor);
    const SaveResult r = save_text_file(d.path, text, d.enc, d.crlf, d.hash);
    if (r.conflict) {
        const int choice = MessageBoxW(ui->wnd,
                                         L"The file changed on disk.\n\nYes = reload disk (lose buffer)\nNo = keep edits (Save still blocked)\nCancel = do nothing",
                                         L"Scylla — conflict", MB_YESNOCANCEL | MB_ICONWARNING);
        if (choice == IDYES) {
            open_document(ui, d.path, true);
        }
        return;
    }
    if (!r.ok) {
        MessageBoxW(ui->wnd, r.error.c_str(), L"Scylla — save failed", MB_ICONERROR);
        return;
    }
    d.text = text;
    d.hash = r.new_hash;
    d.dirty = false;
    d.preview = false;
    refresh_tabs(ui);
}

void check_external(Ui* ui) {
    if (ui->active_doc < 0 || ui->suppress_edit) {
        return;
    }
    OpenDoc& d = ui->docs[ui->active_doc];
    if (d.dirty || d.path.empty()) {
        return;
    }
    const std::uint64_t now = hash_file_bytes(d.path);
    if (now != 0 && now != d.hash) {
        LoadedText loaded = load_text_file(d.path);
        if (!loaded.binary && !(loaded.too_large && loaded.text.empty())) {
            d.text = loaded.text;
            d.hash = loaded.hash;
            d.enc = loaded.enc;
            d.crlf = loaded.crlf;
            d.dirty = false;
            d.large_file = loaded.too_large;
            if (loaded.too_large) {
                d.readonly = true;
            }
            load_doc_into_editor(ui, d);
            refresh_tabs(ui);
        }
    }
}

void refresh_ctx(Ui* ui) {
    SendMessageW(ui->ctx, LB_RESETCONTENT, 0, 0);
    for (const auto& c : ui->session.context_chips) {
        std::wstring row;
        if (c.kind == "knowledge") {
            row = L"[KNOWLEDGE] ";
        }
        row += utf16(c.label);
        if (c.unsaved) {
            row += L"  (unsaved)";
        }
        SendMessageW(ui->ctx, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
}

void add_chip(Ui* ui, ContextChip chip) {
    chip.id = make_uuid();
    ui->session.context_chips.push_back(std::move(chip));
    refresh_ctx(ui);
}

void persist_store(Ui* ui) {
    if (auto* p = ui->session.store.active()) {
        p->files_w = ui->session.settings.files_w;
        p->agent_w = ui->session.settings.agent_w;
        p->history_w = ui->session.settings.history_w;
        p->last_thread_id = ui->session.active_thread_id;
    }
    ui->session.store.save(ui->session.paths.store_path);
}

void refresh_projects(Ui* ui) {
    if (ui->sel_list && ui->sel_owner == ID_PROJECT) {
        return;
    }
    std::wstring name = L"No folder";
    for (const auto& p : ui->session.store.projects) {
        if (p.id == ui->session.store.active_project_id) {
            name = p.name;
            break;
        }
    }
    if (name.empty() && !ui->session.store.projects.empty()) {
        name = ui->session.store.projects.front().name;
    }
    SetWindowTextW(ui->project, name.c_str());
    InvalidateRect(ui->project, nullptr, TRUE);
}

void rebuild_tree(Ui* ui);

// Switching folders leaves live shells behind, still sitting in the old project's directory with
// the old project's environment injected. That is easy to miss, so say it out loud once and let the
// switch be cancelled. Returns true when the caller may proceed.
bool confirm_project_switch_with_live_terminals(Ui* ui) {
    if (!ui || !ui->wnd) {
        return true;
    }
    const auto alive = ui->terminal_sessions.alive_titles();
    if (alive.empty()) {
        return true;
    }
    std::wstring msg = alive.size() == 1 ? L"1 terminal is still running:\r\n\r\n"
                                         : std::to_wstring(alive.size()) + L" terminals are still running:\r\n\r\n";
    const std::size_t shown = alive.size() < 6 ? alive.size() : 6;
    for (std::size_t i = 0; i < shown; ++i) {
        msg += L"    • " + alive[i] + L"\r\n";
    }
    if (shown < alive.size()) {
        msg += L"    • … and " + std::to_wstring(alive.size() - shown) + L" more\r\n";
    }
    msg += L"\r\nThey stay open in the folder and environment they were launched with — switching "
           L"projects does not move or restart them.\r\n\r\nSwitch anyway?";
    return MessageBoxW(ui->wnd, msg.c_str(), L"Terminals still running",
                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON1) == IDOK;
}

void sync_knowledge_agent_grants(Ui* ui) {
    const std::string pid = ui->session.store.active_project_id;
    ui->session.set_knowledge_accessible_paths(ui->knowledge.agent_accessible_paths(pid));
    ui->session.sync_lockdown_config();
}

void apply_project(Ui* ui, const std::string& id) {
    auto* p = ui->session.store.by_id(id);
    if (!p) {
        return;
    }
    if (id != ui->session.store.active_project_id &&
        !confirm_project_switch_with_live_terminals(ui)) {
        refresh_projects(ui);  // repaint the picker: the label must go back to the current project
        return;
    }
    ui->keyring_ui.on_project_switching(id, p->name);
    persist_store(ui);
    ui->session.store.active_project_id = id;
    ui->session.project_root = p->root;
    ui->session.settings.project_folder = p->root;
    ui->session.settings.files_w = p->files_w;
    ui->session.settings.agent_w = p->agent_w;
    ui->session.settings.history_w = p->history_w;
    persist_store(ui);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    sync_knowledge_agent_grants(ui);
    rebuild_tree(ui);
    refresh_projects(ui);
    if (ui->content_view == ContentView::Keyring) {
        ui->keyring_ui.ensure_app_vault_bound();
        ui->keyring_ui.set_active_project(id, p->name);
        ui->keyring_ui.show(true);
        ui->keyring_ui.maybe_prompt_migration(ui->wnd);
        layout(ui);
        return;
    }
    if (ui->content_view == ContentView::Settings) {
        // Project-scoped panels (Knowledge, Security, STRATA, MCP) show per-project rows;
        // without this the previous project's data stayed on screen until the section was reopened.
        if (ui->settings_section == SettingsSection::Security) {
            ui->keyring_ui.ensure_app_vault_bound();
            ui->keyring_ui.set_active_project(id, p->name);
        }
        refresh_settings_data(ui);
        layout(ui);
    }
}

void close_tab(Ui* ui, int i) {
    if (i < 0 || i >= static_cast<int>(ui->docs.size())) {
        return;
    }
    pull_editor(ui);
    if (ui->docs[i].dirty) {
        const int choice = MessageBoxW(ui->wnd, L"Save this file before closing?", L"Scylla",
                                          MB_YESNOCANCEL | MB_ICONWARNING);
        if (choice == IDCANCEL) {
            return;
        }
        if (choice == IDYES) {
            show_doc(ui, i);
            save_active(ui);
            if (ui->docs[i].dirty) {
                return;
            }
        }
    }
    ui->docs.erase(ui->docs.begin() + i);
    if (ui->docs.empty()) {
        ui->active_doc = -1;
        ui->editor_path.clear();
        ui->suppress_edit = true;
        editor_set_text(ui->editor, L"");
        editor_set_readonly(ui->editor, true);
        ui->suppress_edit = false;
        refresh_editor_status(ui);
        TabCtrl_DeleteAllItems(ui->tabs);
        SetWindowTextW(ui->hdr_editor, L"");
        layout(ui);
        return;
    }
    show_doc(ui, std::min(i, static_cast<int>(ui->docs.size()) - 1));
}

void do_find(Ui* ui) {
    std::wstring q = get_window_text(ui->find);
    if (q.empty() || !ui->editor) {
        return;
    }
    if (IsWindowVisible(ui->markdown_view)) {
        CHARRANGE current{}; SendMessageW(ui->markdown_view, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&current));
        FINDTEXTEXW find{}; find.chrg = {current.cpMax, -1}; find.lpstrText = q.c_str();
        if (SendMessageW(ui->markdown_view, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find)) < 0) {
            find.chrg = {0, -1}; SendMessageW(ui->markdown_view, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find));
        }
        if (find.chrgText.cpMax > find.chrgText.cpMin) {
            SendMessageW(ui->markdown_view, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&find.chrgText));
            SendMessageW(ui->markdown_view, EM_SCROLLCARET, 0, 0);
        }
        return;
    }
    editor_find_next(ui->editor, q, false);
    refresh_editor_status(ui);
}

void do_goto(Ui* ui) {
    std::wstring q = get_window_text(ui->find);
    const int line = _wtoi(q.c_str());
    if (line <= 0) {
        SetFocus(ui->find);
        return;
    }
    if (ui->active_doc >= 0) ui->docs[ui->active_doc].markdown_source = true;
    layout(ui);
    editor_goto_line(ui->editor, line);
    refresh_editor_status(ui);
}

void do_replace(Ui* ui) {
    if (!ui->editor || ui->active_doc < 0) {
        return;
    }
    auto& d = ui->docs[ui->active_doc];
    if (d.readonly || d.large_file) {
        MessageBoxW(ui->wnd, L"This buffer is read-only.", L"Scylla", MB_ICONWARNING);
        return;
    }
    if (!ui->find_open) {
        ui->find_open = true;
        layout(ui);
        InvalidateRect(ui->find_toggle, nullptr, TRUE);
    }
    const std::wstring q = get_window_text(ui->find);
    if (q.empty()) {
        SetFocus(ui->find);
        return;
    }
    const std::wstring rep = prompt_text(ui->wnd, L"Replace with", L"");
    if (rep.empty() && MessageBoxW(ui->wnd, L"Replace with empty string?", L"Scylla",
                                   MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    if (editor_replace_next(ui->editor, q, rep, false)) {
        refresh_editor_status(ui);
    }
}

void do_open_file(Ui* ui) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ui->wnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Text\0*.*\0\0";
    ofn.lpstrTitle = L"Open file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        open_document(ui, file, true);
    }
}

void write_recovery(Ui* ui) {
    if (ui->active_doc < 0) {
        return;
    }
    pull_editor(ui);
    const OpenDoc& d = ui->docs[ui->active_doc];
    if (!d.dirty) {
        return;
    }
    const std::wstring name = folder_name(d.path) + L"." + utf16(std::to_string(fnv1a64(d.path.data(), d.path.size() * 2))) + L".txt";
    save_text_file(join_path(ui->session.paths.recovery_dir, name), d.text, TextEnc::Utf8, true, 0);
}

void add_file_chip(Ui* ui) {
    if (ui->active_doc < 0) {
        MessageBoxW(ui->wnd, L"Open a file first. Browsing a tree does not attach it to the agent.", L"Scylla",
                    MB_ICONINFORMATION);
        return;
    }
    pull_editor(ui);
    const OpenDoc& d = ui->docs[ui->active_doc];
    ContextChip c;
    c.kind = d.from_knowledge ? "knowledge" : "file";
    c.path = d.path;
    if (d.from_knowledge) {
        std::wstring label = L"KNOWLEDGE";
        if (!d.knowledge_label.empty()) {
            label += L" / ";
            label += d.knowledge_label;
        }
        label += L" / ";
        label += folder_name(d.path);
        c.label = utf8(label);
    } else {
        c.label = utf8(folder_name(d.path));
    }
    c.body = utf8(d.text);
    c.unsaved = d.dirty;
    add_chip(ui, std::move(c));
}

void add_sel_chip(Ui* ui) {
    if (ui->active_doc < 0) {
        return;
    }
    EditorSel sel = editor_selection(ui->editor);
    if (sel.text.empty()) {
        MessageBoxW(ui->wnd, L"Select text in the editor, then Add selection.", L"Scylla", MB_ICONINFORMATION);
        return;
    }
    pull_editor(ui);
    const OpenDoc& d = ui->docs[ui->active_doc];
    ContextChip c;
    c.kind = "selection";
    c.path = d.path;
    c.label = utf8(folder_name(d.path));
    c.body = utf8(sel.text);
    c.unsaved = d.dirty;
    c.line0 = sel.start_line;
    c.line1 = sel.end_line;
    add_chip(ui, std::move(c));
}

void close_selector(Ui* ui) {
    if (!ui || !ui->sel_list) {
        return;
    }
    HWND popup = ui->sel_list;
    ui->sel_list = nullptr;
    if (GetCapture() == popup) {
        ReleaseCapture();
    }
    DestroyWindow(popup);
    ui->sel_owner = 0;
    ui->sel_face = nullptr;
    ui->sel_items.clear();
    ui->sel_cur = 0;
    ui->sel_hot = -1;
    ui->sel_scroll = 0;
    ui->sel_visible = 0;
    ui->sel_closed_tick = GetTickCount();
}

void refill_model_selector(Ui* ui) {
    (void)ui;
}

void reopen_model_selector(Ui* ui, HWND face) {
    if (!ui || !face) {
        return;
    }
    // Never tear down / rebuild while the agent menu is already open.
    if (ui->sel_list && ui->sel_owner == ID_MODELS) {
        return;
    }
    close_selector(ui);
    ui->sel_closed_tick = 0;  // bypass anti-reopen debounce used for click-toggle
    open_selector(ui, face, ID_MODELS, false);
}

int selector_hit(const Ui* ui, int y) {
    if (!ui || y < 1 || ui->sel_row <= 0) {
        return -1;
    }
    const int visible_index = (y - 1) / ui->sel_row;
    if (visible_index < 0 || visible_index >= ui->sel_visible) {
        return -1;
    }
    const int index = ui->sel_scroll + visible_index;
    return index < static_cast<int>(ui->sel_items.size()) ? index : -1;
}

void selector_ensure_visible(Ui* ui) {
    if (!ui || ui->sel_visible <= 0) {
        return;
    }
    if (ui->sel_cur < ui->sel_scroll) {
        ui->sel_scroll = ui->sel_cur;
    } else if (ui->sel_cur >= ui->sel_scroll + ui->sel_visible) {
        ui->sel_scroll = ui->sel_cur - ui->sel_visible + 1;
    }
    const int max_scroll = std::max(0, static_cast<int>(ui->sel_items.size()) - ui->sel_visible);
    ui->sel_scroll = std::clamp(ui->sel_scroll, 0, max_scroll);
}

LRESULT CALLBACK selector_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = reinterpret_cast<Ui*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ui = static_cast<Ui*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
    }
    if (!ui) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        PAINTSTRUCT ps{};
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wparam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        fill_rect(dc, rc, theme().menu);
        const int end = std::min(static_cast<int>(ui->sel_items.size()), ui->sel_scroll + ui->sel_visible);
        for (int i = ui->sel_scroll; i < end; ++i) {
            RECT row{1, 1 + (i - ui->sel_scroll) * ui->sel_row, rc.right - 1,
                     1 + (i - ui->sel_scroll + 1) * ui->sel_row};
            draw_menu_row(dc, row, ui->font, ui->sel_items[i].c_str(), i == ui->sel_cur || i == ui->sel_hot);
        }
        HBRUSH edge = CreateSolidBrush(theme().divider);
        FrameRect(dc, &rc, edge);
        DeleteObject(edge);
        if (msg == WM_PAINT) {
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    if (msg == WM_GETDLGCODE) {
        return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessageW(ui->wnd, WM_SCYLLA_CLOSE_SEL, 0, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
        if (ui->sel_cur >= 0 && ui->sel_cur < static_cast<int>(ui->sel_items.size())) {
            PostMessageW(ui->wnd, WM_SCYLLA_PICK_SEL, static_cast<WPARAM>(ui->sel_owner),
                         static_cast<LPARAM>(ui->sel_cur));
        }
        return 0;
    }
    if (msg == WM_KEYDOWN &&
        (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_HOME || wparam == VK_END ||
         wparam == VK_PRIOR || wparam == VK_NEXT)) {
        const int last = std::max(0, static_cast<int>(ui->sel_items.size()) - 1);
        if (wparam == VK_UP) {
            ui->sel_cur = std::max(0, ui->sel_cur - 1);
        } else if (wparam == VK_DOWN) {
            ui->sel_cur = std::min(last, ui->sel_cur + 1);
        } else if (wparam == VK_HOME) {
            ui->sel_cur = 0;
        } else if (wparam == VK_END) {
            ui->sel_cur = last;
        } else if (wparam == VK_PRIOR) {
            ui->sel_cur = std::max(0, ui->sel_cur - std::max(1, ui->sel_visible));
        } else {
            ui->sel_cur = std::min(last, ui->sel_cur + std::max(1, ui->sel_visible));
        }
        ui->sel_hot = ui->sel_cur;
        selector_ensure_visible(ui);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        const int max_scroll = std::max(0, static_cast<int>(ui->sel_items.size()) - ui->sel_visible);
        ui->sel_scroll = std::clamp(ui->sel_scroll - (delta / WHEEL_DELTA) * 3, 0, max_scroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        SetFocus(hwnd);
        const int hit = selector_hit(ui, GET_Y_LPARAM(lparam));
        if (hit >= 0) {
            ui->sel_cur = hit;
            ui->sel_hot = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            PostMessageW(ui->wnd, WM_SCYLLA_CLOSE_SEL, 0, 0);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        const int hit = selector_hit(ui, GET_Y_LPARAM(lparam));
        if (hit >= 0) {
            ui->sel_cur = hit;
            PostMessageW(ui->wnd, WM_SCYLLA_PICK_SEL, static_cast<WPARAM>(ui->sel_owner),
                         static_cast<LPARAM>(hit));
        }
        return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        const int hot = selector_hit(ui, GET_Y_LPARAM(lparam));
        if (hot != ui->sel_hot) {
            ui->sel_hot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        ui->sel_hot = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void apply_selector(Ui* ui, int owner, int sel) {
    if (owner == ID_PROJECT) {
        // Index 0 is always "Add Another Folder".
        if (sel == 0) {
            do_open_folder(ui);
            return;
        }
        const int idx = sel - 1;
        if (idx >= 0 && idx < static_cast<int>(ui->session.store.projects.size())) {
            apply_project(ui, ui->session.store.projects[idx].id);
            refresh_threads(ui);
            refresh_chrome(ui);
            layout(ui);
        }
    } else if (owner == ID_MODELS) {
        const std::vector<ModelChoice>& rows =
            !ui->sel_models.empty() ? ui->sel_models : ui->session.models;
        if (sel >= 0 && sel < static_cast<int>(rows.size())) {
            const auto& m = rows[sel];
            ui->session.selected_model = m.id;
            ui->session.settings.selected_model = m.id;
            ui->session.settings.default_provider = m.provider_id.empty() ? "openai" : m.provider_id;
            ui->session.set_provider_default_model(ui->session.settings.default_provider, m.id);
            save_settings(ui->session.paths.settings_path, ui->session.settings);
            if (auto* conv = ui->session.store.by_thread(ui->session.active_thread_id)) {
                conv->provider_id = ui->session.settings.default_provider;
                ui->session.store.save(ui->session.paths.store_path);
            }
            refresh_models(ui);
            refresh_chrome(ui);
            if (m.provider_id == "claude") {
                SetWindowTextW(ui->status, L"Claude selected");
            } else if (ui->session.state == AppState::Ready) {
                SetWindowTextW(ui->status, L"Connected");
            }
        }
    } else if (owner == ID_SCOPE) {
        ui->session.store.history_all_projects = (sel == 1);
        persist_store(ui);
        SetWindowTextW(ui->scope, sel == 1 ? L"All projects" : L"Current project");
        InvalidateRect(ui->scope, nullptr, TRUE);
        refresh_threads(ui);
        layout(ui);
    }
}

std::wstring model_choice_label(const ModelChoice& m) {
    return utf16(std::string(provider_display_name(m.provider_id)) + " · " +
                 (m.display.empty() ? m.id : m.display));
}

void open_selector(Ui* ui, HWND face, int id, bool refetch_models) {
    if (!ui || !face || !IsWindow(face) ||
        (id != ID_PROJECT && id != ID_MODELS && id != ID_SCOPE)) {
        return;
    }
    if (GetTickCount() - ui->sel_closed_tick < 200 && ui->sel_owner == 0) {
        return;
    }
    if (ui->sel_list && ui->sel_owner == id) {
        close_selector(ui);
        return;
    }
    close_selector(ui);
    if (id == ID_MODELS) {
        // Prefer cached Claude session; only force a CLI probe if we have never seen one.
        // A failing forced probe used to clear a good OAuth cache and hide Claude rows.
        if (!claude_is_connected()) {
            claude_code_session_status(true);
        } else if (refetch_models) {
            claude_code_session_status(false);
        }
        ui->session.sync_claude_models();
        // Refetch Codex catalog only when OpenAI is signed in and we have no OpenAI rows yet,
        // or when explicitly reopening after a long gap. Always refetching while the popup is
        // open used to page model/list and race-close the dropdown.
        if (refetch_models && ui->session.account.signed_in) {
            bool has_openai = false;
            for (const auto& m : ui->session.models) {
                if (m.provider_id == "openai") {
                    has_openai = true;
                    break;
                }
            }
            if (!has_openai) {
                ui->session.refresh_models();
            }
        }
    }
    std::vector<std::wstring> items;
    int cur = 0;
    if (id == ID_PROJECT) {
        items.push_back(L"Add Another Folder");
        cur = 0;
        for (std::size_t i = 0; i < ui->session.store.projects.size(); ++i) {
            items.push_back(ui->session.store.projects[i].name);
            if (ui->session.store.projects[i].id == ui->session.store.active_project_id) {
                cur = static_cast<int>(i) + 1;
            }
        }
    } else if (id == ID_MODELS) {
        ui->sel_models.clear();
        const std::string pid = coerce_default_provider(ui->session.settings.default_provider);
        for (const auto& m : ui->session.models) {
            if (m.provider_id != pid) {
                continue;
            }
            if (!ui->session.is_model_enabled(m.provider_id, m.id)) {
                continue;
            }
            ui->sel_models.push_back(m);
        }
        bool found = false;
        for (std::size_t i = 0; i < ui->sel_models.size(); ++i) {
            items.push_back(model_choice_label(ui->sel_models[i]));
            if (ui->sel_models[i].id == ui->session.selected_model) {
                cur = static_cast<int>(i);
                found = true;
            }
        }
        (void)found;
        if (items.empty()) {
            items.push_back(is_api_provider(pid) ? L"Enable models under Settings → Agent Providers"
                                                 : L"Sign in under Settings → Agent Providers");
        }
    } else if (id == ID_SCOPE) {
        items.push_back(L"Current project");
        items.push_back(L"All projects");
        cur = ui->session.store.history_all_projects ? 1 : 0;
    }
    RECT r{};
    GetWindowRect(face, &r);
    const int row = dip(ui->wnd, 28);
    const int w = std::max(static_cast<int>(r.right - r.left), dip(ui->wnd, 280));
    const int max_h = dip(ui->wnd, 360);
    const int visible = std::max(1, std::min(static_cast<int>(items.size()), max_h / row));
    const int h = visible * row + 2;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW popup_class{};
    popup_class.cbSize = sizeof(popup_class);
    if (!GetClassInfoExW(inst, kSelectorClass, &popup_class)) {
        popup_class = {};
        popup_class.cbSize = sizeof(popup_class);
        popup_class.style = CS_DROPSHADOW;
        popup_class.lpfnWndProc = selector_proc;
        popup_class.hInstance = inst;
        popup_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        popup_class.lpszClassName = kSelectorClass;
        if (!RegisterClassExW(&popup_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            SetWindowTextW(ui->status, L"Could not register selector popup");
            return;
        }
    }

    int popup_y = r.bottom + 2;
    HMONITOR monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    if (GetMonitorInfoW(monitor, &monitor_info) && popup_y + h > monitor_info.rcWork.bottom) {
        popup_y = r.top - h - 2;
    }

    ui->sel_items = std::move(items);
    ui->sel_cur = std::clamp(cur, 0, std::max(0, static_cast<int>(ui->sel_items.size()) - 1));
    ui->sel_hot = ui->sel_cur;
    ui->sel_scroll = 0;
    ui->sel_visible = visible;
    ui->sel_row = row;
    selector_ensure_visible(ui);
    ui->sel_list = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSelectorClass, L"", WS_POPUP,
                                   r.left, popup_y, w, h, ui->wnd, nullptr, inst, ui);
    if (!ui->sel_list) {
        const DWORD error = GetLastError();
        wchar_t message[96]{};
        swprintf_s(message, L"Could not create selector popup (Win32 %lu)", error);
        SetWindowTextW(ui->status, message);
        ui->sel_owner = 0;
        ui->sel_face = nullptr;
        ui->sel_items.clear();
        return;
    }
    ui->sel_owner = id;
    ui->sel_face = face;
    ShowWindow(ui->sel_list, SW_SHOWNA);
    SetFocus(ui->sel_list);
    SetCapture(ui->sel_list);
    UpdateWindow(ui->sel_list);
}

LRESULT CALLBACK tabs_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    if (!ui || !ui->tabs_prev) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        HDC dc = nullptr;
        PAINTSTRUCT ps{};
        if (msg == WM_PAINT) {
            dc = BeginPaint(hwnd, &ps);
        } else {
            dc = reinterpret_cast<HDC>(wparam);
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        fill_rect(dc, rc, theme().shell);
        const int n = TabCtrl_GetItemCount(hwnd);
        const int sel = TabCtrl_GetCurSel(hwnd);
        for (int i = 0; i < n; ++i) {
            RECT tr{};
            TabCtrl_GetItemRect(hwnd, i, &tr);
            wchar_t buf[128]{};
            TCITEMW it{};
            it.mask = TCIF_TEXT;
            it.pszText = buf;
            it.cchTextMax = 128;
            TabCtrl_GetItem(hwnd, i, &it);
            draw_tab_item(dc, tr, ui->font, buf, i == sel, false, true);
        }
        if (msg == WM_PAINT) {
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    return CallWindowProcW(ui->tabs_prev, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK chat_tabs_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    if (!ui || !ui->chat_tabs_prev) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        fill_rect(dc, rc, theme().shell);
        const int n = TabCtrl_GetItemCount(hwnd);
        const int sel = TabCtrl_GetCurSel(hwnd);
        for (int i = 0; i < n; ++i) {
            RECT tr{};
            TabCtrl_GetItemRect(hwnd, i, &tr);
            wchar_t buf[128]{};
            TCITEMW it{};
            it.mask = TCIF_TEXT;
            it.pszText = buf;
            it.cchTextMax = 128;
            TabCtrl_GetItem(hwnd, i, &it);
            draw_tab_item(dc, tr, ui->font, buf, i == sel, false, false);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_PRINTCLIENT) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        fill_rect(dc, rc, theme().shell);
        const int n = TabCtrl_GetItemCount(hwnd);
        const int sel = TabCtrl_GetCurSel(hwnd);
        for (int i = 0; i < n; ++i) {
            RECT tr{};
            TabCtrl_GetItemRect(hwnd, i, &tr);
            wchar_t buf[128]{};
            TCITEMW it{};
            it.mask = TCIF_TEXT;
            it.pszText = buf;
            it.cchTextMax = 128;
            TabCtrl_GetItem(hwnd, i, &it);
            draw_tab_item(dc, tr, ui->font, buf, i == sel, false, false);
        }
        return 0;
    }
    if (msg == WM_LBUTTONDBLCLK) {
        TCHITTESTINFO ht{};
        ht.pt.x = GET_X_LPARAM(lparam);
        ht.pt.y = GET_Y_LPARAM(lparam);
        const int i = TabCtrl_HitTest(hwnd, &ht);
        if (i >= 0 && i < static_cast<int>(ui->thread_ids.size())) {
            rename_chat_id(ui, ui->thread_ids[i]);
        }
        return 0;
    }
    return CallWindowProcW(ui->chat_tabs_prev, hwnd, msg, wparam, lparam);
}

// Push current model data into the active settings panel.
//
// This is deliberately NOT called from layout(): every reload() starts with LB_RESETCONTENT,
// so running it per layout pass wiped the user's list selection (and, for STRATA, overwrote
// the edit fields and spawned a bridge process on every resize frame). layout() is geometry
// only; data lands here, on section entry / project switch / explicit refresh.
void refresh_settings_data(Ui* ui) {
    if (!ui || ui->content_view != ContentView::Settings) {
        return;
    }
    switch (ui->settings_section) {
        case SettingsSection::Security: {
            std::wstring pname;
            for (const auto& p : ui->session.store.projects) {
                if (p.id == ui->session.store.active_project_id) {
                    pname = p.name;
                    break;
                }
            }
            ui->keyring_ui.ensure_app_vault_bound();
            ui->keyring_ui.set_active_project(ui->session.store.active_project_id, pname);
            if (ui->security_subpage == SecuritySubpage::Overview) {
                ui->security_overview.reload(ui->keyring_ui.keyring(), ui->environments,
                                             ui->session.store.active_project_id, pname,
                                             ui->session.settings.execution_policy);
            } else if (ui->security_subpage == SecuritySubpage::Policy) {
                ui->security_policy.reload(ui->session.settings.execution_policy);
            } else if (ui->security_subpage == SecuritySubpage::Environments) {
                ui->environment_ui.reload(ui->environments, ui->keyring_ui.keyring(),
                                          ui->session.store.active_project_id, pname,
                                          ui->session.paths.environments_path);
            } else if (ui->security_subpage == SecuritySubpage::Connections) {
                ui->connections_ui.reload(ui->connections, ui->keyring_ui.keyring(),
                                          ui->session.store.active_project_id, pname);
            } else if (ui->security_subpage == SecuritySubpage::Keyring) {
                ui->keyring_ui.refresh();
            }
            break;
        }
        case SettingsSection::Mcp:
            ui->mcp_ui.refresh(ui->mcp);
            break;
        case SettingsSection::Strata: {
            const std::string pid = ui->session.store.active_project_id;
            const auto workspace = resolve_strata_workspace(ui->knowledge, pid, ui->session.project_root);
            ui->strata_ui.set_workspace_path(workspace);
            ui->strata_bridge.set_default_workspace(workspace);
            if (!pid.empty()) {
                if (const StrataBinding* b = ui->strata.binding_for(pid)) {
                    ui->strata_ui.set_project_binding(
                        utf16(b->strata_project.empty() ? b->project_id : b->strata_project));
                } else {
                    ui->strata_ui.set_project_binding(utf16(pid));
                }
            }
            ui->strata_ui.refresh(&ui->strata_bridge, &ui->strata);
            break;
        }
        case SettingsSection::Terminal:
            if (ui->terminal_profiles.empty()) {
                reload_terminal_profiles(ui);
            }
            ui->terminal_ui.reload(ui->terminal_profiles, ui->session.settings);
            break;
        case SettingsSection::Knowledge:
            ui->knowledge_ui.reload(ui->knowledge, ui->session.store.active_project_id);
            break;
        default:
            break;
    }
}

// Which panel owns the keyboard right now, for Enter/Esc routing.
// Returns 0 when the active surface has no default action for |field|.
UINT panel_default_command(Ui* ui, HWND field) {
    if (!ui) {
        return 0;
    }
    if (ui->content_view == ContentView::Keyring) {
        return ui->keyring_ui.default_command(field);
    }
    if (ui->content_view != ContentView::Settings) {
        return 0;
    }
    switch (ui->settings_section) {
        case SettingsSection::Knowledge:
            return ui->knowledge_ui.default_command(field);
        case SettingsSection::Mcp:
            return ui->mcp_ui.default_command(field);
        case SettingsSection::Strata:
            return ui->strata_ui.default_command(field);
        case SettingsSection::Terminal:
            return ui->terminal_ui.default_command(field);
        case SettingsSection::Security:
            if (ui->security_subpage == SecuritySubpage::Environments &&
                ui->environment_ui.owns_hwnd(field)) {
                return ui->environment_ui.default_command(field);
            }
            if (ui->security_subpage == SecuritySubpage::Connections &&
                ui->connections_ui.owns_hwnd(field)) {
                return ui->connections_ui.default_command(field);
            }
            if (ui->security_subpage == SecuritySubpage::Keyring) {
                return ui->keyring_ui.default_command(field);
            }
            return 0;
        default:
            return 0;
    }
}

// Cancel an open form in the active panel. Returns true when Esc was consumed.
bool cancel_active_form(Ui* ui) {
    if (!ui) {
        return false;
    }
    UINT cmd = 0;
    if (ui->content_view == ContentView::Keyring) {
        cmd = ui->keyring_ui.cancel_command();
    } else if (ui->content_view == ContentView::Settings) {
        switch (ui->settings_section) {
            case SettingsSection::Knowledge:
                cmd = ui->knowledge_ui.cancel_command();
                break;
            case SettingsSection::Mcp:
                cmd = ui->mcp_ui.cancel_command();
                break;
            case SettingsSection::Strata:
                cmd = ui->strata_ui.cancel_command();
                break;
            case SettingsSection::Security:
                if (ui->security_subpage == SecuritySubpage::Environments) {
                    cmd = ui->environment_ui.cancel_command();
                } else if (ui->security_subpage == SecuritySubpage::Connections) {
                    cmd = ui->connections_ui.cancel_command();
                } else if (ui->security_subpage == SecuritySubpage::Keyring) {
                    cmd = ui->keyring_ui.cancel_command();
                }
                break;
            default:
                break;
        }
    }
    if (!cmd) {
        return false;
    }
    SendMessageW(ui->wnd, WM_COMMAND, MAKEWPARAM(cmd, BN_CLICKED), 0);
    return true;
}

void layout(Ui* ui) {
    RECT rc{};
    GetClientRect(ui->wnd, &rc);
    const int w = rc.right;
    const int h = rc.bottom;
    const int dpi_w = px_to_dip(ui->wnd, w);
    ui->panes = compute_panes(dpi_w, ui->session.settings.files_w, ui->session.settings.agent_w,
                              ui->session.settings.history_w, ui->session.settings.focus_editor,
                              ui->session.settings.files_mode, ui->session.settings.history_mode, ui->narrow_tab);

    const bool stacked_file_tabs = dpi_w < 1100 && !ui->docs.empty();
    const int toolbar_h = dip(ui->wnd, 40);
    const int bar = toolbar_h + (stacked_file_tabs ? dip(ui->wnd, 34) : 0);
    const int st = dip(ui->wnd, 22);
    const int hdr = dip(ui->wnd, 34);
    const int pad = dip(ui->wnd, 12);
    const int btnw = dip(ui->wnd, 30);
    const int btnh = dip(ui->wnd, 28);
    const int ybtn = (toolbar_h - btnh) / 2;
    const int filter_h = dip(ui->wnd, 28);
    const int thumb_extra = ui->images.empty() ? 0 : dip(ui->wnd, 56);
    // Self-heal after programmatic text changes (thread switch, send, clear) so the box does not
    // stay tall with an empty draft.
    sync_composer_growth(ui);
    const int composer_text = composer_line_height(ui) * ui->composer_lines + dip(ui->wnd, 2);
    const int composer = composer_text + dip(ui->wnd, 56) + thumb_extra;

    const bool narrow = ui->panes.narrow_tabs;
    ShowWindow(ui->tab_editor, narrow ? SW_SHOW : SW_HIDE);
    ShowWindow(ui->tab_agent, narrow ? SW_SHOW : SW_HIDE);

    int x = pad;
    ShowWindow(ui->brand, SW_HIDE);
    const int project_w = dip(ui->wnd, dpi_w < 900 ? (std::max)(80, dpi_w - 640) : 200);
    MoveWindow(ui->project, x, ybtn, project_w, btnh, TRUE);
    x += project_w + dip(ui->wnd, 8);
    MoveWindow(ui->openfolder, x, ybtn, btnh, btnh, TRUE);
    x += dip(ui->wnd, 40);
    const int file_tabs_left = x;
    const int account_w = dip(ui->wnd, dpi_w < 1100 ? 100 : 136);
    const int model_w = dip(ui->wnd, dpi_w < 1100 ? 160 : 200);
    const int gap = dip(ui->wnd, 8);
    const int settings_x = w - pad - btnh;
    const int account_x = settings_x - gap - account_w;
    const int model_x = account_x - gap - model_w;
    const int focus_x = model_x - gap - dip(ui->wnd, 64);
    const int chats_x = focus_x - dip(ui->wnd, 60);
    const int files_x = chats_x - dip(ui->wnd, 60);
    MoveWindow(ui->toggle_files, files_x, ybtn, dip(ui->wnd, 56), btnh, TRUE);
    MoveWindow(ui->toggle_history, chats_x, ybtn, dip(ui->wnd, 56), btnh, TRUE);
    MoveWindow(ui->focus, focus_x, ybtn, dip(ui->wnd, 64), btnh, TRUE);
    MoveWindow(ui->settings, settings_x, ybtn, btnh, btnh, TRUE);
    MoveWindow(ui->account, account_x, ybtn, account_w, btnh, TRUE);
    MoveWindow(ui->models, model_x, ybtn, model_w, btnh, TRUE);
    // File tabs belong to the main chrome. A second chrome row at compact widths
    // keeps tab overflow and the controls from occupying the same hit area.
    MoveWindow(ui->tabs, stacked_file_tabs ? pad : file_tabs_left,
               stacked_file_tabs ? toolbar_h : (toolbar_h - hdr),
               stacked_file_tabs ? w - pad * 2 : (std::max)(1, files_x - gap - file_tabs_left), hdr, TRUE);
    ShowWindow(ui->models, SW_SHOW);
    ShowWindow(ui->signin, SW_HIDE);
    ShowWindow(ui->signout, SW_HIDE);

    int body_y = bar;
    int body_h = h - bar - st;
    if (narrow) {
        MoveWindow(ui->tab_editor, pad, bar + dip(ui->wnd, 4), dip(ui->wnd, 72), btnh, TRUE);
        MoveWindow(ui->tab_agent, pad + dip(ui->wnd, 80), bar + dip(ui->wnd, 4), dip(ui->wnd, 72), btnh, TRUE);
        body_y = bar + dip(ui->wnd, 36);
        body_h = h - body_y - st;
    }

    const int min_term = ui_space::kMinTerminalHDip;
    int term_h_dip = (std::max)(min_term, ui->session.settings.terminal_h);
    if (ui->panel_collapsed) {
        term_h_dip = ui_space::kPanelTabHDip + 8;
    }
    const int term_band = ui->session.settings.terminal_visible
        ? (std::min)(dip(ui->wnd, term_h_dip), (std::max)(0, body_h - dip(ui->wnd, 100))) : 0;
    const int term_splitter = term_band > 0 ? dip(ui->wnd, 8) : 0;
    if (term_band > 0) {
        body_h = (std::max)(0, body_h - term_band - term_splitter);
    }

    auto pxw = [&](int dips) { return dip(ui->wnd, dips); };
    int cx = 0;
    ui->split1 = {};
    ui->split2 = {};
    ui->split3 = {};
    ui->split_term = {};
    ui->split_knowledge = {};

    auto place_files = [&](int ww) {
        ShowWindow(ui->hdr_files, SW_HIDE);
        ShowWindow(ui->filter, SW_SHOW);
        ShowWindow(ui->tree, SW_SHOW);
        const int breath = dip(ui->wnd, 5);
        MoveWindow(ui->filter, cx + pad, body_y + breath, ww - pad * 2, filter_h, TRUE);
        center_single_line_edit(ui->filter, ui->font);
        const int heading = dip(ui->wnd, 28);
        const int available = (std::max)(0, body_h - breath * 2 - filter_h);
        const int default_knowledge_h = (std::min)(dip(ui->wnd, 260), (std::max)(heading, available / 3));
        const int max_knowledge_h = (std::max)(heading, available - dip(ui->wnd, 96) - breath);
        const int min_knowledge_h = (std::min)(dip(ui->wnd, 80), max_knowledge_h);
        const int knowledge_h = ui->knowledge_collapsed ? heading
            : ui->session.settings.knowledge_h == 0 ? default_knowledge_h
            : std::clamp(dip(ui->wnd, ui->session.settings.knowledge_h), min_knowledge_h, max_knowledge_h);
        ui->knowledge_height = px_to_dip(ui->wnd, knowledge_h);
        ui->knowledge_max_height = px_to_dip(ui->wnd, max_knowledge_h);
        const int knowledge_y = body_y + body_h - knowledge_h;
        if (!ui->knowledge_collapsed) {
            // Existing breathing space between the trees doubles as a resize target.
            // No child HWND covers it, so the frame owns capture just like the terminal splitter.
            ui->split_knowledge = {cx + breath, knowledge_y - breath, cx + ww - breath, knowledge_y};
        }
        MoveWindow(ui->tree, cx + breath, body_y + breath + filter_h + breath, ww - breath * 2,
                   (std::max)(0, available - knowledge_h - breath), TRUE);
        MoveWindow(ui->knowledge_header, cx + breath, knowledge_y, (std::max)(1, ww - breath * 2 - heading), heading, TRUE);
        MoveWindow(ui->knowledge_manage, cx + ww - breath - heading, knowledge_y, heading, heading, TRUE);
        SetWindowTextW(ui->knowledge_header, ui->knowledge_collapsed ? L"▸ KNOWLEDGE" : L"▾ KNOWLEDGE");
        ShowWindow(ui->knowledge_header, SW_SHOWNA);
        ShowWindow(ui->knowledge_manage, SW_SHOWNA);
        MoveWindow(ui->knowledge_tree, cx + breath, knowledge_y + heading, ww - breath * 2,
                   (std::max)(0, knowledge_h - heading), TRUE);
        ShowWindow(ui->knowledge_tree, ui->knowledge_collapsed ? SW_HIDE : SW_SHOWNA);
        cx += ww;
    };
    auto hide_files = [&]() {
        ShowWindow(ui->hdr_files, SW_HIDE);
        ShowWindow(ui->filter, SW_HIDE);
        ShowWindow(ui->tree, SW_HIDE);
        ShowWindow(ui->knowledge_tree, SW_HIDE);
        ShowWindow(ui->knowledge_header, SW_HIDE);
        ShowWindow(ui->knowledge_manage, SW_HIDE);
    };
    auto place_editor = [&](int ww) {
        const bool utility = ui->content_view != ContentView::Editor;
        if (utility) {
            ShowWindow(ui->empty_editor, SW_HIDE);
            ShowWindow(ui->empty_open_file, SW_HIDE);
            ShowWindow(ui->empty_open_folder, SW_HIDE);
            ShowWindow(ui->tabs, SW_HIDE);
            ShowWindow(ui->hdr_editor, SW_HIDE);
            ShowWindow(ui->find_toggle, SW_HIDE);
            ShowWindow(ui->find, SW_HIDE);
            ShowWindow(ui->save, SW_HIDE);
            ShowWindow(ui->gutter, SW_HIDE);
            ShowWindow(ui->editor_status, SW_HIDE);
            ShowWindow(ui->editor, SW_HIDE);
            ShowWindow(ui->markdown_view, SW_HIDE);
            ShowWindow(ui->markdown_toggle, SW_HIDE);
            // Background panel only. SW_SHOW would activate and raise it over Settings controls
            // (owner-draw buttons/nav then look blank until hover). SW_SHOWNA keeps creation order.
            ShowWindow(ui->content_host, SW_SHOWNA);
            EnableWindow(ui->content_host, FALSE);
            ShowWindow(ui->content_back, SW_SHOW);
            ShowWindow(ui->content_title, SW_SHOW);
            const int hdr_h = dip(ui->wnd, 36);
            const int gap_below_hdr = dip(ui->wnd, 16);  // space between Back/title and nav/body
            const int pad_u = dip(ui->wnd, 12);
            const int hdr_y = body_y + (hdr_h - btnh) / 2;  // shared vertical mid with Back
            MoveWindow(ui->content_host, cx, body_y, ww, body_h, TRUE);
            MoveWindow(ui->content_back, cx + pad_u, hdr_y, dip(ui->wnd, 72), btnh, TRUE);
            MoveWindow(ui->content_title, cx + pad_u + dip(ui->wnd, 80), hdr_y,
                       ww - pad_u * 2 - dip(ui->wnd, 80), btnh, TRUE);
            hide_settings_controls(ui);
            ShowWindow(ui->gs_open_folder, SW_HIDE);
            ShowWindow(ui->gs_providers, SW_HIDE);
            ShowWindow(ui->content_body, SW_HIDE);
            ShowWindow(ui->content_nav, SW_HIDE);
            const int y0 = body_y + hdr_h + gap_below_hdr;
            const int h0 = body_h - hdr_h - gap_below_hdr - pad_u;
            if (ui->content_view == ContentView::Settings) {
                const int nav_w = dip(ui->wnd, 140);
                ShowWindow(ui->content_nav, SW_SHOW);
                MoveWindow(ui->content_nav, cx + pad_u, y0, nav_w, h0, TRUE);
                InvalidateRect(ui->content_nav, nullptr, TRUE);
                InvalidateRect(ui->content_back, nullptr, TRUE);
                InvalidateRect(ui->content_title, nullptr, TRUE);
                const int px = cx + pad_u + nav_w + pad_u;
                const int pw = ww - nav_w - pad_u * 3;
                int py = y0;
                auto place_btn = [&](HWND h, int bw, int bh = -1) {
                    const int hh = bh < 0 ? btnh : bh;
                    MoveWindow(h, px, py, bw, hh, TRUE);
                    ShowWindow(h, SW_SHOW);
                    InvalidateRect(h, nullptr, TRUE);
                    py += hh + dip(ui->wnd, 8);
                };
                if (ui->settings_section == SettingsSection::Providers) {
                    RECT area{px, py, px + pw, y0 + h0};
                    ui->providers_ui.set_visible(true);
                    ui->providers_ui.layout(area);
                    refresh_settings_pane(ui);
                } else if (ui->settings_section == SettingsSection::Editor) {
                    // Full content-column width — not a narrow chip column.
                    const int opt_h = dip(ui->wnd, 40);
                    const int opt_gap = dip(ui->wnd, 16);
                    const int opt_w = (std::max)(1, pw);
                    auto place_opt = [&](HWND h) {
                        MoveWindow(h, px, py, opt_w, opt_h, TRUE);
                        ShowWindow(h, SW_SHOW);
                        InvalidateRect(h, nullptr, TRUE);
                        py += opt_h + opt_gap;
                    };
                    place_opt(ui->set_wrap);
                    place_opt(ui->set_whitespace);
                    ShowWindow(ui->set_enter_sends, SW_HIDE);
                    refresh_settings_pane(ui);
                } else if (ui->settings_section == SettingsSection::Advanced) {
                    if (ui->ui_gallery.visible()) {
                        RECT gr{px, y0, px + (std::max)(1, pw), y0 + h0};
                        ui->ui_gallery.layout(gr);
                    } else {
                        place_btn(ui->set_codex, dip(ui->wnd, 160));
                        place_btn(ui->set_copy_runtime, dip(ui->wnd, 160));
                        place_btn(ui->set_ui_gallery, dip(ui->wnd, 140));
                        ShowWindow(ui->content_body, SW_SHOW);
                        SetWindowTextW(ui->content_body,
                                       L"Runtime paths and developer tools.\r\n\r\n"
                                       L"Scylla Keyring lives under Settings → Security.");
                        MoveWindow(ui->content_body, px, py + dip(ui->wnd, 8), (std::max)(1, pw),
                                   (std::max)(1, h0 - (py - y0) - dip(ui->wnd, 8)), TRUE);
                    }
                } else if (ui->settings_section == SettingsSection::Security) {
                    const int tab_h = dip(ui->wnd, 32);
                    const int tab_gap = dip(ui->wnd, 8);
                    const int tab_w = dip(ui->wnd, 140);
                    // Widths differ per label; walk the strip so a new tab never overlaps.
                    const struct {
                        HWND tab;
                        int w;
                    } sec_tabs[] = {{ui->sec_tab_overview, tab_w},
                                    {ui->sec_tab_keyring, tab_w},
                                    {ui->sec_tab_environments, tab_w + dip(ui->wnd, 40)},
                                    {ui->sec_tab_connections, tab_w},
                                    {ui->sec_tab_policy, tab_w + dip(ui->wnd, 10)}};
                    int tab_x = px;
                    for (const auto& t : sec_tabs) {
                        if (t.tab) {
                            ShowWindow(t.tab, SW_SHOW);
                            MoveWindow(t.tab, tab_x, y0, t.w, tab_h, TRUE);
                        }
                        tab_x += t.w + tab_gap;
                    }
                    RECT body{px, y0 + tab_h + tab_gap, px + (std::max)(1, pw), y0 + h0};
                    apply_security_subpage(ui);
                    if (ui->security_subpage == SecuritySubpage::Overview) {
                        ui->security_overview.layout(body);
                    } else if (ui->security_subpage == SecuritySubpage::Environments) {
                        ui->environment_ui.layout(body);
                    } else if (ui->security_subpage == SecuritySubpage::Connections) {
                        ui->connections_ui.layout(body);
                    } else if (ui->security_subpage == SecuritySubpage::Keyring) {
                        ui->keyring_ui.layout(body);
                    } else if (ui->security_subpage == SecuritySubpage::Policy) {
                        ui->security_policy.layout(body);
                    }
                } else if (ui->settings_section == SettingsSection::Knowledge) {
                    RECT kr{px, y0, px + (std::max)(1, pw), y0 + h0};
                    ui->knowledge_ui.set_visible(true);
                    ui->knowledge_ui.layout(kr);
                } else if (ui->settings_section == SettingsSection::Mcp) {
                    RECT mr{px, y0, px + (std::max)(1, pw), y0 + h0};
                    ui->mcp_ui.show();
                    ui->mcp_ui.layout(mr);
                } else if (ui->settings_section == SettingsSection::Strata) {
                    RECT sr{px, y0, px + (std::max)(1, pw), y0 + h0};
                    ui->strata_ui.set_visible(true);
                    ui->strata_ui.layout(sr);
                } else if (ui->settings_section == SettingsSection::Terminal) {
                    RECT tr{px, y0, px + (std::max)(1, pw), y0 + h0};
                    ui->terminal_ui.set_visible(true);
                    ui->terminal_ui.layout(tr);
                }
            } else if (ui->content_view == ContentView::Keyring) {
                ShowWindow(ui->content_body, SW_HIDE);
                RECT kr{cx + pad_u, y0, cx + ww - pad_u, y0 + h0};
                ui->keyring_ui.show(true);
                ui->keyring_ui.layout(kr);
            } else {
                ShowWindow(ui->content_body, SW_SHOW);
                MoveWindow(ui->content_body, cx + pad_u, y0, ww - pad_u * 2, h0 - (ui->content_view == ContentView::GettingStarted
                                                                                       ? dip(ui->wnd, 40)
                                                                                       : 0),
                           TRUE);
                if (ui->content_view == ContentView::GettingStarted) {
                    MoveWindow(ui->gs_open_folder, cx + pad_u, body_y + body_h - dip(ui->wnd, 36), dip(ui->wnd, 120), btnh,
                               TRUE);
                    MoveWindow(ui->gs_providers, cx + pad_u + dip(ui->wnd, 128), body_y + body_h - dip(ui->wnd, 36),
                               dip(ui->wnd, 140), btnh, TRUE);
                    ShowWindow(ui->gs_open_folder, SW_SHOW);
                    ShowWindow(ui->gs_providers, SW_SHOW);
                }
            }
            cx += ww;
            return;
        }
        ShowWindow(ui->content_host, SW_HIDE);
        ShowWindow(ui->content_back, SW_HIDE);
        ShowWindow(ui->content_title, SW_HIDE);
        ShowWindow(ui->content_nav, SW_HIDE);
        ShowWindow(ui->content_body, SW_HIDE);
        hide_settings_controls(ui);
        ShowWindow(ui->gs_open_folder, SW_HIDE);
        ShowWindow(ui->gs_providers, SW_HIDE);
        const bool has_doc = !ui->docs.empty();
        const bool markdown = has_doc && ui->active_doc >= 0 && ui->docs[ui->active_doc].language == "markdown" && !ui->docs[ui->active_doc].large_file;
        const bool preview_md = markdown && !ui->docs[ui->active_doc].markdown_source;
        ShowWindow(ui->empty_editor, has_doc ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_open_file, has_doc ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_open_folder, has_doc ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->tabs, has_doc ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->hdr_editor, has_doc ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->find_toggle, has_doc ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->find, (has_doc && ui->find_open) ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->save, has_doc ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->gutter, SW_HIDE);
        ShowWindow(ui->editor_status, has_doc ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->editor, has_doc && !preview_md ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->markdown_view, preview_md ? SW_SHOW : SW_HIDE);
        ShowWindow(ui->markdown_toggle, markdown ? SW_SHOW : SW_HIDE);
        SetWindowTextW(ui->markdown_toggle, preview_md ? L"Source" : L"Preview");
        if (!has_doc) {
            MoveWindow(ui->empty_editor, cx, body_y, ww, body_h, TRUE);
            // Same stack as paint_empty_editor — center in viewport on every resize.
            const int mark_h = dip(ui->wnd, 12);
            const int mark_gap = dip(ui->wnd, 12);
            const int title_h = dip(ui->wnd, 28);
            const int title_gap = dip(ui->wnd, 8);
            const int sub_h = dip(ui->wnd, 20);
            const int sub_to_btn = dip(ui->wnd, 16);
            const int content_h = mark_h + mark_gap + title_h + title_gap + sub_h + sub_to_btn + btnh;
            const int y0 = body_y + (std::max)(0, (body_h - content_h) / 2);
            const int btn_y = y0 + mark_h + mark_gap + title_h + title_gap + sub_h + sub_to_btn;
            const int mid_x = cx + ww / 2;
            MoveWindow(ui->empty_open_file, mid_x - dip(ui->wnd, 128), btn_y, dip(ui->wnd, 120), btnh, TRUE);
            MoveWindow(ui->empty_open_folder, mid_x + dip(ui->wnd, 8), btn_y, dip(ui->wnd, 120), btnh, TRUE);
            cx += ww;
            return;
        }
        const int tab_h = 0; // Tabs now occupy the main window chrome.
        const int crumb = dip(ui->wnd, 32);
        const int status_w = dip(ui->wnd, 280);
        const int trail = btnh * 2 + 8 + (markdown ? dip(ui->wnd, 80) : 0);
        MoveWindow(ui->markdown_toggle, cx + ww - btnh * 2 - 8 - dip(ui->wnd, 80), body_y + (crumb - btnh) / 2, dip(ui->wnd, 76), btnh, TRUE);
        MoveWindow(ui->find_toggle, cx + ww - btnh * 2 - 8, body_y + (crumb - btnh) / 2, btnh, btnh, TRUE);
        MoveWindow(ui->save, cx + ww - btnh, body_y + (crumb - btnh) / 2, btnh, btnh, TRUE);
        if (ui->find_open) {
            const int find_w = dip(ui->wnd, 180);
            const int crumb_w = ww - trail - pad * 2 - find_w - status_w - 16;
            MoveWindow(ui->hdr_editor, cx + pad, body_y + tab_h, (std::max)(dip(ui->wnd, 80), crumb_w), crumb, TRUE);
            MoveWindow(ui->find, cx + ww - trail - pad - find_w - status_w - 8, body_y + tab_h + (crumb - filter_h) / 2, find_w,
                       filter_h, TRUE);
            center_single_line_edit(ui->find, ui->font);
            MoveWindow(ui->editor_status, cx + ww - trail - pad - status_w, body_y + tab_h, status_w, crumb, TRUE);
        } else {
            MoveWindow(ui->hdr_editor, cx + pad, body_y + tab_h, (std::max)(dip(ui->wnd, 60), ww - trail - pad * 2 - status_w - 8), crumb, TRUE);
            MoveWindow(ui->editor_status, cx + ww - trail - pad - status_w, body_y + tab_h, status_w, crumb, TRUE);
        }
        MoveWindow(ui->editor, cx, body_y + tab_h + crumb, ww, body_h - tab_h - crumb, TRUE);
        MoveWindow(ui->markdown_view, cx, body_y + tab_h + crumb, ww, body_h - tab_h - crumb, TRUE);
        cx += ww;
    };
    auto hide_editor = [&]() {
        ShowWindow(ui->hdr_editor, SW_HIDE);
        ShowWindow(ui->tabs, SW_HIDE);
        ShowWindow(ui->find, SW_HIDE);
        ShowWindow(ui->find_toggle, SW_HIDE);
        ShowWindow(ui->save, SW_HIDE);
        ShowWindow(ui->gutter, SW_HIDE);
        ShowWindow(ui->editor_status, SW_HIDE);
        ShowWindow(ui->editor, SW_HIDE);
        ShowWindow(ui->markdown_view, SW_HIDE);
        ShowWindow(ui->markdown_toggle, SW_HIDE);
        ShowWindow(ui->empty_editor, SW_HIDE);
        ShowWindow(ui->empty_open_file, SW_HIDE);
        ShowWindow(ui->empty_open_folder, SW_HIDE);
        ShowWindow(ui->content_host, SW_HIDE);
        ShowWindow(ui->content_back, SW_HIDE);
        ShowWindow(ui->content_title, SW_HIDE);
        ShowWindow(ui->content_nav, SW_HIDE);
        ShowWindow(ui->content_body, SW_HIDE);
        hide_settings_controls(ui);
        ShowWindow(ui->gs_open_folder, SW_HIDE);
        ShowWindow(ui->gs_providers, SW_HIDE);
    };
    auto place_agent = [&](int ww) {
        ShowWindow(ui->hdr_agent, SW_HIDE);
        ShowWindow(ui->chat_tabs, SW_SHOW);
        ShowWindow(ui->neu, SW_SHOW);
        ShowWindow(ui->agent_hint, SW_HIDE);
        ShowWindow(ui->ctx, ui->session.context_chips.empty() ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->composer, SW_SHOW);
        ShowWindow(ui->add_file, SW_HIDE);
        ShowWindow(ui->workflow, SW_SHOW);
        ShowWindow(ui->send, SW_SHOW);
        ShowWindow(ui->cancel, SW_HIDE);
        MoveWindow(ui->chat_tabs, cx + pad, body_y, ww - pad - btnh - 8, hdr, TRUE);
        MoveWindow(ui->neu, cx + ww - pad - btnh, body_y + (hdr - btnh) / 2, btnh, btnh, TRUE);
        const int chip_h = ui->session.context_chips.empty() ? 0 : dip(ui->wnd, 26);
        if (chip_h) {
            MoveWindow(ui->ctx, cx + pad, body_y + hdr, ww - pad * 2, chip_h, TRUE);
        }
        const int activity_h = ui->session.activity.visible ? dip(ui->wnd, 96) : 0;
        const int trans_h = body_h - hdr - chip_h - composer - activity_h;
        const int ty = body_y + hdr + chip_h;
        const bool empty = chat_log_empty(ui->chat_log);
        ShowWindow(ui->transcript, SW_HIDE);
        ShowWindow(ui->chat_log, empty ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_agent, empty ? SW_SHOW : SW_HIDE);
        // The chat log applies its own grid padding internally; a second inset here made the
        // gutters disagree with every other pane.
        MoveWindow(ui->chat_log, cx, ty, ww, std::max(dip(ui->wnd, 80), trans_h), TRUE);
        MoveWindow(ui->empty_agent, cx, ty, ww, std::max(dip(ui->wnd, 80), trans_h), TRUE);
        const int activity_y = ty + std::max(dip(ui->wnd, 80), trans_h);
        ShowWindow(ui->activity, activity_h ? SW_SHOW : SW_HIDE);
        MoveWindow(ui->activity, cx + pad, activity_y, ww - pad * 2, activity_h, TRUE);
        const int cy = activity_y + activity_h;
        const int box_pad = pad;
        ui->composer_box = {cx + box_pad, cy + 4, cx + ww - box_pad, cy + composer - 8};
        const RECT& box = ui->composer_box;
        MoveWindow(ui->composer_panel, box.left, box.top, box.right - box.left, box.bottom - box.top, TRUE);
        ShowWindow(ui->composer_panel, SW_SHOW);
        SetWindowPos(ui->composer_panel, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const int inner = dip(ui->wnd, 12);
        const int foot = dip(ui->wnd, 32);
        const int send_s = dip(ui->wnd, 28);
        const int thumb_h = ui->images.empty() ? 0 : dip(ui->wnd, 56);
        ui->thumb_row = {box.left + inner, box.top + dip(ui->wnd, 6), box.right - inner,
                         box.top + dip(ui->wnd, 6) + (thumb_h ? dip(ui->wnd, 48) : 0)};
        const int text_top = box.top + (thumb_h ? thumb_h : dip(ui->wnd, 8));
        const int text_h = (box.bottom - foot) - text_top - 4;
        MoveWindow(ui->composer, box.left + inner, text_top, (box.right - box.left) - inner * 2,
                   std::max(dip(ui->wnd, 36), text_h), TRUE);
        MoveWindow(ui->composer_cue, box.left + inner + 4, text_top + 2, dip(ui->wnd, 320), dip(ui->wnd, 20), TRUE);
        MoveWindow(ui->workflow, box.left + inner, box.bottom - foot, dip(ui->wnd, 100), dip(ui->wnd, 26), TRUE);
        MoveWindow(ui->send, box.right - inner - send_s, box.bottom - foot - 2, send_s, send_s, TRUE);
        const bool cue = get_window_text(ui->composer).empty();
        ShowWindow(ui->composer_cue, cue ? SW_SHOW : SW_HIDE);
        cx += ww;
    };
    auto hide_agent = [&]() {
        ShowWindow(ui->hdr_agent, SW_HIDE);
        ShowWindow(ui->chat_tabs, SW_HIDE);
        ShowWindow(ui->neu, SW_HIDE);
        ShowWindow(ui->agent_hint, SW_HIDE);
        ShowWindow(ui->add_file, SW_HIDE);
        ShowWindow(ui->workflow, SW_HIDE);
        ShowWindow(ui->ctx, SW_HIDE);
        ShowWindow(ui->transcript, SW_HIDE);
        ShowWindow(ui->chat_log, SW_HIDE);
        ShowWindow(ui->activity, SW_HIDE);
        ShowWindow(ui->composer, SW_HIDE);
        ShowWindow(ui->composer_panel, SW_HIDE);
        ShowWindow(ui->composer_cue, SW_HIDE);
        ShowWindow(ui->send, SW_HIDE);
        ShowWindow(ui->cancel, SW_HIDE);
        ShowWindow(ui->empty_agent, SW_HIDE);
        ui->composer_box = {};
    };
    auto place_history = [&](int ww) {
        ShowWindow(ui->hdr_history, SW_HIDE);
        ShowWindow(ui->scope, SW_HIDE);
        ShowWindow(ui->search, SW_SHOW);
        ShowWindow(ui->pin, SW_HIDE);
        ShowWindow(ui->archive, SW_HIDE);
        ShowWindow(ui->threads, SW_SHOW);
        const int breath = dip(ui->wnd, 5);
        MoveWindow(ui->search, cx + breath, body_y + breath, ww - breath * 2, filter_h, TRUE);
        center_single_line_edit(ui->search, ui->font);
        const int list_y = body_y + breath + filter_h + breath;
        MoveWindow(ui->threads, cx + breath, list_y, ww - breath * 2, body_h - (list_y - body_y) - breath, TRUE);
        MoveWindow(ui->empty_chats, cx + breath, list_y, ww - breath * 2, body_h - (list_y - body_y) - breath, TRUE);
        ShowWindow(ui->empty_chats, ui->thread_ids.empty() ? SW_SHOW : SW_HIDE);
        cx += ww;
    };
    auto hide_history = [&]() {
        ShowWindow(ui->hdr_history, SW_HIDE);
        ShowWindow(ui->scope, SW_HIDE);
        ShowWindow(ui->search, SW_HIDE);
        ShowWindow(ui->pin, SW_HIDE);
        ShowWindow(ui->archive, SW_HIDE);
        ShowWindow(ui->threads, SW_HIDE);
        ShowWindow(ui->empty_chats, SW_HIDE);
    };

    const int split_px = pxw(ui->panes.splitter);
    auto mark_split = [&](RECT& r) {
        r = {cx, body_y, cx + split_px, body_y + body_h};
        cx += split_px;
    };

    if (ui->panes.show_files) {
        place_files(pxw(ui->panes.files));
        if (ui->panes.show_editor || ui->panes.show_agent || ui->panes.show_history) {
            mark_split(ui->split1);
        }
    } else {
        hide_files();
    }
    if (ui->panes.show_editor) {
        place_editor(pxw(ui->panes.editor));
        if (ui->panes.show_agent || ui->panes.show_history) {
            mark_split(ui->split2);
        }
    } else {
        hide_editor();
    }
    if (ui->panes.show_agent) {
        place_agent(pxw(ui->panes.agent));
        if (ui->panes.show_history) {
            mark_split(ui->split3);
        }
    } else {
        hide_agent();
    }
    if (ui->panes.show_history) {
        place_history(pxw(ui->panes.history));
    } else {
        hide_history();
    }

    if (term_band > 0) {
        ensure_terminal_panel(ui);
        const int term_y = h - st - term_band;
        // This gap is outside every child HWND, so mouse input reaches the frame.
        ui->split_term = RECT{pad, term_y - term_splitter, w - pad, term_y};
        RECT panel_rc{pad, term_y, w - pad, h - st};
        const RECT content = ui->workbench_panel.layout(panel_rc);
        ui->terminal_sessions.set_panel_visible(
            !ui->panel_collapsed && ui->workbench_panel.surface() == PanelSurface::Terminal);
        ui->terminal_sessions.reparent_hosts(terminal_host_parent(ui));
        const int cw = content.right - content.left;
        const int ch = content.bottom - content.top;
        if (cw > 0 && ch > 0) {
            ui->terminal_sessions.layout_active(0, 0, cw, ch);
        }
        ui->workbench_panel.refresh_session_tabs(ui->terminal_sessions);
    } else {
        ui->split_term = {};
        ui->workbench_panel.set_visible(false);
        ui->terminal_sessions.set_panel_visible(false);
    }

    // Status strip: message text on the left, clickable Environment chip pinned right. The chip
    // only exists while a project is open, since the environment is per project.
    int status_w = w - pad * 2;
    if (ui->status_env) {
        const bool show_env = !ui->session.store.active_project_id.empty();
        ShowWindow(ui->status_env, show_env ? SW_SHOW : SW_HIDE);
        if (show_env) {
            const int env_w = dip(ui->wnd, 168);
            MoveWindow(ui->status_env, w - pad - env_w, h - st + 2, env_w, st - 4, TRUE);
            status_w -= env_w + dip(ui->wnd, 8);
        }
    }
    MoveWindow(ui->status, pad, h - st + 2, (std::max)(1, status_w), st - 4, TRUE);
    ShowWindow(ui->homehint, SW_HIDE);
    // Never RDW_UPDATENOW here: sync paint while a panel BUTTON is still in BN_CLICKED (or a
    // session-tab LBUTTONUP still on the stack) has crashed after hosts were parented under content_.
    RedrawWindow(ui->wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    refresh_thin_scrollbar(ui->tree);
    refresh_thin_scrollbar(ui->threads);
    refresh_thin_scrollbar(ui->ctx);
    refresh_thin_scrollbar(ui->editor);
    refresh_thin_scrollbar(ui->composer);
}

void refresh_models(Ui* ui) {
    if (ui->sel_list && ui->sel_owner == ID_MODELS) {
        return;  // keep face + popup stable while choosing
    }
    std::wstring label = L"Model";
    for (const auto& m : ui->session.models) {
        if (m.id == ui->session.selected_model &&
            m.provider_id == ui->session.settings.default_provider) {
            label = model_choice_label(m);
            break;
        }
    }
    if (label == L"Model") {
        for (const auto& m : ui->session.models) {
            if (m.provider_id == ui->session.settings.default_provider &&
                ui->session.is_model_enabled(m.provider_id, m.id)) {
                label = model_choice_label(m);
                break;
            }
        }
    }
    if (label == L"Model" && !ui->session.selected_model.empty()) {
        for (const auto& m : ui->session.models) {
            if (m.id == ui->session.selected_model) {
                label = model_choice_label(m);
                break;
            }
        }
    }
    SetWindowTextW(ui->models, label.c_str());
    InvalidateRect(ui->models, nullptr, TRUE);
}

void open_chat_index(Ui* ui, int index) {
    if (!ui || index < 0 || index >= static_cast<int>(ui->thread_ids.size())) {
        return;
    }
    ui->restore_chat_pending = false;
    close_attachment_windows(ui);
    ui->message_attachments.clear();
    const std::string tid = ui->thread_ids[index];
    if (tid == ui->session.active_thread_id) {
        SendMessageW(ui->threads, LB_SETCURSEL, index, 0);
        if (ui->chat_tabs) {
            TabCtrl_SetCurSel(ui->chat_tabs, index);
        }
        return;
    }
    if (auto* c = ui->session.store.by_thread(tid)) {
        if (!c->project_id.empty() && c->project_id != ui->session.store.active_project_id) {
            apply_project(ui, c->project_id);
        }
    }
    ui->session.set_draft(ui->session.active_thread_id, utf8(get_window_text(ui->composer)));
    ui->session.open_thread(tid);
    if (ui->session.transcript_replace) apply_stream(ui);
    SetWindowTextW(ui->composer, utf16(ui->session.draft_for(tid)).c_str());
    ui->shown_stream.clear();
    ui->markdown_stream_start = -1;
    SendMessageW(ui->threads, LB_SETCURSEL, index, 0);
    if (ui->chat_tabs) {
        TabCtrl_SetCurSel(ui->chat_tabs, index);
    }
    refresh_chrome(ui);
    layout(ui);
}

void refresh_threads(Ui* ui) {
    SendMessageW(ui->threads, WM_SETREDRAW, FALSE, 0);
    SendMessageW(ui->threads, LB_RESETCONTENT, 0, 0);
    ui->thread_ids.clear();
    ui->chat_groups.clear();
    std::wstring q = get_window_text(ui->search);
    const auto vis = ui->session.store.list_visible(ui->session.account_scope(), q);
    int sel = 0;
    int shown = 0;
    std::wstring previous_group;
    for (Conversation* c : vis) {
        const auto group = c->pinned ? std::wstring(L"Pinned") : chat_date_group(c->updated_at);
        ui->chat_groups.push_back(group == previous_group ? L"" : group);
        previous_group = group;
        std::wstring name = utf16(c->title.empty() ? "New Chat" : c->title);
        if (c->pinned) {
            name = L"★ " + name;
        }
        SendMessageW(ui->threads, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        SendMessageW(ui->threads, LB_SETITEMHEIGHT, shown, dip(ui->wnd, ui->chat_groups.back().empty() ? 40 : 66));
        ui->thread_ids.push_back(c->thread_id);
        if (c->thread_id == ui->session.active_thread_id) {
            sel = shown;
        }
        ++shown;
    }
    if (!ui->thread_ids.empty()) {
        SendMessageW(ui->threads, LB_SETCURSEL, sel, 0);
    }
    refresh_chat_tabs(ui);
    ShowWindow(ui->empty_chats, ui->thread_ids.empty() ? SW_SHOW : SW_HIDE);
    SendMessageW(ui->threads, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ui->threads, nullptr, FALSE);
}

void refresh_chrome(Ui* ui) {
    const auto activity_text = utf16(ui->session.activity.summary());
    if (activity_text != ui->activity_text) {
        const bool visibility_changed = activity_text.empty() != ui->activity_text.empty();
        ui->activity_text = activity_text;
        const auto first_line = SendMessageW(ui->activity, EM_GETFIRSTVISIBLELINE, 0, 0);
        DWORD selection_start = 0, selection_end = 0;
        SendMessageW(ui->activity, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start),
                     reinterpret_cast<LPARAM>(&selection_end));
        SetWindowTextW(ui->activity, activity_text.c_str());
        SendMessageW(ui->activity, EM_SETSEL, selection_start, selection_end);
        SendMessageW(ui->activity, EM_LINESCROLL, 0, first_line);
        if (visibility_changed) layout(ui);
    }
    // Any open header popup (project / models / scope) must stay frozen — chrome refreshes
    // SetWindowText + RedrawWindow(UPDATENOW) on every Codex line and steal clicks / blink.
    if (ui->sel_list) {
        return;
    }
    const ProviderId def = provider_id_from_string(ui->session.settings.default_provider);
    std::wstring acc = L"Account";
    if (def == ProviderId::Claude) {
        const auto cl = claude_provider_status();
        acc = cl.connected ? L"Claude · connected" : L"Claude · not connected";
    } else if (def == ProviderId::ClaudeApi) {
        acc = claude_api_connected() ? L"Claude API · connected" : L"Claude API · not connected";
    } else if (def == ProviderId::OpenAiApi) {
        acc = openai_api_key_present() ? L"OpenAI API · connected" : L"OpenAI API · not connected";
    } else if (ui->session.account.signed_in) {
        acc = utf16(ui->session.account.email);
        if (acc.size() > 28) {
            acc = acc.substr(0, 26) + L"…";
        }
    }
    SetWindowTextW(ui->account, acc.c_str());
    refresh_projects(ui);
    std::wstring st = state_label(ui->session.state);
    if (ui->session.state == AppState::Ready) {
        st = L"Connected";
    }
    if (def == ProviderId::Claude) {
        st = claude_account_connected() ? L"Claude connected" : L"Claude not connected";
        if (ui->session.state == AppState::Generating) {
            st = L"Claude generating…";
        } else if (ui->session.state == AppState::Ready) {
            st = L"Ready · Claude Account";
        }
    } else if (def == ProviderId::ClaudeApi) {
        st = claude_api_connected() ? L"Claude API connected" : L"Claude API not connected";
        if (ui->session.state == AppState::Generating) {
            st = L"Claude API generating…";
        } else if (ui->session.state == AppState::Ready) {
            st = L"Ready · Claude API";
        }
    } else if (def == ProviderId::OpenAiApi) {
        st = openai_api_key_present() ? L"OpenAI API connected" : L"OpenAI API not connected";
        if (ui->session.state == AppState::Generating) {
            st = L"OpenAI API generating…";
        } else if (ui->session.state == AppState::Ready) {
            st = L"Ready · OpenAI API";
        }
    } else if (ui->session.has_project_grant()) {
        st += L"  ·  browse/edit ";
        st += folder_name(ui->session.conversation_cwd());
    } else {
        st += L"  ·  read-only";
    }
    {
        const std::string pid = ui->session.store.active_project_id;
        if (!pid.empty()) {
            st += L"  ·  Keyring: ";
            st += keyring_status_label(ui, pid);
        }
        // The environment now lives in its own clickable chip beside the Keyring text, so it is
        // not appended to |st| — a streaming status line would otherwise keep overwriting it.
        if (ui->status_env) {
            std::wstring env_label = L"Env: None";
            if (!pid.empty()) {
                const std::string eid = ui->environments.active_environment_id(pid);
                if (!eid.empty()) {
                    if (const auto* env = ui->environments.find_environment(pid, eid)) {
                        env_label = L"Env: " + utf16(env->name);
                    }
                }
            }
            SetWindowTextW(ui->status_env, env_label.c_str());
            InvalidateRect(ui->status_env, nullptr, TRUE);
        }
        if (const auto* sess = ui->terminal_sessions.active_session()) {
            if (sess->human_only) {
                st += L"  ·  Human Only";
            }
        }
        // Cached from the last Settings → Strata refresh. refresh_chrome runs on every
        // streamed line, so it must never call the bridge itself.
        const std::wstring& strata_seg = ui->strata_ui.chrome_summary();
        if (!strata_seg.empty()) {
            st += L"  ·  Strata: ";
            st += strata_seg;
        } else if (ui->strata_bridge.running()) {
            st += L"  ·  Strata: Connected";
        }
    }
    if (!ui->session.last_error.empty() &&
        (def == ProviderId::OpenAI || ui->session.state == AppState::Failed)) {
        st = utf16(ui->session.last_error);
    }
    if (ui->active_doc >= 0) {
        st += L"  ·  ";
        st += ui->docs[ui->active_doc].enc == TextEnc::Utf16Le ? L"UTF-16 LE" : L"UTF-8";
        if (ui->docs[ui->active_doc].crlf) {
            st += L"  CRLF";
        }
    }
    SetWindowTextW(ui->status, st.c_str());
    const bool generating = ui->session.state == AppState::Generating || ui->session.activity.busy;
    SetWindowTextW(ui->send, generating ? L"Stop" : L"Send");
    const bool has_enabled_model =
        !ui->session.models_for_provider(ui->session.settings.default_provider, true).empty();
    const bool provider_ready =
        (def == ProviderId::Claude && claude_account_connected()) ||
        (def == ProviderId::ClaudeApi && claude_api_connected() && has_enabled_model) ||
        (def == ProviderId::OpenAiApi && openai_api_key_present() && has_enabled_model) ||
        (def == ProviderId::OpenAI && ui->session.account.signed_in);
    const bool can_send = generating || (provider_can_send(def) && provider_ready);
    EnableWindow(ui->send, can_send ? TRUE : FALSE);
    ShowWindow(ui->cancel, SW_HIDE);
    ShowWindow(ui->signin, SW_HIDE);
    ShowWindow(ui->signout, SW_HIDE);
    InvalidateRect(ui->send, nullptr, TRUE);
    InvalidateRect(ui->toggle_files, nullptr, TRUE);
    InvalidateRect(ui->toggle_history, nullptr, TRUE);
    InvalidateRect(ui->focus, nullptr, TRUE);
    if (ui->empty_agent) {
        const bool empty = chat_log_empty(ui->chat_log);
        ShowWindow(ui->transcript, SW_HIDE);
        ShowWindow(ui->chat_log, empty ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_agent, empty ? SW_SHOW : SW_HIDE);
        // refresh_chrome runs on every streaming delta; a synchronous repaint here made the pane
        // flicker for the whole turn. Only repaint when the empty state actually flips.
        if (ui->chat_empty_state != (empty ? 1 : 0)) {
            ui->chat_empty_state = empty ? 1 : 0;
            RedrawWindow(ui->empty_agent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
    }
    if (ui->empty_chats) {
        ShowWindow(ui->empty_chats, ui->thread_ids.empty() ? SW_SHOW : SW_HIDE);
        RedrawWindow(ui->empty_chats, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
}

// Status-bar Environment chip: pick the active environment for the open project, or jump to the
// full page. Persists through the same manager as Security → Project Environments so the two
// surfaces cannot disagree.
void choose_status_environment(Ui* ui) {
    if (!ui || !ui->wnd) {
        return;
    }
    const std::string pid = ui->session.store.active_project_id;
    if (pid.empty()) {
        return;
    }
    const ProjectEnvironmentState* st = ui->environments.state_for(pid);
    const std::string active = ui->environments.active_environment_id(pid);

    constexpr UINT kIdNone = 1;
    constexpr UINT kIdManage = 2;
    constexpr UINT kIdFirstEnv = 100;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (active.empty() ? MF_CHECKED : 0), kIdNone, L"None");
    if (st && !st->environments.empty()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        UINT item = kIdFirstEnv;
        for (const auto& e : st->environments) {
            UINT flags = MF_STRING;
            if (e.id == active) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(m, flags, item++, utf16(e.name).c_str());
        }
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kIdManage, L"Manage Environments…");

    POINT p{};
    GetCursorPos(&p);
    const int chosen = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0,
                                      ui->wnd, nullptr);
    DestroyMenu(m);
    if (chosen == 0) {
        return;
    }
    if (chosen == static_cast<int>(kIdManage)) {
        open_settings_section(ui, SettingsSection::Security);
        set_security_subpage(ui, SecuritySubpage::Environments);
        return;
    }
    std::string next;
    if (chosen != static_cast<int>(kIdNone)) {
        const auto idx = static_cast<std::size_t>(chosen - static_cast<int>(kIdFirstEnv));
        if (!st || idx >= st->environments.size()) {
            return;
        }
        next = st->environments[idx].id;
    }
    if (next == active) {
        return;
    }
    if (!ui->environments.set_active(pid, next)) {
        return;
    }
    if (!ui->environments.save(ui->session.paths.environments_path)) {
        ui_kit::report_save_failure(ui->wnd, L"active environment", ui->session.paths.environments_path);
    }
    refresh_settings_data(ui);
    refresh_chrome(ui);
    layout(ui);
}

void apply_stream(Ui* ui) {
    const auto visible_stream = visible_chat_text(ui->session.stream_buffer);
    if (!ui->session.transcript_replace && visible_stream == ui->shown_stream) return;
    std::vector<ChatLogMessage> messages;
    messages.reserve(ui->session.history_messages.size() + 1);
    for (const auto& message : ui->session.history_messages) {
        ChatLogMessage row;
        row.user = message.user;
        if (message.user) {
            const auto display = parse_user_display(message.text);
            row.text = utf16(display.text);
            row.attachments = display.attachments;
        } else {
            row.text = utf16(visible_chat_text(message.text));
        }
        messages.push_back(std::move(row));
    }
    if (ui->session.active_thread_busy() && !visible_stream.empty()) {
        // A completing turn still reports busy for a moment after its final assistant message has
        // landed in history; appending the live row again showed the same reply twice.
        const auto live = utf16(visible_stream);
        const bool already_in_history =
            !messages.empty() && !messages.back().user && messages.back().text == live;
        if (!already_in_history) messages.push_back({false, live, {}});
    }
    chat_log_set_messages(ui->chat_log, std::move(messages));
    chat_log_scroll_bottom(ui->chat_log);
    ui->shown_stream = visible_stream;
    ui->session.transcript_replace = false;
}

void persist_window(Ui* ui) {
    ui->session.settings.restore_chat_on_start = IsWindowVisible(ui->threads) != FALSE;
    ui->session.settings.last_thread_id = ui->session.active_thread_id;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    GetWindowPlacement(ui->wnd, &wp);
    ui->session.settings.window.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    ui->session.settings.window.x = wp.rcNormalPosition.left;
    ui->session.settings.window.y = wp.rcNormalPosition.top;
    ui->session.settings.window.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    ui->session.settings.window.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    ui->session.set_draft(ui->session.active_thread_id, utf8(get_window_text(ui->composer)));
    persist_store(ui);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
}

void clamp_on_screen(int& x, int& y, int& w, int& h) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w < 860) {
        w = 860;
    }
    if (h < 520) {
        h = 520;
    }
    if (x < vx || x > vx + vw - 80) {
        x = vx + 80;
    }
    if (y < vy || y > vy + vh - 80) {
        y = vy + 80;
    }
}

void browse_codex(Ui* ui) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ui->wnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Codex (codex.exe)\0codex.exe\0Executables\0*.exe\0\0";
    ofn.lpstrTitle = L"Select official Codex executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        ui->session.settings.codex_path = file;
        save_settings(ui->session.paths.settings_path, ui->session.settings);
        std::wstring err;
        ui->session.stop_runtime();
        if (!ui->session.start_runtime(ui->wnd, WM_SCYLLA_LINE, &err)) {
            MessageBoxW(ui->wnd, err.c_str(), L"Scylla", MB_ICONERROR);
        }
    }
}

void layout(Ui* ui);

void clear_composer_images(Ui* ui) {
    if (!ui) {
        return;
    }
    for (auto& img : ui->images) {
        if (img.thumb) {
            DeleteObject(img.thumb);
            img.thumb = nullptr;
        }
    }
    ui->images.clear();
}

bool gdip_png_clsid(CLSID* clsid) {
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) {
        return false;
    }
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) {
        return false;
    }
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            *clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

HBITMAP make_thumb_bitmap(Gdiplus::Bitmap& src, int size) {
    if (src.GetLastStatus() != Gdiplus::Ok || size <= 0) {
        return nullptr;
    }
    Gdiplus::Bitmap dest(size, size, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(&dest);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    const UINT sw = src.GetWidth();
    const UINT sh = src.GetHeight();
    if (sw == 0 || sh == 0) {
        return nullptr;
    }
    const float scale = (std::min)(static_cast<float>(size) / sw, static_cast<float>(size) / sh);
    const int dw = static_cast<int>(sw * scale);
    const int dh = static_cast<int>(sh * scale);
    const int dx = (size - dw) / 2;
    const int dy = (size - dh) / 2;
    g.DrawImage(&src, dx, dy, dw, dh);
    HBITMAP hbmp = nullptr;
    if (dest.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbmp) != Gdiplus::Ok) {
        return nullptr;
    }
    return hbmp;
}

HBITMAP load_file_thumb(const std::wstring& path, int size) {
    Gdiplus::Bitmap src(path.c_str());
    return make_thumb_bitmap(src, size);
}

struct AttachmentPreviewState {
    Ui* ui = nullptr;
    std::vector<DisplayAttachment> attachments;
    std::vector<HBITMAP> thumbs;
    DisplayAttachment attachment;
    std::wstring text;
    std::unique_ptr<Gdiplus::Image> image;
    bool lightbox = false;
};

void close_attachment_windows(Ui* ui) {
    if (!ui) return;
    if (IsWindow(ui->attachment_tray)) DestroyWindow(ui->attachment_tray);
    if (IsWindow(ui->attachment_lightbox)) DestroyWindow(ui->attachment_lightbox);
    ui->attachment_tray = ui->attachment_lightbox = nullptr;
}

LRESULT CALLBACK attachment_preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<AttachmentPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        state = reinterpret_cast<AttachmentPreviewState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_KILLFOCUS && !state->lightbox) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_LBUTTONDOWN) {
        if (state->lightbox) { DestroyWindow(hwnd); return 0; }
        const int tile = MulDiv(72, GetDpiForWindow(hwnd), 96);
        const int gap = MulDiv(10, GetDpiForWindow(hwnd), 96);
        const int index = GET_X_LPARAM(lp) / (tile + gap);
        if (index >= 0 && index < static_cast<int>(state->attachments.size())) {
            DisplayAttachment selected = state->attachments[index];
            auto* preview = new AttachmentPreviewState{};
            preview->ui = state->ui; preview->attachment = selected; preview->lightbox = true;
            const auto selected_path = utf16(selected.path);
            bool readable = GetFileAttributesW(selected_path.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (const auto* source = state->ui->knowledge.source_for_path(selected_path, state->ui->session.store.active_project_id))
                readable = readable && state->ui->knowledge.resolve_effective_access(source->id, selected_path).readable;
            if (readable) {
                preview->image = std::make_unique<Gdiplus::Image>(selected_path.c_str());
                if (preview->image->GetLastStatus() != Gdiplus::Ok) {
                    preview->image.reset();
                    const auto loaded = load_text_file(selected_path, 256 * 1024);
                    if (!loaded.binary && loaded.error.empty()) preview->text = loaded.text;
                }
            }
            RECT owner{}; GetClientRect(state->ui->wnd, &owner);
            const int inset = MulDiv(36, GetDpiForWindow(state->ui->wnd), 96);
            POINT origin{owner.left + inset, owner.top + inset}; ClientToScreen(state->ui->wnd, &origin);
            state->ui->attachment_lightbox = CreateWindowExW(WS_EX_TOOLWINDOW, L"ScyllaAttachmentPreview", L"Attachment preview",
                WS_POPUP | WS_BORDER, origin.x, origin.y, owner.right - inset * 2, owner.bottom - inset * 2,
                state->ui->wnd, nullptr, GetModuleHandleW(nullptr), preview);
            ShowWindow(state->ui->attachment_lightbox, SW_SHOW); SetFocus(state->ui->attachment_lightbox);
        }
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        fill_rect(dc, r, state->lightbox ? theme().overlay : theme().surface);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, theme().text);
        if (state->lightbox) {
            RECT title{20, 12, r.right - 48, 40};
            DrawTextW(dc, utf16(state->attachment.label).c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT close{r.right - 42, 8, r.right - 8, 42}; DrawTextW(dc, L"×", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT body{24, 52, r.right - 24, r.bottom - 24};
            if (state->image) {
                Gdiplus::Graphics g(dc); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                const double scale = (std::min)(static_cast<double>(body.right - body.left) / state->image->GetWidth(),
                                                static_cast<double>(body.bottom - body.top) / state->image->GetHeight());
                const int w = static_cast<int>(state->image->GetWidth() * scale), h = static_cast<int>(state->image->GetHeight() * scale);
                g.DrawImage(state->image.get(), body.left + (body.right - body.left - w) / 2,
                            body.top + (body.bottom - body.top - h) / 2, w, h);
            } else if (!state->text.empty()) {
                DrawTextW(dc, state->text.c_str(), static_cast<int>((std::min<std::size_t>)(state->text.size(), 12000)), &body,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
            } else {
                DrawTextW(dc, L"Preview unavailable\n\nThe attachment may have moved or uses a binary format Scylla cannot preview.",
                          -1, &body, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            }
        } else {
            const int tile = MulDiv(72, GetDpiForWindow(hwnd), 96), gap = MulDiv(10, GetDpiForWindow(hwnd), 96);
            for (std::size_t i = 0; i < state->attachments.size(); ++i) {
                RECT box{static_cast<LONG>(i * (tile + gap)), 0, static_cast<LONG>(i * (tile + gap) + tile), tile};
                fill_rect(dc, box, theme().input);
                if (i < state->thumbs.size() && state->thumbs[i]) {
                    HDC mem = CreateCompatibleDC(dc); auto old = SelectObject(mem, state->thumbs[i]);
                    BITMAP bm{}; GetObject(state->thumbs[i], sizeof(bm), &bm);
                    StretchBlt(dc, box.left + 4, box.top + 4, tile - 8, tile - 8, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                    SelectObject(mem, old); DeleteDC(mem);
                } else {
                    RECT icon = box; icon.bottom -= 20;
                    DrawTextW(dc, L"FILE", -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    RECT name = box; name.top = box.bottom - 20;
                    DrawTextW(dc, utf16(state->attachments[i].label).c_str(), -1, &name, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
        }
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_NCDESTROY) {
        if (state->ui) {
            if (state->ui->attachment_tray == hwnd) state->ui->attachment_tray = nullptr;
            if (state->ui->attachment_lightbox == hwnd) state->ui->attachment_lightbox = nullptr;
        }
        for (auto bitmap : state->thumbs) if (bitmap) DeleteObject(bitmap);
        delete state; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensure_attachment_preview_class() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{}; wc.lpfnWndProc = attachment_preview_proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND); wc.lpszClassName = L"ScyllaAttachmentPreview";
    wc.hbrBackground = nullptr; RegisterClassW(&wc); registered = true;
}

void append_attachment_footer(Ui* ui, const std::vector<DisplayAttachment>& attachments) {
    if (!ui || attachments.empty()) return;
    const std::wstring key = utf16(make_uuid());
    ui->message_attachments[key] = attachments;
    // Keep the whole compact footer as one left-aligned hit target. The renderer
    // gives this attachment link a filled background, matching the file-browser
    // CTA and allowing the thumbnail tray to open from either the plus or label.
    std::wstring label = L" +   (" + std::to_wstring(attachments.size()) + L") Attachments";
    RECT client{};
    GetClientRect(ui->transcript, &client);
    HDC dc = GetDC(ui->transcript);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(ui->transcript, WM_GETFONT, 0, 0));
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(ui->transcript, dc);
    const int char_width = (std::max)(1, static_cast<int>(metrics.tmAveCharWidth));
    const int fill = (std::max)(
        0, static_cast<int>((client.right - client.left) / char_width) - static_cast<int>(label.size()) - 2);
    label.append(static_cast<std::size_t>(fill), L' ');
    ui_kit::append_markdown(ui->transcript, L"[" + label + L"](scylla-attachments:" + key + L")\n");
}

void show_attachment_tray(Ui* ui, const std::wstring& key, HWND source, long position) {
    const auto found = ui->message_attachments.find(key);
    if (found == ui->message_attachments.end() || found->second.empty()) return;
    if (IsWindow(ui->attachment_tray)) { DestroyWindow(ui->attachment_tray); return; }
    ensure_attachment_preview_class();
    auto* state = new AttachmentPreviewState{}; state->ui = ui; state->attachments = found->second;
    const int tile = dip(ui->wnd, 72), gap = dip(ui->wnd, 10);
    for (const auto& attachment : state->attachments)
        state->thumbs.push_back(attachment.image ? load_file_thumb(utf16(attachment.path), tile - 8) : nullptr);
    POINT screen{};
    if (source == ui->chat_log) {
        RECT source_rect{};
        GetWindowRect(source, &source_rect);
        screen = {source_rect.left + dip(ui->wnd, 12), source_rect.top + dip(ui->wnd, 24)};
    } else {
        POINTL point{};
        SendMessageW(source, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), position);
        screen = {point.x, point.y + dip(ui->wnd, 24)};
        ClientToScreen(source, &screen);
    }
    const int width = (std::min)(static_cast<int>(state->attachments.size()), 6) * (tile + gap) - gap;
    HMONITOR monitor = MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    screen.x = (std::max)(info.rcWork.left, (std::min)(screen.x, info.rcWork.right - (std::max)(tile, width)));
    screen.y = (std::max)(info.rcWork.top, (std::min)(screen.y, info.rcWork.bottom - tile));
    ui->attachment_tray = CreateWindowExW(WS_EX_TOOLWINDOW, L"ScyllaAttachmentPreview", L"Attachments",
        WS_POPUP | WS_BORDER, screen.x, screen.y, (std::max)(tile, width), tile,
        ui->wnd, nullptr, GetModuleHandleW(nullptr), state);
    ShowWindow(ui->attachment_tray, SW_SHOW); SetFocus(ui->attachment_tray);
}

bool clipboard_has_image() {
    const UINT png = RegisterClipboardFormatW(L"PNG");
    return IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB) ||
           (png != 0 && IsClipboardFormatAvailable(png));
}

bool write_bytes(const std::wstring& path, const void* data, SIZE_T n) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(f, data, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(f);
    return ok && written == n;
}

bool try_paste_composer_image(Ui* ui) {
    if (!ui || !clipboard_has_image() || ui->images.size() >= 8) {
        return false;
    }
    if (!OpenClipboard(ui->wnd)) {
        return false;
    }
    std::wstring path;
    bool saved = false;
    const UINT png_fmt = RegisterClipboardFormatW(L"PNG");
    if (png_fmt && IsClipboardFormatAvailable(png_fmt)) {
        HANDLE h = GetClipboardData(png_fmt);
        if (h) {
            const SIZE_T n = GlobalSize(h);
            void* p = GlobalLock(h);
            if (p && n > 0) {
                path = join_path(ui->session.paths.attachments_dir, utf16(make_uuid()) + L".png");
                saved = write_bytes(path, p, n);
            }
            if (p) {
                GlobalUnlock(h);
            }
        }
    }
    if (!saved && IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hb = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (hb) {
            Gdiplus::Bitmap bmp(hb, nullptr);
            CLSID enc{};
            if (bmp.GetLastStatus() == Gdiplus::Ok && gdip_png_clsid(&enc)) {
                path = join_path(ui->session.paths.attachments_dir, utf16(make_uuid()) + L".png");
                saved = (bmp.Save(path.c_str(), &enc) == Gdiplus::Ok);
            }
        }
    }
    if (!saved && IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE h = GetClipboardData(CF_DIB);
        if (h) {
            auto* bi = static_cast<BITMAPINFO*>(GlobalLock(h));
            if (bi) {
                void* bits = reinterpret_cast<BYTE*>(bi) + bi->bmiHeader.biSize +
                             (bi->bmiHeader.biBitCount <= 8 ? (1 << bi->bmiHeader.biBitCount) * sizeof(RGBQUAD) : 0);
                HDC screen = GetDC(nullptr);
                HBITMAP hb = CreateDIBitmap(screen, &bi->bmiHeader, CBM_INIT, bits, bi, DIB_RGB_COLORS);
                ReleaseDC(nullptr, screen);
                GlobalUnlock(h);
                if (hb) {
                    Gdiplus::Bitmap bmp(hb, nullptr);
                    CLSID enc{};
                    if (bmp.GetLastStatus() == Gdiplus::Ok && gdip_png_clsid(&enc)) {
                        path = join_path(ui->session.paths.attachments_dir, utf16(make_uuid()) + L".png");
                        saved = (bmp.Save(path.c_str(), &enc) == Gdiplus::Ok);
                    }
                    DeleteObject(hb);
                }
            }
        }
    }
    CloseClipboard();
    if (!saved || path.empty()) {
        return false;
    }
    AttachedImage img;
    img.id = make_uuid();
    img.path = path;
    img.thumb = load_file_thumb(path, dip(ui->wnd, 48));
    ui->images.push_back(std::move(img));
    layout(ui);
    InvalidateRect(ui->composer_panel, nullptr, TRUE);
    return true;
}

int hit_thumb_close(Ui* ui, int x, int y) {
    if (!ui || ui->images.empty()) {
        return -1;
    }
    const int thumb = dip(ui->wnd, 48);
    const int gap = dip(ui->wnd, 8);
    const int left0 = dip(ui->wnd, 12);
    const int top = dip(ui->wnd, 8);
    int left = left0;
    for (size_t i = 0; i < ui->images.size(); ++i) {
        RECT close{left + thumb - 14, top, left + thumb, top + 14};
        POINT pt{x, y};
        if (PtInRect(&close, pt)) {
            return static_cast<int>(i);
        }
        left += thumb + gap;
    }
    return -1;
}

void remove_composer_image(Ui* ui, int index) {
    if (!ui || index < 0 || index >= static_cast<int>(ui->images.size())) {
        return;
    }
    if (ui->images[index].thumb) {
        DeleteObject(ui->images[index].thumb);
    }
    ui->images.erase(ui->images.begin() + index);
    layout(ui);
    InvalidateRect(ui->composer_panel, nullptr, TRUE);
}

LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    if (!ui || !ui->panel_prev) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_LBUTTONDOWN) {
        const int idx = hit_thumb_close(ui, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (idx >= 0) {
            remove_composer_image(ui, idx);
            return 0;
        }
    }
    return CallWindowProcW(ui->panel_prev, hwnd, msg, wparam, lparam);
}

void do_send(Ui* ui) {
    if (ui->session.active_thread_busy() || ui->session.claude_generating()) return;
    if (ui->ime_composing) {
        return;
    }
    const ProviderId def = provider_id_from_string(ui->session.settings.default_provider);
    if (!provider_can_send(def)) {
        if (def == ProviderId::Claude) {
            SetWindowTextW(ui->status, L"Connect Claude Account under Settings → Agent Providers");
        } else if (def == ProviderId::ClaudeApi) {
            SetWindowTextW(ui->status, L"Connect Claude API key under Settings → Agent Providers");
        } else if (def == ProviderId::OpenAiApi) {
            SetWindowTextW(ui->status, L"Connect OpenAI API key under Settings → Agent Providers");
        } else {
            SetWindowTextW(ui->status, L"Sign in with ChatGPT to send");
        }
        return;
    }
    if (def == ProviderId::OpenAI && !ui->session.account.signed_in) {
        SetWindowTextW(ui->status, L"Sign in with ChatGPT to send");
        return;
    }
    if ((def == ProviderId::OpenAiApi || def == ProviderId::ClaudeApi) &&
        ui->session.models_for_provider(ui->session.settings.default_provider, true).empty()) {
        SetWindowTextW(ui->status, L"Enable at least one API model under Settings → Agent Providers");
        return;
    }
    const auto mode = static_cast<WorkflowMode>(ui_kit::select_get_data(ui->workflow));
    const auto plan_directory = mode == WorkflowMode::Plan
        ? workflow_plan_directory(ui->knowledge, ui->session.store.active_project_id, ui->session.project_root)
        : std::wstring{};
    if (mode == WorkflowMode::Plan && plan_directory.empty()) {
        SetWindowTextW(ui->status, L"Open a project or configure a writable Knowledge folder to save a plan.");
        return;
    }
    std::wstring text = get_window_text(ui->composer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    if (text.empty() && ui->images.empty()) {
        return;
    }
    const bool scylla_query = consume_scylla_query_slash(&text);
    std::vector<DisplayAttachment> display_attachments;
    for (const auto& chip : ui->session.context_chips) {
        if (!chip.path.empty()) display_attachments.push_back({utf8(chip.path), chip.label, chip.kind == "image"});
    }
    for (const auto& img : ui->images) {
        ContextChip c;
        c.kind = "image";
        c.path = img.path;
        const auto slash = img.path.find_last_of(L"\\/");
        c.label = utf8(slash == std::wstring::npos ? img.path : img.path.substr(slash + 1));
        c.body = utf8(img.path);
        display_attachments.push_back({utf8(img.path), c.label, true});
        add_chip(ui, std::move(c));
    }
    clear_composer_images(ui);
    const std::wstring display = text.empty() ? L"(image attachment)" : text;
    ui->agent_heading_pending = true;
    ui->session.stream_buffer.clear();
    ui->shown_stream.clear();
    ui->markdown_stream_start = -1;
    const auto* active_project = ui->session.store.active();
    const auto sources = agent_file_catalog(ui->knowledge, ui->session.store.active_project_id,
                                            ui->session.project_root,
                                            active_project ? active_project->roots : std::vector<std::wstring>{});
    ui->session.chat_title_seed = utf8(display);
    ui->restore_chat_pending = false;
    std::string skill;
    if (scylla_query) {
        const std::string catalog = format_keyring_name_catalog(
            &ui->keyring_ui.keyring(), &ui->environments, ui->session.store.active_project_id);
        skill = scylla_query_instructions(catalog, broker_query_tool_available(ui));
    } else if (composer_text_has_bang_keyring_token(text)) {
        skill = keyring_bang_token_instructions();
    }
    ui->session.send_user(user_display_metadata(utf8(display), display_attachments) +
                          workflow_source_manifest(sources, ui->session.project_root, utf8(display),
                                                   active_project ? active_project->roots
                                                                  : std::vector<std::wstring>{}) +
                          workflow_instructions(mode, plan_directory,
                                                utf8(folder_name(ui->session.project_root))) +
                          skill + utf8(text.empty() ? display : text));
    ui->session.transcript_replace = true;
    apply_stream(ui);
    SetWindowTextW(ui->composer, L"");
    ui->session.set_draft(ui->session.active_thread_id, "");
    refresh_chrome(ui);
    layout(ui);
}

void do_open_folder(Ui* ui) {
    std::vector<std::wstring> folders;
    if (!pick_folders(ui->wnd, folders)) {
        return;
    }
    const std::wstring& folder = folders.front();
    if (_wcsicmp(folder.c_str(), ui->session.project_root.c_str()) != 0 &&
        !confirm_project_switch_with_live_terminals(ui)) {
        return;
    }
    persist_store(ui);
    auto* p = ui->session.store.open_or_create(folder);
    if (!p) {
        return;
    }
    ui->keyring_ui.on_project_switching(p->id, p->name);
    ui->session.project_root = p->root;
    ui->session.settings.project_folder = p->root;
    for (std::size_t i = 1; i < folders.size(); ++i) ui->session.store.add_root(p->id, folders[i]);
    persist_store(ui);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    ui->session.sync_lockdown_config();
    rebuild_tree(ui);
    refresh_projects(ui);
    refresh_threads(ui);
    refresh_chrome(ui);
    ui->keyring_ui.ensure_app_vault_bound();
    ui->keyring_ui.set_active_project(p->id, p->name);
    if (ui->content_view == ContentView::Keyring) {
        ui->keyring_ui.show(true);
        ui->keyring_ui.maybe_prompt_migration(ui->wnd);
    }
    layout(ui);
}

void do_add_authorized_folders(Ui* ui) {
    std::vector<std::wstring> folders;
    if (!pick_folders(ui->wnd, folders)) return;
    bool changed = false;
    auto* project = ui->session.store.active();
    if (!project) {
        project = ui->session.store.open_or_create(folders.front());
        if (!project) return;
        ui->session.project_root = project->root;
        ui->session.settings.project_folder = project->root;
        folders.erase(folders.begin());
        changed = true;
    }
    for (const auto& folder : folders) changed = ui->session.store.add_root(project->id, folder) || changed;
    if (!changed) return;
    persist_store(ui);
    ui->session.sync_lockdown_config();
    rebuild_tree(ui);
    if (ui->content_view == ContentView::Access) populate_content_body(ui);
    refresh_projects(ui);
    refresh_chrome(ui);
    layout(ui);
}

void toggle_mode(int& mode, bool currently_shown) {
    if (currently_shown) {
        mode = 2;
    } else {
        mode = 1;
    }
}

void apply_mcp_alias_popup(Ui* ui);
void close_mcp_alias_popup(Ui* ui);
void update_mcp_alias_popup(Ui* ui);
std::wstring mention_composer_text(HWND composer);
void refresh_composer_token_styles(Ui* ui);

void refresh_composer_token_styles(Ui* ui) {
    if (!ui || !ui->composer) return;
    const auto text = mention_composer_text(ui->composer);
    CHARRANGE caret{};
    SendMessageW(ui->composer, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&caret));
    CHARRANGE all{0, -1};
    SendMessageW(ui->composer, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_COLOR | CFM_BOLD | CFM_UNDERLINE | CFM_LINK;
    base.crTextColor = kText;
    base.dwEffects = 0;
    SendMessageW(ui->composer, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&base));
    for (const auto& span : find_scylla_query_spans(text)) {
        CHARRANGE range{static_cast<LONG>(span.begin), static_cast<LONG>(span.end)};
        SendMessageW(ui->composer, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W amber{};
        amber.cbSize = sizeof(amber);
        amber.dwMask = CFM_COLOR | CFM_BOLD;
        amber.crTextColor = theme().amber;
        amber.dwEffects = CFE_BOLD;
        SendMessageW(ui->composer, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&amber));
    }
    SendMessageW(ui->composer, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&caret));
}

LRESULT CALLBACK composer_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    if (!ui || !ui->composer_prev) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_IME_STARTCOMPOSITION) {
        ui->ime_composing = true;
    }
    if (msg == WM_IME_ENDCOMPOSITION) {
        ui->ime_composing = false;
    }
    if (msg == WM_PASTE) {
        if (try_paste_composer_image(ui)) {
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && wparam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (GetKeyState(VK_MENU) & 0x8000) == 0) {
        if (clipboard_has_image()) {
            try_paste_composer_image(ui);
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && ui->mcp_alias_popup && !ui->ime_composing) {
        if (wparam == VK_UP || wparam == VK_DOWN) {
            const int current = static_cast<int>(SendMessageW(ui->mcp_alias_popup, LB_GETCURSEL, 0, 0));
            const int count = static_cast<int>(SendMessageW(ui->mcp_alias_popup, LB_GETCOUNT, 0, 0));
            if (count > 0) SendMessageW(ui->mcp_alias_popup, LB_SETCURSEL, (current + (wparam == VK_DOWN ? 1 : count - 1)) % count, 0);
            return 0;
        }
        if (wparam == VK_ESCAPE) { close_mcp_alias_popup(ui); return 0; }
        if ((wparam == VK_RETURN || wparam == VK_TAB) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            ui->suppress_submit_char = true;
            apply_mcp_alias_popup(ui);
            return 0;
        }
    }    if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool send_now = composer_should_submit(ctrl, shift, alt, ui->ime_composing);
        if (send_now && !ui->ime_composing) {
            ui->suppress_submit_char = true;
            if (!(lparam & (1LL << 30))) do_send(ui);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wparam == L'\r' || wparam == L'\n' || wparam == L'\t') && ui->suppress_submit_char) {
        ui->suppress_submit_char = false;
        return 0;
    }
    if (msg == WM_KEYUP && (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_HOME || wparam == VK_END))
        update_mcp_alias_popup(ui);
    if (msg == WM_KILLFOCUS && reinterpret_cast<HWND>(wparam) != ui->mcp_alias_popup)
        close_mcp_alias_popup(ui);
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    return CallWindowProcW(ui->composer_prev, hwnd, msg, wparam, lparam);
}

void close_mcp_alias_popup(Ui* ui) {
    if (!ui || !ui->mcp_alias_popup) {
        return;
    }
    DestroyWindow(ui->mcp_alias_popup);
    ui->mcp_alias_popup = nullptr;
    ui->mcp_alias_completions.clear();
}

std::wstring mention_composer_text(HWND composer) {
    const int length = GetWindowTextLengthW(composer);
    std::wstring value(length + 1, L'\0');
    GETTEXTEX request{static_cast<DWORD>(value.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
    const auto copied = SendMessageW(composer, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request), reinterpret_cast<LPARAM>(value.data()));
    value.resize(static_cast<std::size_t>((std::max)(LRESULT(0), copied)));
    return value;
}

void apply_mcp_alias_popup(Ui* ui) {
    if (!ui || !ui->mcp_alias_popup) return;
    const int selected = static_cast<int>(SendMessageW(ui->mcp_alias_popup, LB_GETCURSEL, 0, 0));
    if (selected < 0 || selected >= static_cast<int>(ui->mcp_alias_completions.size())) return;
    if (ui->mcp_alias_completions[selected].empty()) return;  // hint row, nothing to insert
    const auto completion = utf16(ui->mcp_alias_completions[selected]);
    const auto text = mention_composer_text(ui->composer);
    CHARRANGE range{};
    SendMessageW(ui->composer, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    std::size_t start = std::min(text.size(), static_cast<std::size_t>((std::max)(0L, range.cpMin)));
    while (start > 0 && !iswspace(text[start - 1])) --start;
    if (start >= text.size()) return;
    const wchar_t lead = text[start];
    if (lead != L'@' && lead != L'/' && lead != L'!') return;
    range.cpMin = static_cast<LONG>(start);
    close_mcp_alias_popup(ui);
    SetFocus(ui->composer);
    SendMessageW(ui->composer, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    const auto replacement = completion + L" ";
    SendMessageW(ui->composer, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
    refresh_composer_token_styles(ui);
}

void update_mcp_alias_popup(Ui* ui) {
    if (!ui || !ui->composer || !ui->wnd) {
        return;
    }
    const auto text = mention_composer_text(ui->composer);
    CHARRANGE range{};
    SendMessageW(ui->composer, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    if (range.cpMin != range.cpMax) { close_mcp_alias_popup(ui); return; }
    const auto end = std::min(text.size(), static_cast<std::size_t>((std::max)(0L, range.cpMin)));
    std::size_t i = end;
    while (i > 0 && !iswspace(text[i - 1])) --i;
    const auto token = text.substr(i, end - i);
    if (token.empty() || (token[0] != L'@' && token[0] != L'/' && token[0] != L'!')) {
        close_mcp_alias_popup(ui);
        return;
    }

    std::vector<std::pair<std::string, std::wstring>> comps;
    if (token[0] == L'/') {
        if (const auto skill = slash_skill_completion_for_token(token); !skill.empty()) {
            comps.emplace_back(utf8(skill), L"Slash skill");
        }
    } else if (token[0] == L'!') {
        auto& keyring = ui->keyring_ui.keyring();
        if (keyring.vault_bound() && keyring.is_unlocked()) {
            comps = filter_keyring_bang_completions(keyring.list_refs_for_ui(ui->session.store.active_project_id),
                                                    token);
        } else if (keyring.vault_bound()) {
            // Empty insert text marks a non-insertable hint row.
            comps.emplace_back("", L"Keyring locked — unlock to insert a reference");
        }
    } else {
        if (!ui->mcp_alias_popup) {
            const auto* active_project = ui->session.store.active();
            ui->mention_files = agent_file_catalog(ui->knowledge, ui->session.store.active_project_id,
                                                   ui->session.project_root,
                                                   active_project ? active_project->roots : std::vector<std::wstring>{});
        }
        comps = ui->mcp.alias_completions(utf8(token.substr(1)));
        for (auto& c : comps) c.first = "@" + c.first;
        for (const auto& file : match_agent_files(ui->mention_files, token.substr(1))) {
            comps.emplace_back("@\"" + utf8(file.path) + "\"", file.label);
        }
    }
    if (comps.empty()) {
        close_mcp_alias_popup(ui);
        return;
    }

    if (!ui->mcp_alias_popup) {
        ui->mcp_alias_popup =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"LISTBOX", L"",
                            WS_POPUP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED |
                                LBS_NOINTEGRALHEIGHT,
                            0, 0, 0, 0, ui->wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MCP_ALIAS_POPUP)),
                            GetModuleHandleW(nullptr), nullptr);
        if (!ui->mcp_alias_popup) {
            return;
        }
        SendMessageW(ui->mcp_alias_popup, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
        apply_dark_child(ui->mcp_alias_popup);
        install_thin_scrollbar(ui->mcp_alias_popup, theme().menu);
        if (token[0] == L'@') {
            const auto* active_project = ui->session.store.active();
            ui->mention_files = agent_file_catalog(ui->knowledge, ui->session.store.active_project_id,
                                                   ui->session.project_root,
                                                   active_project ? active_project->roots : std::vector<std::wstring>{});
        }
    }

    SendMessageW(ui->mcp_alias_popup, LB_RESETCONTENT, 0, 0);
    ui->mcp_alias_completions.clear();
    for (const auto& c : comps) {
        std::wstring row;
        if (c.first.empty()) row = c.second;
        else if (c.first.starts_with("@\"")) row = c.second + L"  —  file";
        else if (c.first.starts_with("!")) row = utf16(c.first) + L"  —  " + c.second;
        else if (c.first.starts_with("/")) row = utf16(c.first) + L"  —  " + c.second;
        else row = utf16(c.first) + L"  —  " + c.second;
        SendMessageW(ui->mcp_alias_popup, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        ui->mcp_alias_completions.push_back(c.first);
    }
    SendMessageW(ui->mcp_alias_popup, LB_SETCURSEL, 0, 0);

    RECT cr{};
    GetWindowRect(ui->composer, &cr);
    const int row_h = dip(ui->wnd, 28);
    const int h = (std::min)(row_h * static_cast<int>(comps.size()) + 4, dip(ui->wnd, 180));
    const int w = (std::max)(dip(ui->wnd, 280), static_cast<int>(cr.right - cr.left));
    SetWindowPos(ui->mcp_alias_popup, HWND_TOP, cr.left, cr.top - h - 2, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(ui->mcp_alias_popup, nullptr, TRUE);
}

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            nullptr, nullptr);
}

void create_controls(Ui* ui, HWND hwnd) {
    LoadLibraryW(L"Msftedit.dll");
    editor_register(GetModuleHandleW(nullptr));
    const DWORD btn = WS_TABSTOP | BS_OWNERDRAW;
    ui->brand = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ShowWindow(ui->brand, SW_HIDE);
    ui->project = mk(hwnd, L"BUTTON", L"No folder", btn, ID_PROJECT);
    ui->openfolder = mk(hwnd, L"BUTTON", L"Open folder", btn, ID_OPEN_FOLDER);
    ui->toggle_files = mk(hwnd, L"BUTTON", L"Files", btn, ID_TOGGLE_FILES);
    ui->toggle_history = mk(hwnd, L"BUTTON", L"Chats", btn, ID_TOGGLE_HISTORY);
    ui->focus = mk(hwnd, L"BUTTON", L"Focus", btn, ID_FOCUS);
    ui->settings = mk(hwnd, L"BUTTON", L"Settings", btn, ID_SETTINGS);
    ui->account = mk(hwnd, L"BUTTON", L"Account", btn, ID_ACCOUNT);
    ui->signin = mk(hwnd, L"BUTTON", L"Sign in", btn, ID_SIGNIN);
    ui->signout = mk(hwnd, L"BUTTON", L"Sign out", btn, ID_SIGNOUT);
    ui->tab_editor = mk(hwnd, L"BUTTON", L"Editor", btn, ID_TAB_EDITOR);
    ui->tab_agent = mk(hwnd, L"BUTTON", L"Agent", btn, ID_TAB_AGENT);
    ui->hdr_files = mk(hwnd, L"STATIC", L"  Files", 0, ID_HDR_FILES);
    ui->filter = mk(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_MULTILINE, ID_FILTER);
    ui->tree = mk(hwnd, WC_TREEVIEWW, L"", WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_TRACKSELECT,
                   ID_TREE);
    ui->knowledge_tree = mk(hwnd, WC_TREEVIEWW, L"", WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_INFOTIP, ID_KNOWLEDGE_TREE);
    ui->knowledge_header = ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), ID_KNOWLEDGE_HEADER,
        L"▾ KNOWLEDGE", ui_kit::ButtonKind::Ghost, ui->font);
    ui->knowledge_manage = ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), ID_KNOWLEDGE_MANAGE,
        L"…", ui_kit::ButtonKind::Ghost, ui->font);
    ui->hdr_editor = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, ID_HDR_EDITOR);
    ui->tabs = mk(hwnd, WC_TABCONTROLW, L"",
                  WS_TABSTOP | TCS_SINGLELINE | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH | TCS_BUTTONS |
                      TCS_FLATBUTTONS,
                  ID_TABS);
    ui->find = mk(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, ID_FIND);
    ui->find_toggle = mk(hwnd, L"BUTTON", L"Find", btn, ID_TOGGLE_FIND);
    ui->save = mk(hwnd, L"BUTTON", L"Save", btn, ID_SAVE);
    ui->gutter = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ShowWindow(ui->gutter, SW_HIDE);
    ui->editor_status = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, ID_EDITOR_STATUS);
    ShowWindow(ui->editor_status, SW_HIDE);
    ui->empty_editor = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ui->empty_open_file = mk(hwnd, L"BUTTON", L"Open file", btn, ID_EMPTY_OPEN_FILE);
    ui->empty_open_folder = mk(hwnd, L"BUTTON", L"Open folder", btn, ID_EMPTY_OPEN_FOLDER);
    ui->editor = editor_create(hwnd, ID_EDITOR, GetModuleHandleW(nullptr));
    ui->markdown_view = ui_kit::create_markdown_view(hwnd, GetModuleHandleW(nullptr), ID_MARKDOWN_VIEW, ui->font);
    ui->markdown_toggle = ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), ID_MARKDOWN_TOGGLE, L"Source", ui_kit::ButtonKind::Secondary, ui->font);

    ui->content_host = mk(hwnd, L"STATIC", L"", 0, 0);
    // Background-only panel — must not steal mouse from Settings controls above it.
    EnableWindow(ui->content_host, FALSE);
    ShowWindow(ui->content_host, SW_HIDE);
    ui->content_back = mk(hwnd, L"BUTTON", L"‹ Back", btn, ID_CONTENT_BACK);
    ShowWindow(ui->content_back, SW_HIDE);
    ui->content_title = mk(hwnd, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    ShowWindow(ui->content_title, SW_HIDE);
    ui->content_nav = CreateWindowExW(0, L"LISTBOX", L"",
                                      WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED |
                                          LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
                                      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CONTENT_NAV)),
                                      nullptr, nullptr);
    ShowWindow(ui->content_nav, SW_HIDE);
    ui->content_body = CreateWindowExW(0, L"EDIT", L"",
                                       WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
                                       hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CONTENT_BODY)), nullptr, nullptr);
    ShowWindow(ui->content_body, SW_HIDE);
    apply_dark_child(ui->content_body);
    install_thin_scrollbar(ui->content_body, theme().panel);
    install_thin_scrollbar(ui->content_nav, theme().navigation);
    ui->providers_ui.create(hwnd, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), ui->font,
                            ui->font_small ? ui->font_small : ui->font);
    ui->set_codex = mk(hwnd, L"BUTTON", L"Codex executable…", btn, ID_SET_CODEX);
    ui->set_copy_runtime = mk(hwnd, L"BUTTON", L"Copy runtime path", btn, ID_SET_COPY_RUNTIME);
    ui->set_wrap = mk(hwnd, L"BUTTON", L"Word wrap", btn, ID_SET_WRAP);
    ui->set_whitespace = mk(hwnd, L"BUTTON", L"Show whitespace", btn, ID_SET_WHITESPACE);
    ui->set_enter_sends = mk(hwnd, L"BUTTON", L"Enter sends message", btn, ID_SET_ENTER_SENDS);
    ui->set_ui_gallery = mk(hwnd, L"BUTTON", L"UI Gallery…", btn, ID_UI_GALLERY);
    ui->gs_open_folder = mk(hwnd, L"BUTTON", L"Open Project", btn, ID_GS_OPEN_FOLDER);
    ui->gs_providers = mk(hwnd, L"BUTTON", L"Manage Providers", btn, ID_GS_PROVIDERS);
    ui->knowledge_ui.create(hwnd, GetModuleHandleW(nullptr), ui->font);
    ui->mcp_ui.create(hwnd, ui->font, ID_MCP_LIST, ID_MCP_DETAIL, ID_MCP_ADD, ID_MCP_ADD_ACCOUNT, ID_MCP_MANAGE,
                      ID_MCP_REAUTH, ID_MCP_CHECK, ID_MCP_DISABLE, ID_MCP_DISCONNECT, ID_MCP_REMOVE, ID_MCP_ADD_TEMPLATE,
                      ID_MCP_ADD_NAME, ID_MCP_ADD_ALIAS, ID_MCP_ADD_ENDPOINT, ID_MCP_ADD_SAVE, ID_MCP_ADD_CANCEL);
    // Project-scope picker reads the live store, so opening Manage after a new folder is opened
    // lists it without needing a settings reload.
    ui->mcp_ui.set_project_provider([ui]() {
        McpSettingsUi::ProjectList out;
        for (const auto& p : ui->session.store.projects) {
            out.emplace_back(p.id, p.name.empty() ? p.root : p.name);
        }
        return out;
    });
    ui->strata_ui.create(hwnd, GetModuleHandleW(nullptr), ui->font);
    ui->keyring_ui.create(hwnd, ui->font, ui->font_small, ui->font_semi);
    ui->environment_ui.create(hwnd, GetModuleHandleW(nullptr), ui->font, ui->font_small);
    ui->connections_ui.create(hwnd, GetModuleHandleW(nullptr), ui->font, ui->font_small);
    ui->security_overview.create(hwnd, GetModuleHandleW(nullptr), ui->font, ui->font_small);
    ui->security_policy.create(hwnd, GetModuleHandleW(nullptr), ui->font, ui->font_small);
    ui->sec_tab_overview =
        ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_SecTabOverview, L"Overview",
                              ui_kit::ButtonKind::Secondary, ui->font);
    ui->sec_tab_keyring =
        ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_SecTabKeyring, L"Keyring",
                              ui_kit::ButtonKind::Secondary, ui->font);
    ui->sec_tab_environments =
        ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_SecTabEnvironments, L"Project Environments",
                              ui_kit::ButtonKind::Secondary, ui->font);
    ui->sec_tab_connections =
        ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_SecTabConnections, L"Connections",
                              ui_kit::ButtonKind::Secondary, ui->font);
    ui->sec_tab_policy =
        ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_SecTabPolicy, L"Execution Policy",
                              ui_kit::ButtonKind::Secondary, ui->font);
    ShowWindow(ui->sec_tab_overview, SW_HIDE);
    ShowWindow(ui->sec_tab_keyring, SW_HIDE);
    ShowWindow(ui->sec_tab_environments, SW_HIDE);
    ShowWindow(ui->sec_tab_connections, SW_HIDE);
    ShowWindow(ui->sec_tab_policy, SW_HIDE);
    ui->terminal_ui.create(hwnd, GetModuleHandleW(nullptr), ui->font);
    ui->ui_gallery.create(hwnd, GetModuleHandleW(nullptr), ui->font);
    hide_settings_controls(ui);
    ShowWindow(ui->gs_open_folder, SW_HIDE);
    ShowWindow(ui->gs_providers, SW_HIDE);

    wire_workbench_panel(ui);

    ui->hdr_agent = mk(hwnd, L"STATIC", L"  Agent", 0, ID_HDR_AGENT);
    ShowWindow(ui->hdr_agent, SW_HIDE);
    ui->chat_tabs = mk(hwnd, WC_TABCONTROLW, L"",
                       WS_TABSTOP | TCS_SINGLELINE | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH |
                           TCS_BUTTONS | TCS_FLATBUTTONS,
                       ID_CHAT_TABS);
    ui->models = mk(hwnd, L"BUTTON", L"Model", btn, ID_MODELS);
    ui->neu = mk(hwnd, L"BUTTON", L"New", btn, ID_NEW);
    ui->agent_hint = mk(hwnd, L"STATIC", L"", 0, ID_AGENT_HINT);
    ShowWindow(ui->agent_hint, SW_HIDE);
    ui->add_file = mk(hwnd, L"BUTTON", L"Attach", btn, ID_ADD_FILE);
    ui->workflow = ui_kit::create_select(hwnd, GetModuleHandleW(nullptr), ID_WORKFLOW, ui->font);
    ui_kit::select_set_items(ui->workflow, {{L"Execute", 0}, {L"Plan", 1}, {L"Ask", 2}});
    ui_kit::select_set_index(ui->workflow, 0);
    ui->ctx = CreateWindowExW(0, L"LISTBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_CTX), nullptr, nullptr);
    ui->empty_agent = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ui->activity = ui_kit::create_document_view(hwnd, GetModuleHandleW(nullptr), 0, ui->font_small);
    ui->transcript = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
                                     hwnd, reinterpret_cast<HMENU>(ID_TRANSCRIPT), nullptr, nullptr);
    // Keep link notifications enabled for the transcript for its entire lifetime.
    // Attachment footers are appended after history reloads as well as during send.
    SendMessageW(ui->transcript, EM_SETEVENTMASK, 0,
                 SendMessageW(ui->transcript, EM_GETEVENTMASK, 0, 0) | ENM_LINK);
    ui->chat_log = chat_log_create(
        hwnd, ui->font,
        [ui](const std::vector<DisplayAttachment>& attachments) {
            if (attachments.empty()) return;
            const std::wstring key = utf16(make_uuid());
            ui->message_attachments[key] = attachments;
            show_attachment_tray(ui, key, ui->chat_log, 0);
        },
        [ui](const std::wstring& path) {
            if (path.empty()) return;
            std::wstring resolved = path;
            const bool looks_absolute =
                (path.size() >= 2 && path[1] == L':') ||
                (!path.empty() && (path[0] == L'\\' || path[0] == L'/'));
            if (!looks_absolute) {
                const auto* active_project = ui->session.store.active();
                const auto catalog =
                    agent_file_catalog(ui->knowledge, ui->session.store.active_project_id,
                                       ui->session.project_root,
                                       active_project ? active_project->roots : std::vector<std::wstring>{});
                for (const auto& file : catalog) {
                    const auto slash = file.path.find_last_of(L"\\/");
                    const auto base =
                        slash == std::wstring::npos ? file.path : file.path.substr(slash + 1);
                    if (_wcsicmp(base.c_str(), path.c_str()) == 0 ||
                        _wcsicmp(file.label.c_str(), path.c_str()) == 0) {
                        resolved = file.path;
                        break;
                    }
                }
            }
            if (open_document(ui, resolved, true)) {
                show_editor_content(ui);
                layout(ui);
            }
        });
    ShowWindow(ui->chat_log, SW_HIDE);
    ui->composer_panel = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY | WS_CLIPSIBLINGS, 0);
    ui->composer = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_TABSTOP, 0, 0, 0, 0,
                                    hwnd, reinterpret_cast<HMENU>(ID_COMPOSER), nullptr, nullptr);
    ui->send = mk(hwnd, L"BUTTON", L"Send", btn | BS_OWNERDRAW, ID_SEND);
    ui->cancel = mk(hwnd, L"BUTTON", L"Stop", btn | BS_OWNERDRAW, ID_CANCEL);
    ShowWindow(ui->cancel, SW_HIDE);
    ui->composer_cue = mk(hwnd, L"STATIC", L"Message your agent  ·  /scylla-query for DB handoff", SS_NOTIFY, 0);
    ui->hdr_history = mk(hwnd, L"STATIC", L"  Chats", 0, ID_HDR_HISTORY);
    ShowWindow(ui->hdr_history, SW_HIDE);
    ui->scope = mk(hwnd, L"BUTTON", L"Current project", btn, ID_SCOPE);
    ShowWindow(ui->scope, SW_HIDE);
    ui->search = mk(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_MULTILINE, ID_SEARCH);
    ui->pin = mk(hwnd, L"BUTTON", L"Pin", btn, ID_PIN_CHAT);
    ui->archive = mk(hwnd, L"BUTTON", L"Delete", btn, ID_ARCHIVE_CHAT);
    ui->threads = CreateWindowExW(0, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS |
                                      WS_TABSTOP,
                                  0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_THREADS), nullptr, nullptr);
    SetWindowSubclass(ui->threads, chat_list_mouse_guard, 0x53434854, reinterpret_cast<DWORD_PTR>(&ui->chat_groups));
    ui->empty_chats = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ui->status = mk(hwnd, L"STATIC", L"Offline", 0, ID_STATUS);
    ui->status_env = ui_kit::create_button(hwnd, GetModuleHandleW(nullptr), Cmd_StatusEnv, L"Env: None",
                                           ui_kit::ButtonKind::Ghost, ui->font_small);
    ShowWindow(ui->status_env, SW_HIDE);
    ui->homehint = mk(hwnd, L"STATIC", L"", SS_RIGHT, ID_HOMEHINT);

    SendMessageW(ui->transcript, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
    SendMessageW(ui->composer, EM_EXLIMITTEXT, 0, 256 * 1024);
    SendMessageW(ui->filter, 0x1501, TRUE, reinterpret_cast<LPARAM>(L"Find a file…"));
    SendMessageW(ui->search, 0x1501, TRUE, reinterpret_cast<LPARAM>(L"Search chats…"));
    SendMessageW(ui->find, 0x1501, TRUE, reinterpret_cast<LPARAM>(L"Find or line…"));
    ShowWindow(ui->signout, SW_HIDE);
    ShowWindow(ui->signin, SW_HIDE);
    ShowWindow(ui->pin, SW_HIDE);
    ShowWindow(ui->archive, SW_HIDE);
    ShowWindow(ui->find, SW_HIDE);
    ShowWindow(ui->homehint, SW_HIDE);
    ShowWindow(ui->composer_panel, SW_HIDE);
    ShowWindow(ui->empty_editor, SW_HIDE);
    ShowWindow(ui->empty_open_file, SW_HIDE);
    ShowWindow(ui->empty_open_folder, SW_HIDE);
    TreeView_SetBkColor(ui->tree, kFiles);
    TreeView_SetTextColor(ui->tree, kText);
    TreeView_SetLineColor(ui->tree, kBorder);
    TreeView_SetItemHeight(ui->tree, dip(hwnd, 26));
    TreeView_SetIndent(ui->tree, dip(hwnd, 16));
    TreeView_SetExtendedStyle(ui->tree, 0x0004, 0x0004);
    apply_dark_child(ui->tree);
    TreeView_SetBkColor(ui->knowledge_tree, kFiles);
    TreeView_SetTextColor(ui->knowledge_tree, kText);
    TreeView_SetLineColor(ui->knowledge_tree, kBorder);
    TreeView_SetItemHeight(ui->knowledge_tree, dip(hwnd, 26));
    TreeView_SetIndent(ui->knowledge_tree, dip(hwnd, 16));
    TreeView_SetExtendedStyle(ui->knowledge_tree, 0x0004, 0x0004);
    apply_dark_child(ui->knowledge_tree);
    install_thin_scrollbar(ui->knowledge_tree, kFiles);
    SetWindowTheme(ui->tabs, L"", L"");
    SetWindowTheme(ui->chat_tabs, L"", L"");
    SetWindowTheme(ui->filter, L"", L"");
    SetWindowTheme(ui->search, L"", L"");
    SetWindowTheme(ui->find, L"", L"");
    SetWindowTheme(ui->composer, L"", L"");
    set_rich_colors(ui->transcript, kWindow, 14, L"Segoe UI");
    set_rich_colors(ui->composer, kInput, 14, L"Segoe UI");
    // Thin flat scrollbars (panel-matching track, ~8dip thumb).
    install_thin_scrollbar(ui->tree, kFiles);
    install_thin_scrollbar(ui->threads, kHistory);
    install_thin_scrollbar(ui->ctx, kWindow);
    apply_dark_child(ui->transcript);
    install_thin_scrollbar(ui->transcript, kWindow);
    install_thin_scrollbar(ui->editor, kEditor);
    install_thin_scrollbar(ui->composer, kInput);
    // RichEdit sends no EN_CHANGE unless asked. Without this the composer's token styling,
    // autocomplete popup, and auto-grow never run.
    SendMessageW(ui->composer, EM_SETEVENTMASK, 0,
                 SendMessageW(ui->composer, EM_GETEVENTMASK, 0, 0) | ENM_CHANGE);
    ui->composer_prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui->composer, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(composer_proc)));
    ui->panel_prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(ui->composer_panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(panel_proc)));
    ui->filter_prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui->filter, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(search_edit_proc)));
    ui->search_prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui->search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(search_edit_proc)));
    ui->find_prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui->find, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(search_edit_proc)));
    ui->tabs_prev =
        reinterpret_cast<WNDPROC>(SetWindowLongPtrW(ui->tabs, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(tabs_proc)));
    ui->chat_tabs_prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(ui->chat_tabs, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(chat_tabs_proc)));
    ui->tips = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, nullptr,
                               nullptr);
    auto tip = [&](HWND target, const wchar_t* text) {
        if (!ui->tips || !target) {
            return;
        }
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd;
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.uId = reinterpret_cast<UINT_PTR>(target);
        ti.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(ui->tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    };
    tip(ui->toggle_files, L"Files pane");
    tip(ui->toggle_history, L"Chats pane");
    tip(ui->focus, L"Focus editor and agent");
    tip(ui->openfolder, L"Open folder");
    tip(ui->neu, L"New chat");
    tip(ui->send, L"Send");
    tip(ui->add_file, L"Attach file");
    tip(ui->models, L"Agent model (OpenAI / Claude)");
    tip(ui->find_toggle, L"Find or go to line");
    tip(ui->save, L"Save");
}

void apply_fonts(Ui* ui) {
    for (HWND h : {ui->project, ui->openfolder, ui->toggle_files, ui->toggle_history, ui->focus, ui->settings, ui->signin,
                    ui->signout, ui->hdr_files, ui->hdr_editor, ui->hdr_agent, ui->hdr_history, ui->neu, ui->send, ui->cancel,
                    ui->tab_editor, ui->tab_agent, ui->models, ui->threads, ui->filter, ui->search, ui->tree, ui->knowledge_tree, ui->knowledge_header, ui->knowledge_manage, ui->tabs,
                    ui->chat_tabs, ui->find, ui->find_toggle, ui->save, ui->scope, ui->ctx, ui->add_file, ui->workflow,
                    ui->pin, ui->archive, ui->empty_open_file, ui->empty_open_folder, ui->composer_cue, ui->account,
                    ui->content_back, ui->content_title, ui->content_nav, ui->content_body, ui->set_codex,
                    ui->set_copy_runtime, ui->set_wrap,
                    ui->set_whitespace, ui->set_enter_sends, ui->gs_open_folder, ui->gs_providers}) {
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
        }
    }
    ui->providers_ui.set_fonts(ui->font, ui->font_small);
    SendMessageW(ui->account, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->agent_hint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->activity, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->status, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->homehint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    if (ui->editor_status) {
        SendMessageW(ui->editor_status, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    }
    if (ui->content_title) {
        SendMessageW(ui->content_title, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_semi), TRUE);
    }
    chat_log_set_font(ui->chat_log, ui->font);
}

bool pt_in(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

HBRUSH brush_for(Ui* ui, HWND child) {
    if (child == ui->tree || child == ui->hdr_files || child == ui->filter) {
        return ui->files;
    }
    if (child == ui->threads || child == ui->hdr_history || child == ui->search || child == ui->scope) {
        return ui->history_br;
    }
    if (child == ui->hdr_agent || child == ui->agent_hint || child == ui->ctx || child == ui->chat_tabs) {
        return ui->agent_br;
    }
    if (ui->providers_ui.on_card(child)) {
        return ui->agent_br;  // provider cards are raised (theme.surface)
    }
    if (child == ui->content_host || child == ui->content_title || child == ui->content_body || child == ui->content_nav ||
        ui->providers_ui.owns_hwnd(child) ||
        ui->keyring_ui.owns_hwnd(child) || ui->environment_ui.owns_hwnd(child) ||
        ui->connections_ui.owns_hwnd(child) || ui->security_overview.owns_hwnd(child) ||
        ui->security_policy.owns_hwnd(child)) {
        return ui->editor_br;
    }
    return ui->bg;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Ui* ui = g_ui;
    {
        LRESULT dark_lr = 0;
        if (handle_dark_menubar_message(hwnd, msg, wparam, lparam, &dark_lr)) {
            return dark_lr;
        }
    }
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            ui = static_cast<Ui*>(cs->lpCreateParams);
            g_ui = ui;
            ui->wnd = hwnd;
            ui->bg = CreateSolidBrush(kWindow);
            ui->files = CreateSolidBrush(kFiles);
            ui->editor_br = CreateSolidBrush(kEditor);
            ui->agent_br = CreateSolidBrush(kAgent);
            ui->history_br = CreateSolidBrush(kHistory);
            ui->input_br = CreateSolidBrush(kInput);
            ui->font = make_font_dip(hwnd, 13, false, L"Segoe UI Variable");
            ui->font_small = make_font_dip(hwnd, 12, false, L"Segoe UI");
            ui->font_semi = make_font_dip(hwnd, 14, true, L"Segoe UI Variable");
            ui->font_title = make_font_dip(hwnd, 18, true, L"Segoe UI Variable");
            ui->font_mono = make_font_dip(hwnd, 14, false, L"Cascadia Mono");
            create_controls(ui, hwnd);
            apply_fonts(ui);
            apply_dark_caption(hwnd);
            SetMenu(hwnd, build_menu_bar());
            apply_dark_menus(hwnd);
            DrawMenuBar(hwnd);
            SendMessageW(ui->tabs, TCM_SETITEMSIZE, 0, MAKELPARAM(dip(hwnd, 140), dip(hwnd, 32)));
            // ~12 Segoe characters + padding; fixed-width so labels aren't crushed.
            SendMessageW(ui->chat_tabs, TCM_SETITEMSIZE, 0, MAKELPARAM(dip(hwnd, 118), dip(hwnd, 28)));
            SendMessageW(ui->chat_tabs, TCM_SETPADDING, 0, MAKELPARAM(dip(hwnd, 10), dip(hwnd, 4)));
            SendMessageW(ui->threads, LB_SETITEMHEIGHT, 0, dip(hwnd, 48));
            SendMessageW(ui->ctx, LB_SETITEMHEIGHT, 0, dip(hwnd, 24));
            ui->session.paths = make_paths();
            ui->session.settings = load_settings(ui->session.paths.settings_path);
            ui->knowledge.load(ui->session.paths.knowledge_path);
            ui->mcp.load(ui->session.paths.mcp_path);
            ui->session.set_mcp_manager(&ui->mcp);
            ui->strata.load(ui->session.paths.strata_path);
            ui->environments.load(ui->session.paths.environments_path);
            ui->connections.load(ui->session.paths.connections_path);
            start_query_broker(ui);
            reload_terminal_profiles(ui);
            ui->session.selected_model = ui->session.settings.selected_model;
            ui->session.sync_claude_models();
            ui->claude_auth_polls = 3;  // warm Claude OAuth cache so agent menu includes Claude rows early
            if (ui->session.settings.codex_path.empty()) {
                ui->session.settings.codex_path = discover_codex_exe();
            }
            apply_editor_prefs(ui);
            SetWindowTextW(ui->composer, utf16(ui->session.draft_for(ui->session.settings.last_thread_id)).c_str());
            std::wstring err;
            if (!ui->session.start_runtime(hwnd, WM_SCYLLA_LINE, &err)) {
                SetWindowTextW(ui->status, err.c_str());
            }
            // Store is loaded inside start_runtime; Knowledge was loaded above — expand
            // Codex writableRoots so Agent chat can browse Knowledge trees (e.g. .md).
            sync_knowledge_agent_grants(ui);
            // Project-scoped Knowledge sources need the loaded active project id.
            rebuild_tree(ui);
            refresh_projects(ui);
            refresh_threads(ui);
            refresh_chrome(ui);
            if (ui->session.settings.restore_chat_on_start) {
                const auto wanted = ui->session.settings.last_thread_id;
                const auto* chat = ui->session.store.by_thread(wanted);
                if (chat && chat->provider_id == "claude") {
                    const auto it = std::find(ui->thread_ids.begin(), ui->thread_ids.end(), wanted);
                    if (it != ui->thread_ids.end()) {
                        open_chat_index(ui, static_cast<int>(it - ui->thread_ids.begin()));
                        apply_stream(ui);
                    }
                }
            }
            if (ui->session.settings.terminal_visible) {
                ensure_terminal_panel(ui);
            }
            SetTimer(hwnd, 1, 2000, nullptr);
            SetTimer(hwnd, 2, 180, nullptr);
            queue_mcp_auth_checks(ui, true);
            pump_mcp_auth_queue(ui);
            return 0;
        }
        case WM_SIZE:
            layout(ui);
            return 0;
        case WM_INITMENUPOPUP: {
            HMENU menu = reinterpret_cast<HMENU>(wparam);
            menu_apply_command_state(menu, capture_command_state(ui));
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                // Priority: cancel an open panel form → close the find bar → leave the section.
                if (cancel_active_form(ui)) {
                    return 0;
                }
                if (ui->find_open && ui->content_view == ContentView::Editor) {
                    ui->find_open = false;
                    layout(ui);
                    if (ui->editor) {
                        SetFocus(ui->editor);
                    }
                    return 0;
                }
                if (ui->content_view != ContentView::Editor) {
                    go_back_content(ui);
                    return 0;
                }
            }
            break;
        case ui_kit::WM_SK_FIELD_SUBMIT: {
            // Enter inside a kit field fires the owning panel's default action.
            HWND field = reinterpret_cast<HWND>(lparam);
            const UINT cmd = panel_default_command(ui, field);
            if (cmd) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, BN_CLICKED), 0);
            }
            return 0;
        }
        case ui_kit::WM_SK_FIELD_CANCEL: {
            // Esc inside a kit field: cancel the form, else clear the field, else leave section.
            HWND field = reinterpret_cast<HWND>(lparam);
            if (cancel_active_form(ui)) {
                return 0;
            }
            if (field && GetWindowTextLengthW(field) > 0) {
                SetWindowTextW(field, L"");
                return 0;
            }
            if (ui->content_view != ContentView::Editor) {
                go_back_content(ui);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, ui->bg);
            HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
            HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
            auto line = [&](const RECT& s) {
                if (s.right <= s.left) {
                    return;
                }
                const int x = (s.left + s.right) / 2;
                MoveToEx(dc, x, s.top, nullptr);
                LineTo(dc, x, s.bottom);
            };
            line(ui->split1);
            line(ui->split2);
            line(ui->split3);
            if (ui->split_term.bottom > ui->split_term.top) {
                const int y = (ui->split_term.top + ui->split_term.bottom) / 2;
                MoveToEx(dc, ui->split_term.left, y, nullptr);
                LineTo(dc, ui->split_term.right, y);
            }
            if (ui->split_knowledge.bottom > ui->split_knowledge.top) {
                const int y = (ui->split_knowledge.top + ui->split_knowledge.bottom) / 2;
                MoveToEx(dc, ui->split_knowledge.left, y, nullptr);
                LineTo(dc, ui->split_knowledge.right, y);
            }
            SelectObject(dc, old);
            DeleteObject(pen);
            if (ui->content_view == ContentView::Settings && ui->settings_section == SettingsSection::Providers) {
                ui->providers_ui.paint_chrome(dc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                if (pt_in(ui->split_term, p.x, p.y) || pt_in(ui->split_knowledge, p.x, p.y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
                if (pt_in(ui->split1, p.x, p.y) || pt_in(ui->split2, p.x, p.y) || pt_in(ui->split3, p.x, p.y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            ui_kit::select_close_all();
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            ui->drag = 0;
            if (pt_in(ui->split_knowledge, x, y)) {
                ui->drag = 5;
                ui->drag_origin = y;
                ui->knowledge_h0 = ui->knowledge_height;
                SetCapture(hwnd);
                return 0;
            }
            if (pt_in(ui->split_term, x, y)) {
                ui->drag = 4;
                ui->drag_origin = y;
                ui->terminal_h0 = ui->session.settings.terminal_h;
                SetCapture(hwnd);
                return 0;
            }
            if (pt_in(ui->split1, x, y)) {
                ui->drag = 1;
            } else if (pt_in(ui->split2, x, y)) {
                ui->drag = 2;
            } else if (pt_in(ui->split3, x, y)) {
                ui->drag = 3;
            }
            if (ui->drag) {
                ui->drag_origin = x;
                ui->files_w0 = ui->session.settings.files_w;
                ui->agent_w0 = ui->session.settings.agent_w;
                ui->history_w0 = ui->session.settings.history_w;
                SetCapture(hwnd);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (ui->drag && (wparam & MK_LBUTTON)) {
                if (ui->drag == 5) {
                    const int dy = px_to_dip(hwnd, ui->drag_origin - GET_Y_LPARAM(lparam));
                    const int max_h = (std::max)(28, ui->knowledge_max_height);
                    ui->session.settings.knowledge_h = std::clamp(ui->knowledge_h0 + dy, (std::min)(80, max_h), max_h);
                    layout(ui);
                    return 0;
                }
                if (ui->drag == 4) {
                    const int y = GET_Y_LPARAM(lparam);
                    const int dy = px_to_dip(hwnd, ui->drag_origin - y);
                    RECT crc{};
                    GetClientRect(hwnd, &crc);
                    const int client_h_dip = px_to_dip(hwnd, crc.bottom - crc.top);
                    const int max_term =
                        (std::max)(ui_space::kMinTerminalHDip,
                                   client_h_dip - 40 - 22 - 180);  // header + status + min body
                    ui->session.settings.terminal_h =
                        std::clamp(ui->terminal_h0 + dy, ui_space::kMinTerminalHDip, max_term);
                    if (ui->workbench_panel.maximized()) {
                        ui->workbench_panel.set_maximized(false);
                    }
                    ui->panel_collapsed = false;
                    ui->workbench_panel.set_collapsed(false);
                    layout(ui);
                    return 0;
                }
                const int x = GET_X_LPARAM(lparam);
                const int dx = px_to_dip(hwnd, x - ui->drag_origin);
                if (ui->drag == 1) {
                    ui->session.settings.files_w = std::max(180, ui->files_w0 + dx);
                } else if (ui->drag == 2) {
                    ui->session.settings.agent_w = std::max(320, ui->agent_w0 - dx);
                } else if (ui->drag == 3) {
                    ui->session.settings.history_w = std::max(180, ui->history_w0 - dx);
                }
                layout(ui);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            ui->drag = 0;
            break;
        case WM_LBUTTONUP:
            if (ui->drag) {
                ui->drag = 0;
                ReleaseCapture();
                save_settings(ui->session.paths.settings_path, ui->session.settings);
                persist_store(ui);
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            HWND child = reinterpret_cast<HWND>(lparam);
            // Kit labels: no fill box — transparent over content_host.
            if (msg == WM_CTLCOLORSTATIC &&
                (GetPropW(child, L"ScyllaStatic") || GetPropW(child, L"ScyllaMuted"))) {
                const bool muted = GetPropW(child, L"ScyllaMuted") != nullptr;
                SetTextColor(dc, muted ? kMuted : kText);
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
            }
            SetTextColor(dc, kText);
            if (child == ui->agent_hint || child == ui->status || child == ui->homehint || child == ui->account ||
                child == ui->composer_cue) {
                SetTextColor(dc, kMuted);
            }
            if (child == ui->composer_cue) {
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
            }
            HBRUSH br = brush_for(ui, child);
            COLORREF bk = kWindow;
            if (br == ui->files) {
                bk = kFiles;
            } else if (br == ui->history_br) {
                bk = kHistory;
            } else if (br == ui->agent_br) {
                bk = kAgent;
            } else if (br == ui->editor_br) {
                bk = kEditor;
            }
            if (child == ui->filter || child == ui->search || child == ui->find) {
                bk = kInput;
                br = ui->input_br;
            }
            // Composer RichEdit: match panel fill (no separate nested paint via CTLCOLOR).
            if (child == ui->composer) {
                bk = kInput;
                br = ui->input_br;
            }
            SetBkColor(dc, bk);
            return reinterpret_cast<LRESULT>(br);
        }
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (mi->CtlType == ODT_LISTBOX) {
                if (ui->mcp_ui.measure_item(mi) || ui->terminal_ui.measure_item(mi) ||
                    ui->knowledge_ui.measure_item(mi) || ui->environment_ui.measure_item(mi) ||
                    ui->connections_ui.measure_item(mi) || ui->providers_ui.measure_item(mi)) {
                    return TRUE;
                }
                if (mi->CtlID == ID_THREADS) {
                    mi->itemHeight = dip(hwnd, 48);
                } else if (mi->CtlID == ID_CTX) {
                    mi->itemHeight = dip(hwnd, 24);
                } else if (mi->CtlID == ID_CONTENT_NAV) {
                    mi->itemHeight = dip(hwnd, 32);
                } else if (mi->CtlID == ID_MCP_ALIAS_POPUP) {
                    mi->itemHeight = dip(hwnd, 28);
                } else {
                    mi->itemHeight = dip(hwnd, 28);
                }
            } else if (mi->CtlType == ODT_TAB) {
                mi->itemHeight = dip(hwnd, 32);
            }
            return TRUE;
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (ui_kit::draw_kit_item(di, ui->font)) {
                return TRUE;
            }
            if (ui->terminal_ui.draw_item(di, ui->font) || ui->knowledge_ui.draw_item(di, ui->font) ||
                ui->mcp_ui.draw_item(di, ui->font) || ui->environment_ui.draw_item(di, ui->font) ||
                ui->connections_ui.draw_item(di, ui->font) || ui->providers_ui.draw_item(di, ui->font) ||
                ui->ui_gallery.draw_item(di, ui->font)) {
                return TRUE;
            }
            if (di->CtlType == ODT_BUTTON) {
                BtnVisual vis = BtnVisual::Secondary;
                bool tog = false;
                if (di->CtlID == ID_SEND) {
                    vis = BtnVisual::Primary;
                } else if (di->CtlID == ID_PROJECT || di->CtlID == ID_SCOPE) {
                    vis = BtnVisual::Selector;
                } else if (di->CtlID == ID_MODELS) {
                    vis = BtnVisual::Selector;
                } else if (di->CtlID == ID_ADD_FILE || di->CtlID == ID_ADD_SEL) {
                    vis = BtnVisual::Quiet;
                } else if (di->CtlID == ID_OPEN_FOLDER || di->CtlID == ID_SETTINGS || di->CtlID == ID_SAVE ||
                           di->CtlID == ID_NEW || di->CtlID == ID_TOGGLE_FIND) {
                    vis = BtnVisual::Icon;
                    if (di->CtlID == ID_TOGGLE_FIND) {
                        tog = ui->find_open;
                    }
                } else if (di->CtlID == ID_TOGGLE_FILES) {
                    vis = BtnVisual::Toggle;
                    tog = ui->panes.show_files;
                } else if (di->CtlID == ID_TOGGLE_HISTORY) {
                    vis = BtnVisual::Toggle;
                    tog = ui->panes.show_history;
                } else if (di->CtlID == ID_FOCUS) {
                    vis = BtnVisual::Toggle;
                    tog = ui->session.settings.focus_editor;
                } else if (di->hwndItem == ui->set_wrap) {
                    vis = BtnVisual::Option;
                    tog = ui->session.settings.word_wrap;
                } else if (di->hwndItem == ui->set_whitespace) {
                    vis = BtnVisual::Option;
                    tog = ui->session.settings.show_whitespace;
                } else if (di->hwndItem == ui->set_enter_sends) {
                    vis = BtnVisual::Option;
                    tog = ui->session.settings.enter_sends;
                } else if (di->hwndItem == ui->set_ui_gallery) {
                    vis = BtnVisual::Secondary;
                }
                draw_themed_button(di, ui->font, vis, tog, ui->session.state == AppState::Generating && di->CtlID == ID_SEND);
                return TRUE;
            }
            if (di->CtlType == ODT_COMBOBOX) {
                draw_combo_item(di, ui->font, (di->itemState & ODS_COMBOBOXEDIT) != 0);
                return TRUE;
            }
            if (di->CtlType == ODT_TAB) {
                wchar_t buf[128]{};
                TCITEMW it{};
                it.mask = TCIF_TEXT;
                it.pszText = buf;
                it.cchTextMax = 128;
                TabCtrl_GetItem(di->hwndItem, di->itemID, &it);
                const bool active = TabCtrl_GetCurSel(di->hwndItem) == static_cast<int>(di->itemID);
                const bool closable = di->hwndItem == ui->tabs;
                draw_tab_item(di->hDC, di->rcItem, ui->font, buf, active, false, closable);
                return TRUE;
            }
            if (di->CtlType == ODT_LISTBOX) {
                if (di->itemID == static_cast<UINT>(-1)) {
                    fill_rect(di->hDC, di->rcItem, theme().menu);
                    return TRUE;
                }
                wchar_t buf[256]{};
                SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, reinterpret_cast<LPARAM>(buf));
                const bool sel = (di->itemState & ODS_SELECTED) != 0;
                if (di->hwndItem == ui->threads && di->itemID < ui->chat_groups.size()) {
                    RECT row = di->rcItem;
                    const auto& heading = ui->chat_groups[di->itemID];
                    if (!heading.empty()) {
                        RECT label = row;
                        label.bottom = label.top + dip(hwnd, 26);
                        fill_rect(di->hDC, label, theme().navigation);
                        label.left += dip(hwnd, 10);
                        SetTextColor(di->hDC, theme().muted);
                        SetBkMode(di->hDC, TRANSPARENT);
                        auto old = SelectObject(di->hDC, ui->font_small);
                        DrawTextW(di->hDC, heading.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                        SelectObject(di->hDC, old);
                        row.top = label.bottom;
                    }
                    draw_list_row(di->hDC, row, ui->font, buf, sel, false);
                    if (di->itemID < ui->thread_ids.size() && ui->session.thread_busy(ui->thread_ids[di->itemID])) {
                        const int cy = (row.top + row.bottom) / 2;
                        const int gap = dip(hwnd, 5), radius = dip(hwnd, 2);
                        const int right = row.right - dip(hwnd, 12);
                        for (int dot = 0; dot < 3; ++dot) {
                            HBRUSH brush = CreateSolidBrush(
                                dot == static_cast<int>(ui->chat_busy_frame % 3) ? theme().amber : theme().muted);
                            HBRUSH old = static_cast<HBRUSH>(SelectObject(di->hDC, brush));
                            SelectObject(di->hDC, GetStockObject(NULL_PEN));
                            const int cx = right - (2 - dot) * gap;
                            Ellipse(di->hDC, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
                            SelectObject(di->hDC, old);
                            DeleteObject(brush);
                        }
                    }
                } else if (di->hwndItem == ui->content_nav) {
                    ui_kit::paint_nav_row(di->hDC, di->rcItem, ui->font, buf, sel, false);
                } else {
                    draw_list_row(di->hDC, di->rcItem, ui->font, buf, sel, false);
                }
                return TRUE;
            }
            if (di->CtlType == ODT_STATIC) {
                if (di->hwndItem == ui->brand) {
                    return TRUE;
                }
                if (di->hwndItem == ui->gutter) {
                    return TRUE;
                }
                if (di->hwndItem == ui->editor_status) {
                    const Theme& t = theme();
                    fill_rect(di->hDC, di->rcItem, t.shell);
                    wchar_t buf[256]{};
                    GetWindowTextW(di->hwndItem, buf, 256);
                    RECT tr = di->rcItem;
                    SetBkMode(di->hDC, TRANSPARENT);
                    SetTextColor(di->hDC, t.muted);
                    SelectObject(di->hDC, ui->font_small ? ui->font_small : ui->font);
                    DrawTextW(di->hDC, buf, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    return TRUE;
                }
                if (di->hwndItem == ui->empty_editor) {
                    paint_empty_editor(di->hDC, di->rcItem, ui->font_title, ui->font);
                    return TRUE;
                }
                if (di->hwndItem == ui->empty_agent) {
                    paint_empty_agent(di->hDC, di->rcItem, ui->font_semi, ui->font);
                    return TRUE;
                }
                if (di->hwndItem == ui->empty_chats) {
                    paint_empty_chats(di->hDC, di->rcItem, ui->font_semi, ui->font_small);
                    return TRUE;
                }
                if (di->hwndItem == ui->composer_panel) {
                    paint_composer_chrome(di->hDC, di->rcItem, dip(hwnd, 10));
                    if (!ui->images.empty()) {
                        std::vector<HBITMAP> thumbs;
                        thumbs.reserve(ui->images.size());
                        for (const auto& img : ui->images) {
                            thumbs.push_back(img.thumb);
                        }
                        RECT tr = di->rcItem;
                        tr.left += dip(hwnd, 12);
                        tr.right -= dip(hwnd, 12);
                        tr.top += dip(hwnd, 8);
                        tr.bottom = tr.top + dip(hwnd, 48);
                        paint_image_thumbs(di->hDC, tr, thumbs, dip(hwnd, 48), dip(hwnd, 8));
                    }
                    return TRUE;
                }
                if (di->hwndItem == ui->hdr_editor) {
                    const Theme& t = theme();
                    fill_rect(di->hDC, di->rcItem, t.shell);
                    wchar_t buf[512]{};
                    GetWindowTextW(di->hwndItem, buf, 512);
                    RECT tr = di->rcItem;
                    SetBkMode(di->hDC, TRANSPARENT);
                    SetTextColor(di->hDC, t.muted);
                    SelectObject(di->hDC, ui->font_small ? ui->font_small : ui->font);
                    DrawTextW(di->hDC, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    return TRUE;
                }
            }
            break;
        }
        case WM_CONTEXTMENU: {
            HWND from = reinterpret_cast<HWND>(wparam);
            if (from == ui->knowledge_tree) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (point.x == -1 && point.y == -1) {
                    RECT rect{};
                    GetWindowRect(from, &rect);
                    point = {rect.left + dip(hwnd, 16), rect.top + dip(hwnd, 16)};
                }
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"Manage knowledge folders…");
                AppendMenuW(menu, MF_STRING, 2, L"Refresh folders");
                const int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (action == 1) open_settings_section(ui, SettingsSection::Knowledge);
                if (action == 2) rebuild_tree(ui);
                return 0;
            }
            if (from == ui->threads && !ui->thread_ids.empty()) {
                POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (p.x == -1 && p.y == -1) {
                    const int selected = static_cast<int>(SendMessageW(ui->threads, LB_GETCURSEL, 0, 0));
                    RECT row{};
                    if (selected < 0 || SendMessageW(ui->threads, LB_GETITEMRECT, selected, reinterpret_cast<LPARAM>(&row)) == LB_ERR) return 0;
                    p = {row.left, row.bottom}; ClientToScreen(ui->threads, &p);
                } else {
                    POINT client = p; ScreenToClient(ui->threads, &client);
                    const int hit = chat_row_at_point(ui->threads, client, ui->chat_groups);
                    if (hit < 0) return 0;
                    SendMessageW(ui->threads, LB_SETCURSEL, hit, 0);
                }
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, ID_RENAME_CHAT, L"Rename");
                AppendMenuW(m, MF_STRING, ID_PIN_CHAT, L"Pin / unpin");
                AppendMenuW(m, MF_STRING, ID_ARCHIVE_CHAT, L"Delete");
                TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(m);
                return 0;
            }
            if (from == ui->tree && !ui->session.store.active_project_id.empty()) {
                POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (p.x == -1 && p.y == -1) {
                    HTREEITEM selected = TreeView_GetSelection(ui->tree);
                    RECT rect{};
                    if (selected && TreeView_GetItemRect(ui->tree, selected, &rect, TRUE)) {
                        p = {rect.left, rect.bottom};
                        ClientToScreen(ui->tree, &p);
                    } else {
                        GetWindowRect(ui->tree, &rect);
                        p = {rect.left + dip(hwnd, 16), rect.top + dip(hwnd, 16)};
                    }
                }
                // Security entry points belong to the project root, not to arbitrary files or the
                // KNOWLEDGE section.
                TVHITTESTINFO ht{};
                ht.pt = p;
                ScreenToClient(ui->tree, &ht.pt);
                HTREEITEM hit = TreeView_HitTest(ui->tree, &ht);
                bool project_root = false;
                if (hit) {
                    TreeView_SelectItem(ui->tree, hit);
                    TVITEMW it{};
                    it.mask = TVIF_PARAM;
                    it.hItem = hit;
                    if (TreeView_GetItem(ui->tree, &it)) {
                        const auto* node = reinterpret_cast<TreeNode*>(it.lParam);
                        project_root = !TreeView_GetParent(ui->tree, hit) && node && node->kind == TreeNode::Kind::Project;
                    }
                }
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, ID_REFRESH_FILES, L"Refresh");
                if (project_root) {
                    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(m, MF_STRING, Cmd_TreeProjectEnv, L"Project Environment…");
                    AppendMenuW(m, MF_STRING, Cmd_TreeProjectSecrets, L"Project Secrets…");
                    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(m, MF_STRING, Cmd_TreeProjectSecurity, L"Security…");
                }
                TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(m);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lparam);
            if (hdr->code == EN_LINK) {
                const auto* link = reinterpret_cast<ENLINK*>(lparam);
                if (link->msg != WM_LBUTTONUP) return 0;
                auto target = ui_kit::markdown_link_at(hdr->hwndFrom, link->chrg.cpMin);
                if (target.empty()) return 0;
                const auto key = file_search_key(target);
                const std::wstring attachment_scheme = L"scylla-attachments:";
                if (key.starts_with(attachment_scheme)) {
                    show_attachment_tray(ui, target.substr(attachment_scheme.size()), hdr->hwndFrom, link->chrg.cpMin);
                    return 0;
                }
                if (key.starts_with(L"https://") || key.starts_with(L"http://")) {
                    ShellExecuteW(hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                if (key.starts_with(L"file:///")) target = target.substr(8);
                // Decode percent-encoded UTF-8 paths (including spaces).
                const auto encoded = utf8(target); std::string decoded;
                auto hex = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
                for (std::size_t i = 0; i < encoded.size(); ++i) {
                    if (encoded[i] == '%' && i + 2 < encoded.size() && hex(encoded[i + 1]) >= 0 && hex(encoded[i + 2]) >= 0) {
                        decoded += static_cast<char>(hex(encoded[i + 1]) * 16 + hex(encoded[i + 2])); i += 2;
                    } else decoded += encoded[i];
                }
                target = utf16(decoded);
                if (target.size() > 3 && target[0] == L'/' && target[2] == L':') target.erase(0, 1);
                int line = 0;
                const auto colon = target.find_last_of(L':');
                if (colon != std::wstring::npos && colon > 1 && colon + 1 < target.size() &&
                    target.find_first_not_of(L"0123456789", colon + 1) == std::wstring::npos) {
                    line = _wtoi(target.c_str() + colon + 1); target.resize(colon);
                }
                // Never dispatch arbitrary URI schemes or executable files to the OS.
                const auto scheme = target.find(L':');
                if (scheme != std::wstring::npos && scheme != 1) return 0;
                std::filesystem::path path(target);
                if (path.is_relative()) {
                    const auto base = hdr->hwndFrom == ui->markdown_view && ui->active_doc >= 0
                        ? std::filesystem::path(ui->docs[ui->active_doc].path).parent_path() : std::filesystem::path(ui->session.project_root);
                    path = base / path;
                }
                if (open_document(ui, path.wstring(), true)) {
                    show_editor_content(ui);
                    if (line > 0) { ui->docs[ui->active_doc].markdown_source = true; editor_goto_line(ui->editor, line); }
                    layout(ui);
                }
                return 0;
            }
            if (hdr->hwndFrom == ui->knowledge_tree && hdr->code == TVN_GETINFOTIPW) {
                auto* tip = reinterpret_cast<NMTVGETINFOTIPW*>(lparam);
                const auto* node = reinterpret_cast<TreeNode*>(tip->lParam);
                if (node && !node->path.empty()) wcsncpy_s(tip->pszText, tip->cchTextMax, node->path.c_str(), _TRUNCATE);
                return 0;
            }
            if ((hdr->hwndFrom == ui->tree || hdr->hwndFrom == ui->knowledge_tree) && hdr->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMTVCUSTOMDRAW*>(lparam);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const bool sel = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
                    cd->clrText = theme().text;
                    cd->clrTextBk = sel ? theme().selected : theme().navigation;
                    return CDRF_NOTIFYPOSTPAINT;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
                    if (cd->nmcd.uItemState & CDIS_SELECTED) {
                        RECT r = cd->nmcd.rc;
                        r.right = r.left + 3;
                        fill_rect(cd->nmcd.hdc, r, theme().amber);
                    }
                    return CDRF_DODEFAULT;
                }
            }
            if ((hdr->hwndFrom == ui->tree || hdr->hwndFrom == ui->knowledge_tree) && hdr->code == TVN_DELETEITEMW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                delete reinterpret_cast<TreeNode*>(nmtv->itemOld.lParam);
            }
            if ((hdr->hwndFrom == ui->tree || hdr->hwndFrom == ui->knowledge_tree) && hdr->code == TVN_ITEMEXPANDINGW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                auto* node = reinterpret_cast<TreeNode*>(nmtv->itemNew.lParam);
                if (node && node->dir && !node->loaded && (nmtv->action & TVE_EXPAND)) {
                    node->loaded = true;
                    if (node->kind == TreeNode::Kind::KnowledgeSource ||
                        node->kind == TreeNode::Kind::KnowledgeEntry) {
                        // Top-level (or nested) listing under a knowledge source — one level.
                        // Cascade: skip No Access (and other !readable) children.
                        fill_dir(hdr->hwndFrom, nmtv->itemNew.hItem, node->path, L"", TreeNode::Kind::KnowledgeEntry,
                                 node->source_id, &ui->knowledge);
                    } else if (node->kind != TreeNode::Kind::KnowledgeHeader) {
                        fill_dir(hdr->hwndFrom, nmtv->itemNew.hItem, node->path, L"");
                    }
                }
            }
            if ((hdr->hwndFrom == ui->tree || hdr->hwndFrom == ui->knowledge_tree) && hdr->code == TVN_SELCHANGEDW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                auto* node = reinterpret_cast<TreeNode*>(nmtv->itemNew.lParam);
                if (node && !node->dir && !node->path.empty()) {
                    open_document(ui, node->path, false);
                }
            }
            if ((hdr->hwndFrom == ui->tree || hdr->hwndFrom == ui->knowledge_tree) && hdr->code == NM_DBLCLK) {
                HTREEITEM sel = TreeView_GetSelection(hdr->hwndFrom);
                if (sel) {
                    TVITEMW it{};
                    it.mask = TVIF_PARAM;
                    it.hItem = sel;
                    if (TreeView_GetItem(hdr->hwndFrom, &it)) {
                        auto* node = reinterpret_cast<TreeNode*>(it.lParam);
                        if (node && !node->dir && !node->path.empty()) {
                            open_document(ui, node->path, true);
                        }
                    }
                }
            }
            if (hdr->hwndFrom == ui->tabs && hdr->code == NM_CLICK) {
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(ui->tabs, &p);
                const int n = TabCtrl_GetItemCount(ui->tabs);
                for (int i = 0; i < n; ++i) {
                    RECT tr{};
                    TabCtrl_GetItemRect(ui->tabs, i, &tr);
                    if (p.x >= tr.right - 18 && p.x < tr.right && p.y >= tr.top && p.y < tr.bottom) {
                        close_tab(ui, i);
                        break;
                    }
                }
            }
            if (hdr->hwndFrom == ui->tabs && hdr->code == TCN_SELCHANGE) {
                const int i = TabCtrl_GetCurSel(ui->tabs);
                show_doc(ui, i);
            }
            if (hdr->hwndFrom == ui->chat_tabs && hdr->code == TCN_SELCHANGE) {
                const int i = TabCtrl_GetCurSel(ui->chat_tabs);
                open_chat_index(ui, i);
            }
            if (hdr->hwndFrom == ui->editor) {
                if (hdr->code == SCN_MODIFIED) {
                    auto* scn = reinterpret_cast<SCNotification*>(lparam);
                    const int mods = SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT;
                    if (!ui->suppress_edit && ui->active_doc >= 0 && (scn->modificationType & mods)) {
                        ui->docs[ui->active_doc].dirty = true;
                        ui->docs[ui->active_doc].preview = false;
                        refresh_tabs(ui);
                        refresh_editor_status(ui);
                    }
                } else if (hdr->code == SCN_UPDATEUI) {
                    refresh_editor_status(ui);
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (id == ID_WORKFLOW) {
                ui_kit::select_handle_command(ui->workflow, static_cast<WORD>(code));
            } else if (id == ID_KNOWLEDGE_HEADER) {
                ui->knowledge_collapsed = !ui->knowledge_collapsed;
                layout(ui);
            } else if (id == ID_KNOWLEDGE_MANAGE) {
                open_settings_section(ui, SettingsSection::Knowledge);
            } else if (id == ID_SIGNIN) {
                ui->session.login_chatgpt();
                refresh_chrome(ui);
            } else if (id == ID_SIGNOUT) {
                ui->session.logout();
            } else if (id == ID_NEW) {
                ui->restore_chat_pending = false;
                ui->session.set_draft(ui->session.active_thread_id, utf8(get_window_text(ui->composer)));
                ui->session.new_conversation();
                close_attachment_windows(ui);
                ui->message_attachments.clear();
                SetWindowTextW(ui->composer, L"");
                SetWindowTextW(ui->transcript, L"");
                chat_log_clear(ui->chat_log);
                ui->shown_stream.clear();
                ui->markdown_stream_start = -1;
                ui->agent_heading_pending = false;
                ui->session.stream_buffer.clear();
            } else if (id == ID_SEND) {
                if (ui->session.active_thread_busy() || ui->session.claude_generating()) {
                    ui->session.cancel_turn();
                    refresh_chrome(ui);
                } else {
                    do_send(ui);
                }
            } else if (id == ID_ACCOUNT) {
                POINT p{};
                GetCursorPos(&p);
                HMENU m = CreatePopupMenu();
                if (ui->session.account.signed_in) {
                    const std::wstring email = utf16(ui->session.account.email);
                    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, email.c_str());
                    if (!ui->session.account.plan.empty()) {
                        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, utf16(ui->session.account.plan).c_str());
                    }
                    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(m, MF_STRING, ID_SIGNOUT, L"Sign out");
                } else {
                    AppendMenuW(m, MF_STRING, ID_SIGNIN, L"Sign in");
                }
                TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(m);
            } else if (id == ID_CANCEL) {
                ui->session.cancel_turn();
                refresh_chrome(ui);
            } else if (id == ID_OPEN_FOLDER || id == ID_EMPTY_OPEN_FOLDER) {
                do_open_folder(ui);
            } else if (id == ID_OPEN_FILE || id == ID_EMPTY_OPEN_FILE) {
                do_open_file(ui);
            } else if (id == ID_PROJECT && code == BN_CLICKED) {
                PostMessageW(hwnd, WM_SCYLLA_OPEN_SEL, ID_PROJECT, reinterpret_cast<LPARAM>(ui->project));
            } else if (id == ID_MODELS && code == BN_CLICKED) {
                PostMessageW(hwnd, WM_SCYLLA_OPEN_SEL, ID_MODELS, reinterpret_cast<LPARAM>(ui->models));
            } else if (id == ID_SCOPE && code == BN_CLICKED) {
                PostMessageW(hwnd, WM_SCYLLA_OPEN_SEL, ID_SCOPE, reinterpret_cast<LPARAM>(ui->scope));
            } else if (id == ID_COMPOSER && code == EN_CHANGE) {
                ShowWindow(ui->composer_cue, get_window_text(ui->composer).empty() ? SW_SHOW : SW_HIDE);
                refresh_composer_token_styles(ui);
                // Grow before the popup is placed; the popup anchors to the composer rect.
                if (sync_composer_growth(ui)) layout(ui);
                update_mcp_alias_popup(ui);
            } else if (id == ID_MCP_ALIAS_POPUP && (code == LBN_SELCHANGE || code == LBN_DBLCLK)) {
                apply_mcp_alias_popup(ui);
            } else if (reinterpret_cast<HWND>(lparam) == ui->composer_cue && code == STN_CLICKED) {
                SetFocus(ui->composer);
            } else if (id == ID_SAVE) {
                save_active(ui);
            } else if (id == ID_REFRESH_FILES) {
                rebuild_tree(ui);
                layout(ui);
            } else if (id == ID_MARKDOWN_TOGGLE && ui->active_doc >= 0) {
                pull_editor(ui);
                auto& doc = ui->docs[ui->active_doc];
                doc.markdown_source = !doc.markdown_source;
                if (!doc.markdown_source) ui_kit::set_markdown(ui->markdown_view, doc.text);
                layout(ui);
                SetFocus(doc.markdown_source ? ui->editor : ui->markdown_view);
            } else if (id == ID_CLOSE_TAB) {
                close_tab(ui, ui->active_doc);
            } else if (id == ID_TOGGLE_FIND && code == BN_CLICKED) {
                ui->find_open = !ui->find_open;
                layout(ui);
                InvalidateRect(ui->find_toggle, nullptr, TRUE);
                if (ui->find_open) {
                    SetFocus(ui->find);
                } else if (ui->editor) {
                    SetFocus(ui->editor);
                }
            } else if (id == ID_FIND && (code == 0 || code == 1)) {
                // Menu/accelerator only — ignore EN_SETFOCUS/EN_KILLFOCUS (those were re-focusing find and blocking the toggle).
                if (!ui->find_open) {
                    ui->find_open = true;
                    layout(ui);
                    InvalidateRect(ui->find_toggle, nullptr, TRUE);
                }
                SetFocus(ui->find);
            } else if (id == ID_GOTO) {
                if (!ui->find_open) {
                    ui->find_open = true;
                    layout(ui);
                    InvalidateRect(ui->find_toggle, nullptr, TRUE);
                }
                do_goto(ui);
            } else if (id == ID_REPLACE) {
                do_replace(ui);
            } else if (id == ID_FOCUS_COMPOSER) {
                SetFocus(ui->composer);
            } else if (id == ID_ADD_FILE) {
                add_file_chip(ui);
            } else if (id == ID_ADD_SEL) {
                add_sel_chip(ui);
            } else if (id == ID_CLEAR_CTX) {
                ui->session.context_chips.clear();
                refresh_ctx(ui);
                layout(ui);
            } else if (id == ID_EXIT) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            } else if (id == ID_EDIT_UNDO || id == ID_EDIT_REDO || id == ID_EDIT_CUT || id == ID_EDIT_COPY ||
                       id == ID_EDIT_PASTE || id == ID_EDIT_SELECT_ALL) {
                edit_target_action(ui, id);
            } else if (id == ID_VIEW_WRAP) {
                ui->session.settings.word_wrap = !ui->session.settings.word_wrap;
                apply_editor_prefs(ui);
                save_settings(ui->session.paths.settings_path, ui->session.settings);
            } else if (id == ID_VIEW_WHITESPACE) {
                ui->session.settings.show_whitespace = !ui->session.settings.show_whitespace;
                apply_editor_prefs(ui);
                save_settings(ui->session.paths.settings_path, ui->session.settings);
            } else if (id == ID_TOGGLE_TERMINAL) {
                toggle_terminal(ui);
            } else if (id == ID_NEW_TERMINAL) {
                request_new_terminal(ui);
            } else if (id == ID_PANEL_PROBLEMS) {
                show_panel_surface(ui, PanelSurface::Problems);
            } else if (id == ID_PANEL_OUTPUT) {
                show_panel_surface(ui, PanelSurface::Output);
            } else if (id == ID_PANEL_PORTS) {
                show_panel_surface(ui, PanelSurface::Ports);
            } else if (id == ID_ACCESS_SHOW || id == ID_PERM_INFO) {
                show_content_view(ui, ContentView::Access);
            } else if (id == ID_ACCESS_FOLDERS) {
                do_add_authorized_folders(ui);
            } else if (id == ID_ACCESS_KEYRING) {
                show_content_view(ui, ContentView::Keyring);
            } else if (id == ID_HELP_ABOUT) {
                show_about(hwnd);
            } else if (id == ID_HELP_SHORTCUTS) {
                show_content_view(ui, ContentView::Shortcuts);
            } else if (id == ID_HELP_DIAG) {
                show_content_view(ui, ContentView::Diagnostics);
            } else if (id == ID_HELP_GETTING_STARTED) {
                show_content_view(ui, ContentView::GettingStarted);
            } else if (id == ID_CONTENT_BACK) {
                go_back_content(ui);
            } else if (id == ID_CONTENT_NAV && code == LBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(ui->content_nav, LB_GETCURSEL, 0, 0));
                show_content_view(ui, ContentView::Settings, settings_section_from_nav(sel));
            } else if (const auto prov_action =
                           ui->providers_ui.handle_command(id, code, ui->session, hwnd);
                       prov_action != ProvidersSettingsAction::None) {
                using PA = ProvidersSettingsAction;
                if (prov_action == PA::OpenAISignIn) {
                    ui->session.login_chatgpt();
                } else if (prov_action == PA::OpenAISignOut) {
                    ui->session.logout();
                } else if (prov_action == PA::ClaudeApiKey) {
                    const std::wstring key = prompt_text(hwnd, L"Anthropic API key", L"");
                    if (!key.empty()) {
                        if (claude_api_key_save(key)) {
                            MessageBoxW(hwnd, L"Claude API key saved to Windows Credential Manager.", L"Claude API",
                                        MB_OK | MB_ICONINFORMATION);
                            ui->session.refresh_claude_api_models();
                        } else {
                            MessageBoxW(hwnd, L"Could not save API key to Credential Manager.", L"Claude API",
                                        MB_OK | MB_ICONERROR);
                        }
                    }
                } else if (prov_action == PA::ClaudeApiDisconnect) {
                    claude_api_key_clear();
                    if (ui->session.settings.default_provider == "claude-api") {
                        ui->session.set_default_provider("openai");
                    }
                    ui->session.sync_claude_models();
                } else if (prov_action == PA::OpenAiApiKey) {
                    const std::wstring key = prompt_text(hwnd, L"OpenAI API key", L"");
                    if (!key.empty()) {
                        if (openai_api_key_save(key)) {
                            MessageBoxW(hwnd, L"OpenAI API key saved to Windows Credential Manager.", L"OpenAI API",
                                        MB_OK | MB_ICONINFORMATION);
                            ui->session.refresh_openai_api_models();
                        } else {
                            MessageBoxW(hwnd, L"Could not save API key to Credential Manager.", L"OpenAI API",
                                        MB_OK | MB_ICONERROR);
                        }
                    }
                } else if (prov_action == PA::OpenAiApiDisconnect) {
                    openai_api_key_clear();
                    if (ui->session.settings.default_provider == "openai-api") {
                        ui->session.set_default_provider("openai");
                    }
                    ui->session.sync_claude_models();
                } else if (prov_action == PA::ClaudeCodeLogin) {
                    std::wstring err;
                    if (!claude_code_login_launch(&err)) {
                        MessageBoxW(hwnd, err.c_str(), L"Claude Code", MB_OK | MB_ICONWARNING);
                    } else {
                        ui->claude_auth_polls = 45;
                        MessageBoxW(hwnd,
                                    L"Complete Claude Code login in the console/browser.\n"
                                    L"Settings will refresh when the session is detected.",
                                    L"Claude Code", MB_OK | MB_ICONINFORMATION);
                    }
                    claude_code_session_status(true);
                } else if (prov_action == PA::ClaudeDisconnect) {
                    std::wstring err;
                    if (claude_code_session_status(true).logged_in) {
                        if (!claude_code_logout(&err) && !err.empty()) {
                            MessageBoxW(hwnd, err.c_str(), L"Claude Code", MB_OK | MB_ICONWARNING);
                        }
                    }
                    claude_clear_connection_state();
                    if (ui->session.settings.default_provider == "claude") {
                        ui->session.set_default_provider("openai");
                    }
                } else if (prov_action == PA::RefreshOpenAI) {
                    if (ui->session.account.signed_in) {
                        ui->session.refresh_models();
                    }
                } else if (prov_action == PA::RefreshClaude) {
                    ui->session.refresh_claude_model_catalog(true);
                } else if (prov_action == PA::RefreshOpenAiApi) {
                    ui->session.refresh_openai_api_models();
                } else if (prov_action == PA::RefreshClaudeApi) {
                    ui->session.refresh_claude_api_models();
                }
                if (prov_action == PA::PersistAndSync || prov_action == PA::OpenAISignIn ||
                    prov_action == PA::OpenAISignOut || prov_action == PA::ClaudeApiKey ||
                    prov_action == PA::ClaudeApiDisconnect || prov_action == PA::OpenAiApiKey ||
                    prov_action == PA::OpenAiApiDisconnect || prov_action == PA::ClaudeCodeLogin ||
                    prov_action == PA::ClaudeDisconnect || prov_action == PA::RefreshOpenAI ||
                    prov_action == PA::RefreshClaude || prov_action == PA::RefreshOpenAiApi ||
                    prov_action == PA::RefreshClaudeApi) {
                    save_settings(ui->session.paths.settings_path, ui->session.settings);
                    ui->session.sync_claude_models();
                    refresh_settings_pane(ui);
                    refresh_models(ui);
                    refresh_chrome(ui);
                }
                if (prov_action == PA::Relayout) {
                    layout(ui);
                    refresh_settings_pane(ui);
                }
            } else if (id == ID_SET_CODEX) {
                browse_codex(ui);
            } else if (id == ID_SET_COPY_RUNTIME) {
                PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_COPY_RUNTIME, 0), 0);
            } else if (id == ID_UI_GALLERY ||
                       (ui->ui_gallery.visible() &&
                        ui->ui_gallery.handle_command(static_cast<WORD>(id), code))) {
                if (id == ID_UI_GALLERY) {
                    ui->ui_gallery.show(true);
                    if (ui->set_codex) {
                        ShowWindow(ui->set_codex, SW_HIDE);
                    }
                    if (ui->set_copy_runtime) {
                        ShowWindow(ui->set_copy_runtime, SW_HIDE);
                    }
                    if (ui->set_ui_gallery) {
                        ShowWindow(ui->set_ui_gallery, SW_HIDE);
                    }
                    if (ui->content_body) {
                        ShowWindow(ui->content_body, SW_HIDE);
                    }
                }
                layout(ui);
            } else if (id == ID_SET_WRAP) {
                ui->session.settings.word_wrap = !ui->session.settings.word_wrap;
                apply_editor_prefs(ui);
                save_settings(ui->session.paths.settings_path, ui->session.settings);
                InvalidateRect(ui->set_wrap, nullptr, TRUE);
            } else if (id == ID_SET_WHITESPACE) {
                ui->session.settings.show_whitespace = !ui->session.settings.show_whitespace;
                apply_editor_prefs(ui);
                save_settings(ui->session.paths.settings_path, ui->session.settings);
                InvalidateRect(ui->set_whitespace, nullptr, TRUE);
            } else if (id == ID_SET_ENTER_SENDS) {
                ui->session.settings.enter_sends = !ui->session.settings.enter_sends;
                save_settings(ui->session.paths.settings_path, ui->session.settings);
                InvalidateRect(ui->set_enter_sends, nullptr, TRUE);
            } else if (id == ID_GS_OPEN_FOLDER) {
                do_open_folder(ui);
            } else if (id == ID_GS_PROVIDERS) {
                open_settings_section(ui, SettingsSection::Providers);
            } else if (id == Cmd_SecTabOverview) {
                set_security_subpage(ui, SecuritySubpage::Overview);
            } else if (id == Cmd_SecTabKeyring) {
                set_security_subpage(ui, SecuritySubpage::Keyring);
            } else if (id == Cmd_SecTabEnvironments) {
                set_security_subpage(ui, SecuritySubpage::Environments);
            } else if (id == Cmd_SecTabConnections) {
                set_security_subpage(ui, SecuritySubpage::Connections);
            } else if (id == Cmd_SecTabPolicy) {
                set_security_subpage(ui, SecuritySubpage::Policy);
            } else if (const auto sec_act = ui->security_overview.on_command(static_cast<WORD>(id), code);
                       sec_act != SecurityOverviewAction::None) {
                if (sec_act == SecurityOverviewAction::GotoKeyring) {
                    set_security_subpage(ui, SecuritySubpage::Keyring);
                } else if (sec_act == SecurityOverviewAction::GotoEnvironments) {
                    set_security_subpage(ui, SecuritySubpage::Environments);
                } else if (sec_act == SecurityOverviewAction::GotoPolicy) {
                    set_security_subpage(ui, SecuritySubpage::Policy);
                } else if (sec_act == SecurityOverviewAction::LockKeyring) {
                    ui->keyring_ui.lock_now();
                    refresh_settings_data(ui);
                    refresh_chrome(ui);
                    layout(ui);
                }
            } else if (ui->security_policy.on_command(static_cast<WORD>(id), code, hwnd, ui->session.settings,
                                                      ui->session.paths.settings_path)) {
                // Policy edits change how a protected terminal launch behaves, so the Overview
                // summary and any open status text refresh with it.
                layout(ui);
            } else if (id == Cmd_StatusEnv && code == BN_CLICKED) {
                choose_status_environment(ui);
            } else if (id == Cmd_TreeProjectEnv) {
                open_settings_section(ui, SettingsSection::Security);
                set_security_subpage(ui, SecuritySubpage::Environments);
            } else if (id == Cmd_TreeProjectSecrets) {
                open_settings_section(ui, SettingsSection::Security);
                set_security_subpage(ui, SecuritySubpage::Keyring);
            } else if (id == Cmd_TreeProjectSecurity) {
                open_settings_section(ui, SettingsSection::Security);
                set_security_subpage(ui, SecuritySubpage::Overview);
            } else if (ui->terminal_ui.on_command(static_cast<WORD>(id), code, hwnd, ui->terminal_profiles,
                                                  ui->session.settings, ui->session.paths.settings_path,
                                                  ui->session.paths.terminals_path)) {
                ui->workbench_panel.set_enabled_profiles(enabled_terminal_profiles(ui->terminal_profiles));
                layout(ui);
            } else if (ui->knowledge_ui.on_command(static_cast<WORD>(id), code, hwnd, ui->knowledge,
                                                   ui->session.paths.knowledge_path,
                                                   ui->session.store.active_project_id)) {
                sync_knowledge_agent_grants(ui);
                rebuild_tree(ui);
                layout(ui);
            } else if (ui->keyring_ui.handle_command(static_cast<int>(id), hwnd, code)) {
                layout(ui);
                refresh_chrome(ui);
            } else if (ui->environment_ui.on_command(static_cast<WORD>(id), code, hwnd, ui->environments,
                                                     ui->keyring_ui.keyring(),
                                                     ui->session.store.active_project_id,
                                                     ui->session.paths.environments_path)) {
                if (ui->environment_ui.take_nav_request() == EnvUiNavRequest::OpenKeyring) {
                    set_security_subpage(ui, SecuritySubpage::Keyring);
                } else {
                    layout(ui);
                    refresh_chrome(ui);
                }
            } else if (ui->connections_ui.on_command(static_cast<WORD>(id), code, hwnd, ui->connections,
                                                     ui->keyring_ui.keyring(),
                                                     ui->session.store.active_project_id,
                                                     ui->session.paths.connections_path)) {
                switch (ui->connections_ui.take_request()) {
                    case ConnUiRequest::OpenKeyring:
                        set_security_subpage(ui, SecuritySubpage::Keyring);
                        break;
                    case ConnUiRequest::TestConnection:
                        start_connection_test(ui, ui->connections_ui.pending_test_alias());
                        layout(ui);
                        refresh_chrome(ui);
                        break;
                    case ConnUiRequest::None:
                        layout(ui);
                        refresh_chrome(ui);
                        break;
                }
            } else if (ui->mcp_ui.handle_command(static_cast<int>(id), code, ui->mcp, ui->session.paths.mcp_path,
                                                 hwnd)) {
                layout(ui);
            } else if (ui->strata_ui.handle_command(id, code, &ui->strata_bridge, &ui->strata,
                                                    ui->session.store.active_project_id,
                                                    ui->session.paths.strata_path, hwnd)) {
                // Status / search / binding updated in-place; no relayout needed.
            } else if (id == ID_CTX && code == LBN_DBLCLK) {
                const int sel = static_cast<int>(SendMessageW(ui->ctx, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ui->session.context_chips.size())) {
                    ui->session.context_chips.erase(ui->session.context_chips.begin() + sel);
                    refresh_ctx(ui);
                }
            } else if (id == ID_PIN_CHAT) {
                const int sel = static_cast<int>(SendMessageW(ui->threads, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ui->thread_ids.size())) {
                    if (auto* c = ui->session.store.by_thread(ui->thread_ids[sel])) {
                        c->pinned = !c->pinned;
                        persist_store(ui);
                        refresh_threads(ui);
                    }
                }
            } else if (id == ID_RENAME_CHAT) {
                const int sel = static_cast<int>(SendMessageW(ui->threads, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ui->thread_ids.size())) {
                    rename_chat_id(ui, ui->thread_ids[sel]);
                }
            } else if (id == ID_ARCHIVE_CHAT) {
                const int sel = static_cast<int>(SendMessageW(ui->threads, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ui->thread_ids.size())) {
                    const std::string tid = ui->thread_ids[sel];
                    if (ui->session.store.remove_thread(tid)) {
                        if (ui->session.active_thread_id == tid) {
                            close_attachment_windows(ui);
                            ui->message_attachments.clear();
                            ui->session.active_thread_id.clear();
                            ui->session.settings.last_thread_id.clear();
                            SetWindowTextW(ui->transcript, L"");
                            chat_log_clear(ui->chat_log);
                            SetWindowTextW(ui->composer, L"");
                            ui->shown_stream.clear();
                            ui->markdown_stream_start = -1;
                            ui->agent_heading_pending = false;
                            ui->session.stream_buffer.clear();
                        }
                        persist_store(ui);
                        refresh_threads(ui);
                        if (ui->session.active_thread_id.empty() && !ui->thread_ids.empty()) {
                            open_chat_index(ui, 0);
                        } else {
                            refresh_chrome(ui);
                            layout(ui);
                        }
                    }
                }
            } else if (id == ID_EDITOR && code == EN_CHANGE) {
                // Scintilla notifies via SCN_MODIFIED; RichEdit path unused.
            } else if (id == ID_TOGGLE_FILES) {
                toggle_mode(ui->session.settings.files_mode, ui->panes.show_files);
                layout(ui);
            } else if (id == ID_TOGGLE_HISTORY) {
                toggle_mode(ui->session.settings.history_mode, ui->panes.show_history);
                layout(ui);
            } else if (id == ID_FOCUS) {
                if (!ui->session.settings.focus_editor) {
                    ui->focus_restore_files = ui->panes.show_files;
                    ui->focus_restore_history = ui->panes.show_history;
                    ui->session.settings.focus_editor = true;
                } else {
                    ui->session.settings.focus_editor = false;
                    ui->session.settings.files_mode = ui->focus_restore_files ? 1 : 2;
                    ui->session.settings.history_mode = ui->focus_restore_history ? 1 : 2;
                }
                layout(ui);
            } else if (id == ID_TAB_EDITOR) {
                ui->narrow_tab = 0;
                layout(ui);
            } else if (id == ID_TAB_AGENT) {
                ui->narrow_tab = 1;
                layout(ui);
            } else if (id == ID_SETTINGS || id == ID_AI_PROVIDERS) {
                open_settings_section(ui, id == ID_AI_PROVIDERS ? SettingsSection::Providers
                                                                : SettingsSection::Providers);
            } else if (id == ID_COPY_RUNTIME) {
                const std::wstring path = ui->session.runtime_path.empty() ? ui->session.settings.codex_path
                                                                               : ui->session.runtime_path;
                if (OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
                    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (mem) {
                        memcpy(GlobalLock(mem), path.c_str(), bytes);
                        GlobalUnlock(mem);
                        SetClipboardData(CF_UNICODETEXT, mem);
                    }
                    CloseClipboard();
                }
            } else if (id == ID_CODEX_EXE) {
                browse_codex(ui);
            } else if (id == ID_FILTER && code == EN_CHANGE) {
                rebuild_tree(ui);
            } else if (id == ID_SEARCH && code == EN_CHANGE) {
                refresh_threads(ui);
            } else if (id == ID_THREADS && code == LBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(ui->threads, LB_GETCURSEL, 0, 0));
                open_chat_index(ui, sel);
            }
            return 0;
        }
        case WM_SCYLLA_CLOSE_SEL:
            close_selector(ui);
            return 0;
        case WM_SCYLLA_OPEN_SEL:
            open_selector(ui, reinterpret_cast<HWND>(lparam), static_cast<int>(wparam));
            return 0;
        case WM_SCYLLA_PICK_SEL: {
            const int owner = static_cast<int>(wparam);
            const int sel = static_cast<int>(lparam);
            close_selector(ui);
            apply_selector(ui, owner, sel);
            return 0;
        }
        case WM_SCYLLA_CLAUDE_DONE: {
            auto* r = reinterpret_cast<ClaudePrintResult*>(lparam);
            if (r) {
                ui->session.complete_claude_print(r->ok, r->text, r->error);
                delete r;
                apply_stream(ui);
                refresh_threads(ui);
                refresh_chrome(ui);
                layout(ui);
            }
            return 0;
        }
        case WM_SCYLLA_CLAUDE_MODELS: {
            ui->session.sync_claude_models();
            refresh_models(ui);
            // Open popup stays frozen (no refill).
            return 0;
        }
        case WM_SCYLLA_TERMINAL_OUT:
            poll_terminal(ui);
            return 0;
        case WM_SCYLLA_MCP_AUTH:
            handle_mcp_auth_result(ui, reinterpret_cast<McpAuthPosted*>(lparam));
            return 0;
        case WM_SCYLLA_TEST_RESULT: {
            std::unique_ptr<ConnectionTestResult> result(reinterpret_cast<ConnectionTestResult*>(lparam));
            if (ui->connection_test.joinable()) ui->connection_test.join();
            ui->connection_test_running = false;
            if (result) {
                ui->connections_ui.report_test_result(result->ok, result->message);
            }
            return 0;
        }
        case WM_SCYLLA_BROKER_PREPARE:
            broker_prepare_on_ui(ui, reinterpret_cast<BrokerPrepareBridge*>(lparam));
            return 0;
        case WM_SCYLLA_RELAYOUT: {
            if (wparam == kRelayoutEnsurePanel) {
                ensure_terminal_panel(ui);
            }
            layout(ui);
            if (lparam != 0) {
                ui->terminal_sessions.focus_active();
                refresh_chrome(ui);
            }
            return 0;
        }
        case WM_SCYLLA_LINE: {
            auto* line = reinterpret_cast<std::string*>(lparam);
            if (line) {
                const int model_epoch = ui->session.models_epoch;
                const int chats_epoch = ui->session.chats_epoch;
                const std::size_t thread_n = ui->session.threads.size();
                const std::size_t conv_n = ui->session.store.conversations.size();
                const std::string active_before = ui->session.active_thread_id;
                const auto state_before = ui->session.state;
                const auto activity_before = ui->session.activity.summary();
                const auto stream_before = ui->session.stream_buffer;
                ui->session.handle_line(*line);
                if (ui->session.chats_epoch != chats_epoch) refresh_threads(ui);
                if (ui->restore_chat_pending && ui->session.account.signed_in && ui->session.state == AppState::Ready) {
                    ui->restore_chat_pending = false;
                    if (ui->session.settings.restore_chat_on_start) {
                        const auto wanted = ui->session.settings.last_thread_id;
                        refresh_threads(ui);
                        const auto found = std::find(ui->thread_ids.begin(), ui->thread_ids.end(), wanted);
                        if (found != ui->thread_ids.end()) open_chat_index(ui, static_cast<int>(found - ui->thread_ids.begin()));
                    }
                }
                delete line;
                apply_stream(ui);
                if (ui->session.models_epoch != model_epoch) {
                    refresh_models(ui);
                    // Do not touch an open agent popup — snapshot is frozen until close.
                }
                if (ui->session.threads.size() != thread_n || ui->session.store.conversations.size() != conv_n ||
                    ui->session.active_thread_id != active_before) {
                    if (!ui->sel_list) {
                        refresh_threads(ui);
                        layout(ui);
                    }
                }
                if (!ui->sel_list && (ui->session.state != state_before ||
                    ui->session.activity.summary() != activity_before || ui->session.stream_buffer != stream_before)) {
                    refresh_chrome(ui);
                }
            }
            return 0;
        }
        case WM_DPICHANGED: {
            RECT* r = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            // Remap every descendant off the old handles before destroying them. apply_fonts()
            // only covers shell controls, so panel / keyring / environment children used to keep
            // pointing at deleted HFONTs after a monitor change.
            const HFONT old_fonts[] = {ui->font, ui->font_small, ui->font_semi, ui->font_title, ui->font_mono};
            ui->font = make_font_dip(hwnd, 13, false, L"Segoe UI Variable");
            ui->font_small = make_font_dip(hwnd, 12, false, L"Segoe UI");
            ui->font_semi = make_font_dip(hwnd, 14, true, L"Segoe UI Variable");
            ui->font_title = make_font_dip(hwnd, 18, true, L"Segoe UI Variable");
            ui->font_mono = make_font_dip(hwnd, 14, false, L"Cascadia Mono");
            const HFONT new_fonts[] = {ui->font, ui->font_small, ui->font_semi, ui->font_title, ui->font_mono};
            struct FontRemap {
                const HFONT* old_list;
                const HFONT* new_list;
                int count;
            } remap{old_fonts, new_fonts, 5};
            EnumChildWindows(
                hwnd,
                [](HWND child, LPARAM param) -> BOOL {
                    auto* rm = reinterpret_cast<FontRemap*>(param);
                    const HFONT cur = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
                    for (int i = 0; i < rm->count; ++i) {
                        if (cur == rm->old_list[i]) {
                            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(rm->new_list[i]), TRUE);
                            break;
                        }
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&remap));
            // Panels cache fonts for owner-draw painting and for controls created later.
            ui->workbench_panel.set_fonts(ui->font, ui->font_small);
            ui->keyring_ui.set_fonts(ui->font, ui->font_small, ui->font_semi);
            ui->environment_ui.set_fonts(ui->font, ui->font_small);
            ui->connections_ui.set_fonts(ui->font, ui->font_small);
            ui->security_overview.set_fonts(ui->font, ui->font_small);
            ui->security_policy.set_fonts(ui->font, ui->font_small);
            ui->providers_ui.set_fonts(ui->font, ui->font_small);
            ui->knowledge_ui.set_fonts(ui->font);
            ui->mcp_ui.set_fonts(ui->font);
            ui->strata_ui.set_fonts(ui->font);
            ui->terminal_ui.set_fonts(ui->font);
            for (HFONT f : old_fonts) {
                DeleteObject(f);
            }
            apply_fonts(ui);
            if (ui->editor && ui->active_doc >= 0) {
                editor_apply_chrome(ui->editor, ui->font_mono, 13, static_cast<int>(GetDpiForWindow(hwnd)));
            }
            layout(ui);
            return 0;
        }
        case WM_TIMER:
            if (wparam == 2) {
                ++ui->chat_busy_frame;
                if (ui->threads) for (std::size_t i = 0; i < ui->thread_ids.size(); ++i) {
                    if (!ui->session.thread_busy(ui->thread_ids[i])) continue;
                    RECT row{};
                    if (SendMessageW(ui->threads, LB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&row)) != LB_ERR)
                        InvalidateRect(ui->threads, &row, FALSE);
                }
                return 0;
            }
            ui->strata_ui.poll();
            if (ui->session.activity.busy) refresh_chrome(ui);
            if (wparam == 1) {
                check_external(ui);
                write_recovery(ui);
                poll_terminal(ui);
                {
                    // The status bar carries a global "Keyring: Unlocked / Locked" segment, so an
                    // auto-lock has to refresh chrome from any view — not only the Keyring page.
                    const bool was_unlocked = ui->keyring_ui.keyring().is_unlocked();
                    ui->keyring_ui.tick_autolock(15ull * 60ull * 1000ull);
                    const bool locked_now = was_unlocked && !ui->keyring_ui.keyring().is_unlocked();
                    if (locked_now) {
                        refresh_chrome(ui);
                        if (ui->content_view == ContentView::Keyring ||
                            (ui->content_view == ContentView::Settings &&
                             ui->settings_section == SettingsSection::Security)) {
                            layout(ui);
                        }
                    }
                }
                if (ui->claude_auth_polls > 0) {
                    --ui->claude_auth_polls;
                    if (ui->sel_list) {
                        // Don't thrash auth/catalog while any header menu is open.
                    } else {
                        const bool was = claude_code_session_status(false).logged_in;
                        const bool now = claude_code_session_status(true).logged_in;
                        if (now || ui->content_view == ContentView::Settings) {
                            ui->session.refresh_claude_model_catalog(now);
                            refresh_settings_pane(ui);
                            refresh_models(ui);
                            refresh_chrome(ui);
                        }
                        if (now && !was) {
                            ui->claude_auth_polls = 0;
                        }
                    }
                } else if (!ui->sel_list && ui->content_view == ContentView::Settings &&
                           ui->settings_section == SettingsSection::Providers) {
                    // Occasional refresh while Providers is open so OAuth from outside shows up.
                    static int settings_tick = 0;
                    if ((++settings_tick % 5) == 0) {  // every ~10s
                        claude_code_session_status(true);
                        ui->session.refresh_claude_model_catalog(false);
                        ui->session.sync_claude_models();
                        refresh_settings_pane(ui);
                        refresh_models(ui);
                        refresh_chrome(ui);
                    }
                }
                // MCP: one silent freshness check at a time; re-queue enabled HTTP connections ~5 min.
                pump_mcp_auth_queue(ui);
                if ((++ui->mcp_auth_tick % 150) == 0) {
                    queue_mcp_auth_checks(ui, false);
                }
            }
            return 0;
        case WM_CLOSE: {
            pull_editor(ui);
            write_recovery(ui);
            // Ask about unsaved work BEFORE tearing anything down: this prompt can be cancelled,
            // and killing the shells / locking the vault first left a running app with dead
            // terminals and a locked Keyring after the user chose Cancel.
            for (int i = 0; i < static_cast<int>(ui->docs.size()); ++i) {
                if (!ui->docs[i].dirty) {
                    continue;
                }
                std::wstring msg = L"Save " + folder_name(ui->docs[i].path) + L" before closing?";
                const int choice =
                    MessageBoxW(hwnd, msg.c_str(), L"Scylla Workbench", MB_YESNOCANCEL | MB_ICONWARNING);
                if (choice == IDCANCEL) {
                    return 0;
                }
                if (choice == IDYES) {
                    show_doc(ui, i);
                    save_active(ui);
                    if (ui->docs[i].dirty) {
                        return 0;
                    }
                }
            }
            // Past the point of no return — now shut down sessions and secrets.
            ui->terminal_sessions.destroy_all();
            close_attachment_windows(ui);
            // Before locking the vault: the broker holds a Keyring reference and its pipe workers
            // may be mid-SendMessage back to this thread.
            stop_query_broker(ui);
            ui->keyring_ui.lock_now();
            persist_window(ui);
            ui->session.stop_runtime();
            clear_composer_images(ui);
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            close_mcp_alias_popup(ui);
            ui->terminal_sessions.destroy_all();
            ui->workbench_panel.destroy();
            clear_composer_images(ui);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace  (keep splash helpers in scyllagpt; use theme() for colors)

struct SplashState {
    Gdiplus::Image* logo = nullptr;
    HBITMAP cache = nullptr;
    int cache_w = 0;
    int cache_h = 0;
    HINSTANCE inst = nullptr;
};

void render_splash_cache(SplashState* st, int w, int h) {
    if (!st || w <= 0 || h <= 0) {
        return;
    }
    if (st->cache && st->cache_w == w && st->cache_h == h) {
        return;
    }
    if (st->cache) {
        DeleteObject(st->cache);
        st->cache = nullptr;
    }
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT rc{0, 0, w, h};
    const Theme& th = theme();
    fill_rect(mem, rc, th.shell);
    if (st->logo && st->logo->GetLastStatus() == Gdiplus::Ok) {
        const int iw = static_cast<int>(st->logo->GetWidth());
        const int ih = static_cast<int>(st->logo->GetHeight());
        const int max_w = w * 3 / 5;
        const int max_h = h * 3 / 5;
        float scale = 1.0f;
        if (iw > 0 && ih > 0) {
            scale = (std::min)(static_cast<float>(max_w) / iw, static_cast<float>(max_h) / ih);
        }
        const int dw = (std::max)(1, static_cast<int>(iw * scale));
        const int dh = (std::max)(1, static_cast<int>(ih * scale));
        const int x = (w - dw) / 2;
        const int y = (h - dh) / 2 - 12;
        {
            Gdiplus::Graphics g(mem);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            g.DrawImage(st->logo, x, y, dw, dh);
        }
        SetBkMode(mem, TRANSPARENT);
        HFONT title_font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable");
        HFONT subtitle_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable");
        HGDIOBJ old_font = SelectObject(mem, title_font);
        SetTextColor(mem, kText);
        RECT title_rect{0, y + dh + 12, w, y + dh + 38};
        DrawTextW(mem, L"SCYLLA", -1, &title_rect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(mem, subtitle_font);
        SetTextColor(mem, kMuted);
        RECT subtitle_rect{0, y + dh + 38, w, y + dh + 62};
        DrawTextW(mem, L"workbench", -1, &subtitle_rect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(mem, old_font);
        DeleteObject(title_font);
        DeleteObject(subtitle_font);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    st->cache = bmp;
    st->cache_w = w;
    st->cache_h = h;
}

LRESULT CALLBACK splash_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* st = reinterpret_cast<SplashState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (st) {
                render_splash_cache(st, rc.right - rc.left, rc.bottom - rc.top);
            }
            if (st && st->cache) {
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(mem, st->cache);
                BitBlt(dc, 0, 0, st->cache_w, st->cache_h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            } else {
                fill_rect(dc, rc, theme().shell);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

Gdiplus::Image* load_splash_png(HINSTANCE inst) {
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(IDR_SPLASH_PNG), RT_RCDATA);
    if (!res) {
        return nullptr;
    }
    HGLOBAL data = LoadResource(inst, res);
    if (!data) {
        return nullptr;
    }
    const DWORD size = SizeofResource(inst, res);
    void* ptr = LockResource(data);
    if (!ptr || size == 0) {
        return nullptr;
    }
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hg) {
        return nullptr;
    }
    void* dest = GlobalLock(hg);
    if (!dest) {
        GlobalFree(hg);
        return nullptr;
    }
    std::memcpy(dest, ptr, size);
    GlobalUnlock(hg);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &stream)) || !stream) {
        GlobalFree(hg);
        return nullptr;
    }
    auto* img = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (!img || img->GetLastStatus() != Gdiplus::Ok) {
        delete img;
        return nullptr;
    }
    return img;
}

HWND show_splash(HINSTANCE inst, SplashState* state) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = splash_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // no erase flicker
    wc.lpszClassName = L"ScyllaGPTSplash";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassExW(&wc);

    state->inst = inst;
    state->logo = load_splash_png(inst);

    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    const int w = 420;
    const int h = 320;
    render_splash_cache(state, w, h);
    HWND hwnd =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_COMPOSITED, L"ScyllaGPTSplash", L"Scylla", WS_POPUP,
                        (sw - w) / 2, (sh - h) / 2, w, h, nullptr, nullptr, inst, state);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

void pump_splash(DWORD min_ms) {
    const DWORD start = GetTickCount();
    MSG msg{};
    while (GetTickCount() - start < min_ms) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
}

void destroy_splash(HWND splash, SplashState* state) {
    if (splash) {
        DestroyWindow(splash);
    }
    if (state) {
        if (state->cache) {
            DeleteObject(state->cache);
            state->cache = nullptr;
        }
        delete state->logo;
        state->logo = nullptr;
    }
}

int run_ui(HINSTANCE inst, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR gdip_token = 0;
    Gdiplus::GdiplusStartupInput gdip_in;
    Gdiplus::GdiplusStartup(&gdip_token, &gdip_in, nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES |
                                               ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    apply_dark_menus(nullptr);  // ForceDark app-wide before the first window/menu

    SplashState splash_state{};
    HWND splash = show_splash(inst, &splash_state);
    pump_splash(900);

    Ui ui{};
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kWindow);
    wc.lpszClassName = L"ScyllaGPTWindow";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!wc.hIcon) {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    RegisterClassExW(&wc);

    ACCEL acc[] = {
        {FVIRTKEY | FCONTROL, 'O', ID_OPEN_FILE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'O', ID_OPEN_FOLDER},
        {FVIRTKEY | FCONTROL, 'S', ID_SAVE},
        {FVIRTKEY | FCONTROL, 'W', ID_CLOSE_TAB},
        {FVIRTKEY | FCONTROL, 'F', ID_FIND},
        {FVIRTKEY | FCONTROL, 'H', ID_REPLACE},
        {FVIRTKEY | FCONTROL, 'G', ID_GOTO},
        {FVIRTKEY | FCONTROL, VK_OEM_3, ID_TOGGLE_TERMINAL},  // Ctrl+`
        {FVIRTKEY | FCONTROL | FALT, 'A', ID_FOCUS_COMPOSER},
        {FVIRTKEY | FCONTROL | FALT, 'H', ID_TOGGLE_HISTORY},
        {FVIRTKEY | FCONTROL | FALT, 'E', ID_TOGGLE_FILES},
        {FVIRTKEY | FCONTROL | FALT, VK_RETURN, ID_FOCUS},
        {FVIRTKEY | FCONTROL | FALT, 'N', ID_NEW},
        // No bare Esc accelerator: it outranked every focused control, so pressing Esc inside a
        // form discarded the input and exited the section. Esc is routed contextually in
        // WM_KEYDOWN / WM_SK_FIELD_CANCEL instead.
    };
    HACCEL accel = CreateAcceleratorTableW(acc, static_cast<int>(sizeof(acc) / sizeof(acc[0])));

    ui.session.paths = make_paths();
    ui.session.settings = load_settings(ui.session.paths.settings_path);
    int x = ui.session.settings.window.x;
    int y = ui.session.settings.window.y;
    int w = ui.session.settings.window.w;
    int h = ui.session.settings.window.h;
    if (w < 1100) {
        w = 1440;
        h = 860;
    }
    clamp_on_screen(x, y, w, h);

HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"ScyllaGPTWindow", L"Scylla", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr, inst,
                                  &ui);
    if (!hwnd) {
        destroy_splash(splash, &splash_state);
        if (gdip_token) {
            Gdiplus::GdiplusShutdown(gdip_token);
        }
        CoUninitialize();
        return 1;
    }
    // Hide splash before revealing main to avoid stacked-window flicker.
    destroy_splash(splash, &splash_state);
    splash = nullptr;
    ShowWindow(hwnd, ui.session.settings.window.maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        const auto* terminal = ui.terminal_sessions.active_session();
        const bool terminal_input = terminal && terminal->host && msg.hwnd == terminal->host->hwnd()
            && msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST;
        if (terminal_input || (!TranslateAcceleratorW(hwnd, accel, &msg) && !IsDialogMessageW(hwnd, &msg))) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyAcceleratorTable(accel);
    if (ui.bg) {
        DeleteObject(ui.bg);
    }
    if (ui.files) {
        DeleteObject(ui.files);
    }
    if (ui.editor_br) {
        DeleteObject(ui.editor_br);
    }
    if (ui.agent_br) {
        DeleteObject(ui.agent_br);
    }
    if (ui.history_br) {
        DeleteObject(ui.history_br);
    }
    if (ui.input_br) {
        DeleteObject(ui.input_br);
    }
    if (ui.font) {
        DeleteObject(ui.font);
    }
    if (ui.font_small) {
        DeleteObject(ui.font_small);
    }
    if (ui.font_semi) {
        DeleteObject(ui.font_semi);
    }
    if (ui.font_title) {
        DeleteObject(ui.font_title);
    }
    if (ui.font_mono) {
        DeleteObject(ui.font_mono);
    }
    if (gdip_token) {
        Gdiplus::GdiplusShutdown(gdip_token);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

}  // namespace scyllagpt
