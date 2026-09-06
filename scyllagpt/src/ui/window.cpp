#include "scyllagpt/window.h"
#include "scyllagpt/resource.h"

#include "scyllagpt/document.h"
#include "scyllagpt/editor_host.h"
#include "scyllagpt/commands.h"
#include "scyllagpt/layout.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/provider.h"
#include "scyllagpt/session.h"
#include "scyllagpt/store.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/utf.h"

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
#include <string>
#include <string_view>
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
constexpr wchar_t kSelectorClass[] = L"ScyllaGPTSelectorPopup";

constexpr COLORREF kWindow = RGB(0x14, 0x16, 0x18);
constexpr COLORREF kFiles = RGB(0x18, 0x1A, 0x1D);
constexpr COLORREF kEditor = RGB(0x1C, 0x1E, 0x21);
constexpr COLORREF kAgent = RGB(0x1C, 0x1E, 0x21);
constexpr COLORREF kHistory = RGB(0x18, 0x1A, 0x1D);
constexpr COLORREF kInput = RGB(0x23, 0x26, 0x2A);
constexpr COLORREF kText = RGB(0xE4, 0xE7, 0xEB);
constexpr COLORREF kMuted = RGB(0x94, 0x9D, 0xA8);
constexpr COLORREF kAccent = RGB(0xE6, 0x94, 0x05);
constexpr COLORREF kYouBody = RGB(0xA8, 0xB0, 0xBA);  // dimmer than white "You" heading
constexpr COLORREF kAsst = RGB(0xE4, 0xE7, 0xEB);
constexpr COLORREF kBorder = RGB(0x30, 0x33, 0x38);
constexpr COLORREF kRule = RGB(0x28, 0x2B, 0x30);  // very subtle divider

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
    ID_CLEAR_CTX = Cmd_ClearCtx,
    ID_ACCESS_SHOW = Cmd_AccessShow,
    ID_ACCESS_FOLDERS = Cmd_AccessFolders,
    ID_HELP_ABOUT = Cmd_HelpAbout,
    ID_HELP_SHORTCUTS = Cmd_HelpShortcuts,
    ID_HELP_DIAG = Cmd_HelpDiag,
    ID_AI_PROVIDERS = Cmd_AiProviders,
    ID_PERM_INFO = Cmd_PermInfo,
    ID_HELP_GETTING_STARTED = Cmd_HelpGettingStarted,
    ID_CONTENT_BACK = Cmd_ContentBack,
    ID_CONTENT_NAV = Cmd_ContentNav,
    ID_CONTENT_BODY = Cmd_ContentBody,
    ID_SET_OA_STATUS = Cmd_SetOaStatus,
    ID_SET_OA_SIGNIN = Cmd_SetOaSignIn,
    ID_SET_OA_SIGNOUT = Cmd_SetOaSignOut,
    ID_SET_CL_STATUS = Cmd_SetClStatus,
    ID_SET_CL_KEY = Cmd_SetClKey,
    ID_SET_CL_CODE = Cmd_SetClCode,
    ID_SET_CL_DISC = Cmd_SetClDisc,
    ID_SET_DEF_LABEL = Cmd_SetDefLabel,
    ID_SET_DEF_COMBO = Cmd_SetDefCombo,
    ID_SET_CODEX = Cmd_SetCodex,
    ID_SET_COPY_RUNTIME = Cmd_SetCopyRuntime,
    ID_SET_WRAP = Cmd_SetWrap,
    ID_SET_WHITESPACE = Cmd_SetWhitespace,
    ID_SET_ENTER_SENDS = Cmd_SetEnterSends,
    ID_GS_OPEN_FOLDER = Cmd_GsOpenFolder,
    ID_GS_PROVIDERS = Cmd_GsProviders,
};

struct TreeNode {
    std::wstring path;
    bool dir = false;
    bool loaded = false;
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
    HWND hdr_editor = nullptr;
    HWND tabs = nullptr;
    HWND find = nullptr;
    HWND find_toggle = nullptr;
    HWND save = nullptr;
    bool find_open = false;
    HWND editor = nullptr;
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
    HWND composer = nullptr;
    HWND send = nullptr;
    HWND cancel = nullptr;
    HWND hdr_history = nullptr;
    HWND scope = nullptr;
    HWND search = nullptr;
    HWND threads = nullptr;
    HWND ctx = nullptr;
    HWND add_file = nullptr;
    HWND add_sel = nullptr;
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
    bool ime_composing = false;
    WNDPROC composer_prev = nullptr;
    WNDPROC filter_prev = nullptr;
    WNDPROC search_prev = nullptr;
    WNDPROC find_prev = nullptr;
    int drag = 0;  // 1 files|editor, 2 editor|agent, 3 agent|history
    int drag_origin = 0;
    int files_w0 = 0;
    int agent_w0 = 0;
    int history_w0 = 0;
    RECT split1{};
    RECT split2{};
    RECT split3{};
    PaneLayout panes{};
    int narrow_tab = 0;
    bool focus_restore_files = false;
    bool focus_restore_history = false;
    std::wstring editor_path;
    std::vector<std::string> thread_ids;
    std::vector<OpenDoc> docs;
    int active_doc = -1;
    bool suppress_edit = false;

    ContentView content_view = ContentView::Editor;
    SettingsSection settings_section = SettingsSection::Providers;
    int claude_auth_polls = 0;  // remaining WM_TIMER ticks to re-query `claude auth status`
    HWND content_host = nullptr;
    HWND content_back = nullptr;
    HWND content_title = nullptr;
    HWND content_nav = nullptr;
    HWND content_body = nullptr;
    HWND set_oa_status = nullptr;
    HWND set_oa_signin = nullptr;
    HWND set_oa_signout = nullptr;
    HWND set_cl_status = nullptr;
    HWND set_cl_key = nullptr;
    HWND set_cl_code = nullptr;
    HWND set_cl_disc = nullptr;
    HWND set_def_label = nullptr;
    HWND set_def_combo = nullptr;
    HWND set_codex = nullptr;
    HWND set_copy_runtime = nullptr;
    HWND set_wrap = nullptr;
    HWND set_whitespace = nullptr;
    HWND set_enter_sends = nullptr;
    HWND gs_open_folder = nullptr;
    HWND gs_providers = nullptr;
};

Ui* g_ui = nullptr;

std::wstring get_window_text(HWND h);
void do_find(Ui* ui);
void do_goto(Ui* ui);
void layout(Ui* ui);
void center_single_line_edit(HWND edit, HFONT font);

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd)), 96);
}

int px_to_dip(HWND hwnd, int px) {
    return MulDiv(px, 96, static_cast<int>(GetDpiForWindow(hwnd)));
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
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_FACE | CFM_SIZE | CFM_BACKCOLOR;
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
    // Hairline rule: low-contrast box-drawing line, not a bubble/border.
    append_rich(edit, L"\r\n────────────────────────\r\n\r\n", kRule, false);
}

void append_user_message(HWND edit, const std::wstring& text) {
    ShowWindow(edit, SW_SHOW);
    append_rich(edit, L"You\r\n", kText, true);
    std::wstring body = text;
    for (auto& ch : body) {
        if (ch == L'\n') {
            ch = L'\r';
        }
    }
    append_rich(edit, body + L"\r\n", kYouBody, false);
}

void append_agent_message(HWND edit, const std::wstring& text) {
    ShowWindow(edit, SW_SHOW);
    append_rich(edit, L"Agent\r\n", kAccent, true);
    std::wstring body = text;
    for (auto& ch : body) {
        if (ch == L'\n') {
            ch = L'\r';
        }
    }
    append_rich(edit, body + L"\r\n", kAsst, false);
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

bool pick_folder(HWND owner, std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Open project folder");
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

void fill_dir(HWND tree, HTREEITEM parent, const std::wstring& dir, const std::wstring& filter) {
    std::wstring glob = dir;
    if (!glob.empty() && glob.back() != L'\\' && glob.back() != L'/') {
        glob += L'\\';
    }
    glob += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        auto* n = new TreeNode{L"", false, true};
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
        auto* n = new TreeNode{e.path, true, e.reparse};
        insert_tree_item(tree, parent, e.reparse ? (e.name + L" ↗") : e.name, n, !e.reparse);
    }
    for (const auto& e : files) {
        auto* n = new TreeNode{e.path, false, true};
        insert_tree_item(tree, parent, e.name, n, false);
    }
}

void rebuild_tree(Ui* ui) {
    TreeView_DeleteAllItems(ui->tree);
    if (ui->session.settings.project_folder.empty()) {
        return;
    }
    const DWORD a = GetFileAttributesW(ui->session.settings.project_folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        auto* n = new TreeNode{L"", false, true};
        insert_tree_item(ui->tree, TVI_ROOT, L"Folder missing — use Open folder", n, false);
        return;
    }
    auto* root = new TreeNode{ui->session.settings.project_folder, true, false};
    HTREEITEM r = insert_tree_item(ui->tree, TVI_ROOT, folder_name(ui->session.settings.project_folder), root, true);
    fill_dir(ui->tree, r, ui->session.settings.project_folder, get_window_text(ui->filter));
    root->loaded = true;
    TreeView_Expand(ui->tree, r, TVE_EXPAND);
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
void refresh_models(Ui* ui);
void apply_selector(Ui* ui, int owner, int sel);
std::wstring model_choice_label(const ModelChoice& m);
void do_open_folder(Ui* ui);
void persist_store(Ui* ui);
void browse_codex(Ui* ui);
void open_ai_providers_settings(Ui* ui);
void apply_editor_prefs(Ui* ui);
void open_chat_index(Ui* ui, int index);
std::wstring prompt_text(HWND parent, const wchar_t* caption, const std::wstring& initial);
bool rename_chat_id(Ui* ui, const std::string& thread_id);
LRESULT CALLBACK chat_tabs_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

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
}

struct PromptOut {
    std::wstring value;
    bool accepted = false;
};

LRESULT CALLBACK prompt_wnd_proc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 12, 16, 320, 28,
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
    const bool on_rich = focus == ui->composer || focus == ui->transcript || focus == ui->find;
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
    if (on_rich && focus) {
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
    AppendMenuW(file, MF_STRING, ID_OPEN_FOLDER, L"Open Folder…\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_OPEN_FILE, L"Open File…");
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
    AppendMenuW(agent, MF_STRING, ID_AI_PROVIDERS, L"Manage AI Providers…");
    AppendMenuW(agent, MF_STRING, ID_PERM_INFO, L"Permission modes…");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(agent), L"&Agent");

    HMENU access = CreatePopupMenu();
    AppendMenuW(access, MF_STRING, ID_ACCESS_SHOW, L"Show Current Access");
    AppendMenuW(access, MF_STRING, ID_ACCESS_FOLDERS, L"Authorized Folders…");
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
    st.has_context_chips = !ui->session.context_chips.empty();
    return st;
}

void hide_settings_controls(Ui* ui) {
    const HWND ctrls[] = {ui->content_nav, ui->set_oa_status, ui->set_oa_signin, ui->set_oa_signout,
                          ui->set_cl_status, ui->set_cl_key, ui->set_cl_code, ui->set_cl_disc, ui->set_def_label,
                          ui->set_def_combo, ui->set_codex, ui->set_copy_runtime, ui->set_wrap, ui->set_whitespace,
                          ui->set_enter_sends, ui->gs_open_folder, ui->gs_providers};
    for (HWND h : ctrls) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
}

void refresh_settings_pane(Ui* ui) {
    if (!ui || ui->content_view != ContentView::Settings) {
        return;
    }
    const auto oa = openai_provider_status(ui->session.account.signed_in, ui->session.account.email,
                                           ui->session.account.plan, ui->session.account.type);
    std::wstring oa_line = L"OpenAI — ";
    oa_line += utf16(oa.auth_label);
    if (!oa.detail.empty()) {
        oa_line += L"\n";
        oa_line += utf16(oa.detail);
    }
    SetWindowTextW(ui->set_oa_status, oa_line.c_str());

    const auto cl = claude_provider_status();
    std::wstring cl_line = L"Claude — ";
    cl_line += utf16(cl.auth_label);
    if (!cl.detail.empty()) {
        cl_line += L"\n";
        cl_line += utf16(cl.detail);
    }
    cl_line += L"\n(Chat via Claude Code — print mode)";
    SetWindowTextW(ui->set_cl_status, cl_line.c_str());
    EnableWindow(ui->set_oa_signin, !ui->session.account.signed_in);
    EnableWindow(ui->set_oa_signout, ui->session.account.signed_in);
    const bool claude_cli = !discover_claude_cli().empty();
    const bool claude_connected = cl.connected;
    EnableWindow(ui->set_cl_disc, claude_connected ? TRUE : FALSE);
    // Mirror OpenAI: Sign in disabled while already authenticated.
    EnableWindow(ui->set_cl_code, (claude_cli && !claude_connected) ? TRUE : FALSE);

    // Active agent button mirrors the header model selector (same popup catalog).
    EnableWindow(ui->set_def_combo, TRUE);
    InvalidateRect(ui->set_def_combo, nullptr, TRUE);

    InvalidateRect(ui->set_wrap, nullptr, TRUE);
    InvalidateRect(ui->set_whitespace, nullptr, TRUE);
    InvalidateRect(ui->set_enter_sends, nullptr, TRUE);
    ui->session.sync_claude_models();
    refresh_models(ui);
}

std::wstring access_body_text(Ui* ui) {
    std::wstring msg = L"ACTIVE PROJECT\r\n";
    msg += ui->session.project_root.empty() ? L"(none — open a folder)" : ui->session.project_root;
    msg += L"\r\n\r\nSESSION MODE\r\n";
    msg += ui->session.has_project_grant() ? L"Allow edits (workspace-write + sandboxed shell)"
                                          : L"Read only (no project grant)";
    msg += L"\r\nAsk before tool use — approval_policy on-request (always)\r\n";
    msg += L"\r\nAUTHORIZED FOLDERS\r\n";
    msg += ui->session.project_root.empty() ? L"(none)" : ui->session.project_root;
    msg += L"\r\n\r\nUse Access > Authorized Folders… (or Open Folder) to set the project grant.";
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
           L"Ctrl+O             Open folder\r\n"
           L"\r\nSEARCH\r\n"
           L"Ctrl+F             Find\r\n"
           L"Ctrl+H             Replace\r\n"
           L"Ctrl+G             Go to line\r\n"
           L"\r\nSCYLLA\r\n"
           L"Escape             Back from Settings / utility view\r\n"
           L"Enter              Send (when Enter-sends)\r\n"
           L"\r\nMenus: File / Edit / View / Agent / Access / Help";
}

std::wstring getting_started_body_text() {
    return L"GETTING STARTED\r\n\r\n"
           L"1. Open a project folder (sets the agent grant)\r\n"
           L"2. Sign in under Settings → AI Providers (OpenAI and/or Claude)\r\n"
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
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AI Providers"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Editor"));
        SendMessageW(ui->content_nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Advanced"));
        const int sel = section == SettingsSection::Editor ? 1 : (section == SettingsSection::Advanced ? 2 : 0);
        SendMessageW(ui->content_nav, LB_SETCURSEL, sel, 0);
        refresh_settings_pane(ui);
    }
    if (view != ContentView::Settings) {
        populate_content_body(ui);
    }
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
                L"Sign in to either or both under Settings → AI Providers. "
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
        std::wstring row = utf16(c.label);
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

void apply_project(Ui* ui, const std::string& id) {
    auto* p = ui->session.store.by_id(id);
    if (!p) {
        return;
    }
    persist_store(ui);
    ui->session.store.active_project_id = id;
    ui->session.project_root = p->root;
    ui->session.settings.project_folder = p->root;
    ui->session.settings.files_w = p->files_w;
    ui->session.settings.agent_w = p->agent_w;
    ui->session.settings.history_w = p->history_w;
    persist_store(ui);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    ui->session.sync_lockdown_config();
    rebuild_tree(ui);
    refresh_projects(ui);
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
    c.kind = "file";
    c.path = d.path;
    c.label = utf8(folder_name(d.path));
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
    const char* prefix = m.provider_id == "claude" ? "Claude Code" : "OpenAI";
    return utf16(std::string(prefix) + " · " + (m.display.empty() ? m.id : m.display));
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
                if (m.provider_id != "claude") {
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
        ui->sel_models = ui->session.models;
        bool found = false;
        for (std::size_t i = 0; i < ui->sel_models.size(); ++i) {
            items.push_back(model_choice_label(ui->sel_models[i]));
            if (ui->sel_models[i].id == ui->session.selected_model &&
                ui->sel_models[i].provider_id == ui->session.settings.default_provider) {
                cur = static_cast<int>(i);
                found = true;
            }
        }
        if (!found) {
            for (std::size_t i = 0; i < ui->sel_models.size(); ++i) {
                if (ui->sel_models[i].id == ui->session.selected_model) {
                    cur = static_cast<int>(i);
                    break;
                }
            }
        }
        if (items.empty()) {
            items.push_back(L"Sign in under Settings → AI Providers");
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

void layout(Ui* ui) {
    RECT rc{};
    GetClientRect(ui->wnd, &rc);
    const int w = rc.right;
    const int h = rc.bottom;
    const int dpi_w = px_to_dip(ui->wnd, w);
    ui->panes = compute_panes(dpi_w, ui->session.settings.files_w, ui->session.settings.agent_w,
                              ui->session.settings.history_w, ui->session.settings.focus_editor,
                              ui->session.settings.files_mode, ui->session.settings.history_mode, ui->narrow_tab);

    const int bar = dip(ui->wnd, 40);
    const int st = dip(ui->wnd, 22);
    const int hdr = dip(ui->wnd, 34);
    const int pad = dip(ui->wnd, 12);
    const int btnw = dip(ui->wnd, 30);
    const int btnh = dip(ui->wnd, 28);
    const int ybtn = (bar - btnh) / 2;
    const int filter_h = dip(ui->wnd, 28);
    const int thumb_extra = ui->images.empty() ? 0 : dip(ui->wnd, 56);
    const int composer = dip(ui->wnd, 112) + thumb_extra;

    const bool narrow = ui->panes.narrow_tabs;
    ShowWindow(ui->tab_editor, narrow ? SW_SHOW : SW_HIDE);
    ShowWindow(ui->tab_agent, narrow ? SW_SHOW : SW_HIDE);

    int x = pad;
    ShowWindow(ui->brand, SW_HIDE);
    MoveWindow(ui->project, x, ybtn, dip(ui->wnd, 200), btnh, TRUE);
    x += dip(ui->wnd, 208);
    MoveWindow(ui->openfolder, x, ybtn, btnh, btnh, TRUE);
    x += dip(ui->wnd, 40);
    MoveWindow(ui->toggle_files, x, ybtn, dip(ui->wnd, 56), btnh, TRUE);
    x += dip(ui->wnd, 60);
    MoveWindow(ui->toggle_history, x, ybtn, dip(ui->wnd, 56), btnh, TRUE);
    x += dip(ui->wnd, 60);
    MoveWindow(ui->focus, x, ybtn, dip(ui->wnd, 64), btnh, TRUE);

    MoveWindow(ui->settings, w - pad - btnh, ybtn, btnh, btnh, TRUE);
    MoveWindow(ui->account, w - pad - btnh - dip(ui->wnd, 148), ybtn, dip(ui->wnd, 136), btnh, TRUE);
    MoveWindow(ui->models, w - pad - btnh - dip(ui->wnd, 148) - dip(ui->wnd, 208), ybtn, dip(ui->wnd, 200), btnh, TRUE);
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

    auto pxw = [&](int dips) { return dip(ui->wnd, dips); };
    int cx = 0;
    ui->split1 = {};
    ui->split2 = {};
    ui->split3 = {};

    auto place_files = [&](int ww) {
        ShowWindow(ui->hdr_files, SW_HIDE);
        ShowWindow(ui->filter, SW_SHOW);
        ShowWindow(ui->tree, SW_SHOW);
        const int breath = dip(ui->wnd, 5);
        MoveWindow(ui->filter, cx + pad, body_y + breath, ww - pad * 2, filter_h, TRUE);
        center_single_line_edit(ui->filter, ui->font);
        MoveWindow(ui->tree, cx + breath, body_y + breath + filter_h + breath, ww - breath - breath,
                   body_h - breath * 2 - filter_h, TRUE);
        cx += ww;
    };
    auto hide_files = [&]() {
        ShowWindow(ui->hdr_files, SW_HIDE);
        ShowWindow(ui->filter, SW_HIDE);
        ShowWindow(ui->tree, SW_HIDE);
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
            ShowWindow(ui->content_host, SW_SHOW);
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
                const int px = cx + pad_u + nav_w + pad_u;
                const int pw = ww - nav_w - pad_u * 3;
                int py = y0;
                auto place_btn = [&](HWND h, int bw, int bh = -1) {
                    const int hh = bh < 0 ? btnh : bh;
                    MoveWindow(h, px, py, bw, hh, TRUE);
                    ShowWindow(h, SW_SHOW);
                    py += hh + dip(ui->wnd, 8);
                };
                if (ui->settings_section == SettingsSection::Providers) {
                    place_btn(ui->set_oa_status, pw, dip(ui->wnd, 40));
                    place_btn(ui->set_oa_signin, dip(ui->wnd, 160));
                    MoveWindow(ui->set_oa_signout, px + dip(ui->wnd, 168), py - btnh - dip(ui->wnd, 8), dip(ui->wnd, 100),
                               btnh, TRUE);
                    ShowWindow(ui->set_oa_signout, SW_SHOW);
                    place_btn(ui->set_cl_status, pw, dip(ui->wnd, 48));
                    place_btn(ui->set_cl_key, dip(ui->wnd, 140));
                    MoveWindow(ui->set_cl_code, px + dip(ui->wnd, 148), py - btnh - dip(ui->wnd, 8), dip(ui->wnd, 180),
                               btnh, TRUE);
                    ShowWindow(ui->set_cl_code, SW_SHOW);
                    MoveWindow(ui->set_cl_disc, px + dip(ui->wnd, 336), py - btnh - dip(ui->wnd, 8), dip(ui->wnd, 90), btnh,
                               TRUE);
                    ShowWindow(ui->set_cl_disc, SW_SHOW);
                    place_btn(ui->set_def_label, dip(ui->wnd, 160), dip(ui->wnd, 20));
                    place_btn(ui->set_def_combo, dip(ui->wnd, 280));
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
                    place_opt(ui->set_enter_sends);
                    refresh_settings_pane(ui);
                } else {
                    place_btn(ui->set_codex, dip(ui->wnd, 160));
                    place_btn(ui->set_copy_runtime, dip(ui->wnd, 160));
                }
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
        ShowWindow(ui->editor, has_doc ? SW_SHOW : SW_HIDE);
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
        const int tab_h = hdr;
        const int crumb = dip(ui->wnd, 32);
        const int status_w = dip(ui->wnd, 280);
        const int trail = btnh * 2 + 8;
        MoveWindow(ui->tabs, cx + pad, body_y, ww - pad - trail - 4, tab_h, TRUE);
        MoveWindow(ui->find_toggle, cx + ww - trail, body_y + (tab_h - btnh) / 2, btnh, btnh, TRUE);
        MoveWindow(ui->save, cx + ww - btnh, body_y + (tab_h - btnh) / 2, btnh, btnh, TRUE);
        if (ui->find_open) {
            const int find_w = dip(ui->wnd, 180);
            const int crumb_w = ww - pad * 2 - find_w - status_w - 16;
            MoveWindow(ui->hdr_editor, cx + pad, body_y + tab_h, (std::max)(dip(ui->wnd, 80), crumb_w), crumb, TRUE);
            MoveWindow(ui->find, cx + ww - pad - find_w - status_w - 8, body_y + tab_h + (crumb - filter_h) / 2, find_w,
                       filter_h, TRUE);
            center_single_line_edit(ui->find, ui->font);
            MoveWindow(ui->editor_status, cx + ww - pad - status_w, body_y + tab_h, status_w, crumb, TRUE);
        } else {
            MoveWindow(ui->hdr_editor, cx + pad, body_y + tab_h, ww - pad * 2 - status_w - 8, crumb, TRUE);
            MoveWindow(ui->editor_status, cx + ww - pad - status_w, body_y + tab_h, status_w, crumb, TRUE);
        }
        MoveWindow(ui->editor, cx, body_y + tab_h + crumb, ww, body_h - tab_h - crumb, TRUE);
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
        ShowWindow(ui->add_file, SW_SHOW);
        ShowWindow(ui->add_sel, SW_SHOW);
        ShowWindow(ui->send, SW_SHOW);
        ShowWindow(ui->cancel, SW_HIDE);
        MoveWindow(ui->chat_tabs, cx + pad, body_y, ww - pad - btnh - 8, hdr, TRUE);
        MoveWindow(ui->neu, cx + ww - pad - btnh, body_y + (hdr - btnh) / 2, btnh, btnh, TRUE);
        const int chip_h = ui->session.context_chips.empty() ? 0 : dip(ui->wnd, 26);
        if (chip_h) {
            MoveWindow(ui->ctx, cx + pad, body_y + hdr, ww - pad * 2, chip_h, TRUE);
        }
        const int trans_h = body_h - hdr - chip_h - composer;
        const int ty = body_y + hdr + chip_h;
        GETTEXTLENGTHEX gtl{};
        gtl.flags = GTL_DEFAULT;
        gtl.codepage = 1200;
        const bool empty = SendMessageW(ui->transcript, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0) <= 0;
        ShowWindow(ui->transcript, empty ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_agent, empty ? SW_SHOW : SW_HIDE);
        const int transcript_pad = dip(ui->wnd, 12);
        MoveWindow(ui->transcript, cx + transcript_pad, ty + transcript_pad, ww - transcript_pad * 2,
                   std::max(dip(ui->wnd, 80), trans_h) - transcript_pad * 2, TRUE);
        MoveWindow(ui->empty_agent, cx, ty, ww, std::max(dip(ui->wnd, 80), trans_h), TRUE);
        const int cy = ty + std::max(dip(ui->wnd, 80), trans_h);
        const int box_pad = pad;
        ui->composer_box = {cx + box_pad, cy + 4, cx + ww - box_pad, cy + composer - 8};
        const RECT& box = ui->composer_box;
        MoveWindow(ui->composer_panel, box.left, box.top, box.right - box.left, box.bottom - box.top, TRUE);
        ShowWindow(ui->composer_panel, SW_SHOW);
        SetWindowPos(ui->composer_panel, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const int inner = dip(ui->wnd, 12);
        const int foot = dip(ui->wnd, 32);
        const int send_s = dip(ui->wnd, 28);
        const int clip_s = dip(ui->wnd, 28);
        const int thumb_h = ui->images.empty() ? 0 : dip(ui->wnd, 56);
        ui->thumb_row = {box.left + inner, box.top + dip(ui->wnd, 6), box.right - inner,
                         box.top + dip(ui->wnd, 6) + (thumb_h ? dip(ui->wnd, 48) : 0)};
        const int text_top = box.top + (thumb_h ? thumb_h : dip(ui->wnd, 8));
        const int text_h = (box.bottom - foot) - text_top - 4;
        MoveWindow(ui->composer, box.left + inner, text_top, (box.right - box.left) - inner * 2,
                   std::max(dip(ui->wnd, 36), text_h), TRUE);
        MoveWindow(ui->composer_cue, box.left + inner + 4, text_top + 2, dip(ui->wnd, 200), dip(ui->wnd, 20), TRUE);
        MoveWindow(ui->add_sel, box.left + inner, box.bottom - foot, dip(ui->wnd, 100), dip(ui->wnd, 26), TRUE);
        MoveWindow(ui->add_file, box.right - inner - send_s - clip_s - 6, box.bottom - foot - 2, clip_s, clip_s, TRUE);
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
        ShowWindow(ui->add_sel, SW_HIDE);
        ShowWindow(ui->ctx, SW_HIDE);
        ShowWindow(ui->transcript, SW_HIDE);
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

    MoveWindow(ui->status, pad, h - st + 2, w - pad * 2, st - 4, TRUE);
    ShowWindow(ui->homehint, SW_HIDE);
    RedrawWindow(ui->wnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    refresh_thin_scrollbar(ui->tree);
    refresh_thin_scrollbar(ui->threads);
    refresh_thin_scrollbar(ui->ctx);
    refresh_thin_scrollbar(ui->transcript);
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
            if (m.provider_id == ui->session.settings.default_provider) {
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
    if (ui->set_def_combo) {
        SetWindowTextW(ui->set_def_combo, label == L"Model" ? L"Choose agent…" : label.c_str());
        InvalidateRect(ui->set_def_combo, nullptr, TRUE);
    }
}

void open_chat_index(Ui* ui, int index) {
    if (!ui || index < 0 || index >= static_cast<int>(ui->thread_ids.size())) {
        return;
    }
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
    SetWindowTextW(ui->composer, utf16(ui->session.draft_for(tid)).c_str());
    ui->shown_stream.clear();
    SendMessageW(ui->threads, LB_SETCURSEL, index, 0);
    if (ui->chat_tabs) {
        TabCtrl_SetCurSel(ui->chat_tabs, index);
    }
    refresh_chrome(ui);
    layout(ui);
}

void refresh_threads(Ui* ui) {
    SendMessageW(ui->threads, LB_RESETCONTENT, 0, 0);
    ui->thread_ids.clear();
    std::wstring q = get_window_text(ui->search);
    const auto vis = ui->session.store.list_visible(ui->session.account_scope(), q);
    int sel = 0;
    int shown = 0;
    for (Conversation* c : vis) {
        std::wstring name = utf16(c->title.empty() ? "New Chat" : c->title);
        if (c->pinned) {
            name = L"★ " + name;
        }
        SendMessageW(ui->threads, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
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
}

void refresh_chrome(Ui* ui) {
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
        st = claude_is_connected() ? L"Claude connected" : L"Claude not connected";
        if (ui->session.state == AppState::Generating) {
            st = L"Claude generating…";
        } else if (ui->session.state == AppState::Ready) {
            st = L"Ready · Claude";
        }
    } else if (ui->session.has_project_grant()) {
        st += L"  ·  browse/edit ";
        st += folder_name(ui->session.conversation_cwd());
    } else {
        st += L"  ·  read-only";
    }
    if (!ui->session.last_error.empty() &&
        (def != ProviderId::Claude || ui->session.state == AppState::Failed)) {
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
    const bool generating = ui->session.state == AppState::Generating;
    SetWindowTextW(ui->send, generating ? L"Stop" : L"Send");
    const bool provider_ready =
        (def == ProviderId::Claude && claude_is_connected()) ||
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
        GETTEXTLENGTHEX gtl{};
        gtl.flags = GTL_DEFAULT;
        gtl.codepage = 1200;
        const LRESULT len = SendMessageW(ui->transcript, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0);
        const bool empty = len <= 0;
        ShowWindow(ui->transcript, empty ? SW_HIDE : SW_SHOW);
        ShowWindow(ui->empty_agent, empty ? SW_SHOW : SW_HIDE);
        RedrawWindow(ui->empty_agent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
    if (ui->empty_chats) {
        ShowWindow(ui->empty_chats, ui->thread_ids.empty() ? SW_SHOW : SW_HIDE);
        RedrawWindow(ui->empty_chats, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
}

void apply_stream(Ui* ui) {
    if (ui->session.transcript_replace) {
        SetWindowTextW(ui->transcript, L"");
        set_rich_colors(ui->transcript, kWindow, 14, L"Segoe UI");
        for (std::size_t i = 0; i < ui->session.history_messages.size(); ++i) {
            if (i > 0) {
                append_divider(ui->transcript);
            }
            const auto& message = ui->session.history_messages[i];
            if (message.user) {
                append_user_message(ui->transcript, utf16(message.text));
            } else {
                append_agent_message(ui->transcript, utf16(message.text));
            }
        }
        ui->shown_stream = ui->session.stream_buffer;
        ui->session.transcript_replace = false;
        return;
    }
    if (ui->session.stream_buffer.size() <= ui->shown_stream.size()) {
        return;
    }
    if (ui->session.stream_buffer.compare(0, ui->shown_stream.size(), ui->shown_stream) != 0) {
        SetWindowTextW(ui->transcript, utf16(ui->session.stream_buffer).c_str());
        ui->shown_stream = ui->session.stream_buffer;
        return;
    }
    const std::string delta = ui->session.stream_buffer.substr(ui->shown_stream.size());
    append_rich(ui->transcript, utf16(delta), kAsst, false);
    ui->shown_stream = ui->session.stream_buffer;
}

void persist_window(Ui* ui) {
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
    if (ui->ime_composing) {
        return;
    }
    const ProviderId def = provider_id_from_string(ui->session.settings.default_provider);
    if (!provider_can_send(def)) {
        SetWindowTextW(ui->status, def == ProviderId::Claude ? L"Connect Claude under Settings → AI Providers"
                                                             : L"Sign in with ChatGPT to send");
        return;
    }
    if (def == ProviderId::OpenAI && !ui->session.account.signed_in) {
        SetWindowTextW(ui->status, L"Sign in with ChatGPT to send");
        return;
    }
    std::wstring text = get_window_text(ui->composer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    if (text.empty() && ui->images.empty()) {
        return;
    }
    for (const auto& img : ui->images) {
        ContextChip c;
        c.kind = "image";
        c.path = img.path;
        const auto slash = img.path.find_last_of(L"\\/");
        c.label = utf8(slash == std::wstring::npos ? img.path : img.path.substr(slash + 1));
        c.body = utf8(img.path);
        add_chip(ui, std::move(c));
    }
    clear_composer_images(ui);
    const std::wstring display = text.empty() ? L"(image attachment)" : text;
    GETTEXTLENGTHEX gtl{};
    gtl.flags = GTL_DEFAULT;
    gtl.codepage = 1200;
    const bool had_content = SendMessageW(ui->transcript, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0) > 0;
    if (had_content) {
        append_divider(ui->transcript);
    }
    append_user_message(ui->transcript, display);
    append_divider(ui->transcript);
    append_rich(ui->transcript, L"Agent\r\n", kAccent, true);
    ui->session.stream_buffer.clear();
    ui->shown_stream.clear();
    ui->session.send_user(utf8(text.empty() ? display : text));
    SetWindowTextW(ui->composer, L"");
    ui->session.set_draft(ui->session.active_thread_id, "");
    refresh_chrome(ui);
    layout(ui);
}

void do_open_folder(Ui* ui) {
    std::wstring folder;
    if (!pick_folder(ui->wnd, folder)) {
        return;
    }
    persist_store(ui);
    auto* p = ui->session.store.open_or_create(folder);
    if (!p) {
        return;
    }
    ui->session.project_root = p->root;
    ui->session.settings.project_folder = p->root;
    persist_store(ui);
    save_settings(ui->session.paths.settings_path, ui->session.settings);
    ui->session.sync_lockdown_config();
    rebuild_tree(ui);
    refresh_projects(ui);
    refresh_threads(ui);
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
    if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool enter_sends = ui->session.settings.enter_sends;
        const bool send_now = enter_sends ? !shift : shift;
        if (send_now && !ui->ime_composing) {
            do_send(ui);
            return 0;
        }
    }
    return CallWindowProcW(ui->composer_prev, hwnd, msg, wparam, lparam);
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
    ui->tree = mk(hwnd, WC_TREEVIEWW, L"", WS_TABSTOP | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_TRACKSELECT,
                   ID_TREE);
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

    ui->content_host = mk(hwnd, L"STATIC", L"", 0, 0);
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
    ui->content_body = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
                                       hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CONTENT_BODY)), nullptr, nullptr);
    ShowWindow(ui->content_body, SW_HIDE);
    ui->set_oa_status = mk(hwnd, L"STATIC", L"", 0, ID_SET_OA_STATUS);
    ui->set_oa_signin = mk(hwnd, L"BUTTON", L"Sign in with ChatGPT", btn, ID_SET_OA_SIGNIN);
    ui->set_oa_signout = mk(hwnd, L"BUTTON", L"Sign out", btn, ID_SET_OA_SIGNOUT);
    ui->set_cl_status = mk(hwnd, L"STATIC", L"", 0, ID_SET_CL_STATUS);
    ui->set_cl_key = mk(hwnd, L"BUTTON", L"Connect API key…", btn, ID_SET_CL_KEY);
    ui->set_cl_code = mk(hwnd, L"BUTTON", L"Sign in with Claude Code", btn, ID_SET_CL_CODE);
    ui->set_cl_disc = mk(hwnd, L"BUTTON", L"Disconnect", btn, ID_SET_CL_DISC);
    ui->set_def_label = mk(hwnd, L"STATIC", L"Active agent", 0, ID_SET_DEF_LABEL);
    ui->set_def_combo = mk(hwnd, L"BUTTON", L"Choose agent…", btn, ID_SET_DEF_COMBO);
    ui->set_codex = mk(hwnd, L"BUTTON", L"Codex executable…", btn, ID_SET_CODEX);
    ui->set_copy_runtime = mk(hwnd, L"BUTTON", L"Copy runtime path", btn, ID_SET_COPY_RUNTIME);
    ui->set_wrap = mk(hwnd, L"BUTTON", L"Word wrap", btn, ID_SET_WRAP);
    ui->set_whitespace = mk(hwnd, L"BUTTON", L"Show whitespace", btn, ID_SET_WHITESPACE);
    ui->set_enter_sends = mk(hwnd, L"BUTTON", L"Enter sends message", btn, ID_SET_ENTER_SENDS);
    ui->gs_open_folder = mk(hwnd, L"BUTTON", L"Open Project", btn, ID_GS_OPEN_FOLDER);
    ui->gs_providers = mk(hwnd, L"BUTTON", L"Manage Providers", btn, ID_GS_PROVIDERS);
    hide_settings_controls(ui);
    ShowWindow(ui->gs_open_folder, SW_HIDE);
    ShowWindow(ui->gs_providers, SW_HIDE);

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
    ui->add_sel = mk(hwnd, L"BUTTON", L"Add selection", btn, ID_ADD_SEL);
    ui->ctx = CreateWindowExW(0, L"LISTBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP,
                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_CTX), nullptr, nullptr);
    ui->empty_agent = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ui->transcript = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
                                    hwnd, reinterpret_cast<HMENU>(ID_TRANSCRIPT), nullptr, nullptr);
    ui->composer_panel = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY | WS_CLIPSIBLINGS, 0);
    ui->composer = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_TABSTOP, 0, 0, 0, 0,
                                    hwnd, reinterpret_cast<HMENU>(ID_COMPOSER), nullptr, nullptr);
    ui->send = mk(hwnd, L"BUTTON", L"Send", btn | BS_OWNERDRAW, ID_SEND);
    ui->cancel = mk(hwnd, L"BUTTON", L"Stop", btn | BS_OWNERDRAW, ID_CANCEL);
    ShowWindow(ui->cancel, SW_HIDE);
    ui->composer_cue = mk(hwnd, L"STATIC", L"Message your agent", SS_NOTIFY, 0);
    ui->hdr_history = mk(hwnd, L"STATIC", L"  Chats", 0, ID_HDR_HISTORY);
    ShowWindow(ui->hdr_history, SW_HIDE);
    ui->scope = mk(hwnd, L"BUTTON", L"Current project", btn, ID_SCOPE);
    ShowWindow(ui->scope, SW_HIDE);
    ui->search = mk(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_MULTILINE, ID_SEARCH);
    ui->pin = mk(hwnd, L"BUTTON", L"Pin", btn, ID_PIN_CHAT);
    ui->archive = mk(hwnd, L"BUTTON", L"Delete", btn, ID_ARCHIVE_CHAT);
    ui->threads = CreateWindowExW(0, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                                      WS_TABSTOP,
                                  0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_THREADS), nullptr, nullptr);
    ui->empty_chats = mk(hwnd, L"STATIC", L"", SS_OWNERDRAW, 0);
    ui->status = mk(hwnd, L"STATIC", L"Offline", 0, ID_STATUS);
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
    SetWindowTheme(ui->tree, L"", L"");
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
    install_thin_scrollbar(ui->transcript, kWindow);
    install_thin_scrollbar(ui->editor, kEditor);
    install_thin_scrollbar(ui->composer, kInput);
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
                    ui->tab_editor, ui->tab_agent, ui->models, ui->threads, ui->filter, ui->search, ui->tree, ui->tabs,
                    ui->chat_tabs, ui->find, ui->find_toggle, ui->save, ui->scope, ui->ctx, ui->add_file, ui->add_sel,
                    ui->pin, ui->archive, ui->empty_open_file, ui->empty_open_folder, ui->composer_cue, ui->account,
                    ui->content_back, ui->content_title, ui->content_nav, ui->content_body, ui->set_oa_status,
                    ui->set_oa_signin, ui->set_oa_signout, ui->set_cl_status, ui->set_cl_key, ui->set_cl_code,
                    ui->set_cl_disc, ui->set_def_label, ui->set_def_combo, ui->set_codex, ui->set_copy_runtime, ui->set_wrap,
                    ui->set_whitespace, ui->set_enter_sends, ui->gs_open_folder, ui->gs_providers}) {
        if (h) {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
        }
    }
    SendMessageW(ui->account, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->agent_hint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->status, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    SendMessageW(ui->homehint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    if (ui->editor_status) {
        SendMessageW(ui->editor_status, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_small), TRUE);
    }
    if (ui->content_title) {
        SendMessageW(ui->content_title, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font_semi), TRUE);
    }
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
    if (child == ui->content_host || child == ui->content_title || child == ui->content_body || child == ui->content_nav ||
        child == ui->set_oa_status || child == ui->set_cl_status || child == ui->set_def_label) {
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
            ui->session.selected_model = ui->session.settings.selected_model;
            ui->session.sync_claude_models();
            ui->claude_auth_polls = 3;  // warm Claude OAuth cache so agent menu includes Claude rows early
            if (ui->session.settings.codex_path.empty()) {
                ui->session.settings.codex_path = discover_codex_exe();
            }
            apply_editor_prefs(ui);
            SetWindowTextW(ui->composer, utf16(ui->session.draft_for(ui->session.settings.last_thread_id)).c_str());
            if (!ui->session.settings.project_folder.empty()) {
                rebuild_tree(ui);
            }
            std::wstring err;
            if (!ui->session.start_runtime(hwnd, WM_SCYLLA_LINE, &err)) {
                SetWindowTextW(ui->status, err.c_str());
            }
            refresh_projects(ui);
            refresh_threads(ui);
            refresh_chrome(ui);
            SetTimer(hwnd, 1, 2000, nullptr);
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
            if (wparam == VK_ESCAPE && ui->content_view != ContentView::Editor) {
                go_back_content(ui);
                return 0;
            }
            break;
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
            SelectObject(dc, old);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                if (pt_in(ui->split1, p.x, p.y) || pt_in(ui->split2, p.x, p.y) || pt_in(ui->split3, p.x, p.y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            ui->drag = 0;
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
                if (mi->CtlID == ID_THREADS) {
                    mi->itemHeight = dip(hwnd, 48);
                } else if (mi->CtlID == ID_CTX) {
                    mi->itemHeight = dip(hwnd, 24);
                } else if (mi->CtlID == ID_CONTENT_NAV) {
                    mi->itemHeight = dip(hwnd, 32);
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
            if (di->CtlType == ODT_BUTTON) {
                BtnVisual vis = BtnVisual::Secondary;
                bool tog = false;
                if (di->CtlID == ID_SEND) {
                    vis = BtnVisual::Primary;
                } else if (di->CtlID == ID_PROJECT || di->CtlID == ID_SCOPE) {
                    vis = BtnVisual::Selector;
                } else if (di->CtlID == ID_MODELS || di->CtlID == ID_SET_DEF_COMBO) {
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
                if (di->hwndItem == ui->content_nav) {
                    const Theme& t = theme();
                    fill_rect(di->hDC, di->rcItem, sel ? t.amber : t.editor);
                    RECT tr = di->rcItem;
                    tr.left += 12;
                    SetBkMode(di->hDC, TRANSPARENT);
                    SetTextColor(di->hDC, sel ? RGB(0xFF, 0xFF, 0xFF) : t.text);
                    SelectObject(di->hDC, ui->font);
                    DrawTextW(di->hDC, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
            if (from == ui->threads && !ui->thread_ids.empty()) {
                POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (p.x == -1 && p.y == -1) {
                    GetCursorPos(&p);
                }
                POINT client = p;
                ScreenToClient(ui->threads, &client);
                const int hit = static_cast<int>(SendMessageW(ui->threads, LB_ITEMFROMPOINT, 0,
                                                              MAKELPARAM(client.x, client.y)));
                if (HIWORD(hit) == 0 && LOWORD(hit) < ui->thread_ids.size()) {
                    SendMessageW(ui->threads, LB_SETCURSEL, LOWORD(hit), 0);
                }
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, ID_RENAME_CHAT, L"Rename");
                AppendMenuW(m, MF_STRING, ID_PIN_CHAT, L"Pin / unpin");
                AppendMenuW(m, MF_STRING, ID_ARCHIVE_CHAT, L"Delete");
                TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(m);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lparam);
            if (hdr->hwndFrom == ui->tree && hdr->code == NM_CUSTOMDRAW) {
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
            if (hdr->hwndFrom == ui->tree && hdr->code == TVN_DELETEITEMW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                delete reinterpret_cast<TreeNode*>(nmtv->itemOld.lParam);
            }
            if (hdr->hwndFrom == ui->tree && hdr->code == TVN_ITEMEXPANDINGW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                auto* node = reinterpret_cast<TreeNode*>(nmtv->itemNew.lParam);
                if (node && node->dir && !node->loaded && (nmtv->action & TVE_EXPAND)) {
                    node->loaded = true;
                    fill_dir(ui->tree, nmtv->itemNew.hItem, node->path, L"");
                }
            }
            if (hdr->hwndFrom == ui->tree && hdr->code == TVN_SELCHANGEDW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lparam);
                auto* node = reinterpret_cast<TreeNode*>(nmtv->itemNew.lParam);
                if (node && !node->dir && !node->path.empty()) {
                    open_document(ui, node->path, false);
                }
            }
            if (hdr->hwndFrom == ui->tree && hdr->code == NM_DBLCLK) {
                HTREEITEM sel = TreeView_GetSelection(ui->tree);
                if (sel) {
                    TVITEMW it{};
                    it.mask = TVIF_PARAM;
                    it.hItem = sel;
                    if (TreeView_GetItem(ui->tree, &it)) {
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
            if (id == ID_SIGNIN) {
                ui->session.login_chatgpt();
                refresh_chrome(ui);
            } else if (id == ID_SIGNOUT) {
                ui->session.logout();
            } else if (id == ID_NEW) {
                ui->session.set_draft(ui->session.active_thread_id, utf8(get_window_text(ui->composer)));
                ui->session.new_conversation();
                SetWindowTextW(ui->composer, L"");
                SetWindowTextW(ui->transcript, L"");
                ui->shown_stream.clear();
                ui->session.stream_buffer.clear();
            } else if (id == ID_SEND) {
                if (ui->session.state == AppState::Generating) {
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
            } else if (id == ID_SET_DEF_COMBO && code == BN_CLICKED) {
                // Same agent catalog as the header — not a separate OpenAI/Claude stub list.
                PostMessageW(hwnd, WM_SCYLLA_OPEN_SEL, ID_MODELS, reinterpret_cast<LPARAM>(ui->set_def_combo));
            } else if (id == ID_SCOPE && code == BN_CLICKED) {
                PostMessageW(hwnd, WM_SCYLLA_OPEN_SEL, ID_SCOPE, reinterpret_cast<LPARAM>(ui->scope));
            } else if (id == ID_COMPOSER && code == EN_CHANGE) {
                ShowWindow(ui->composer_cue, get_window_text(ui->composer).empty() ? SW_SHOW : SW_HIDE);
            } else if (reinterpret_cast<HWND>(lparam) == ui->composer_cue && code == STN_CLICKED) {
                SetFocus(ui->composer);
            } else if (id == ID_SAVE) {
                save_active(ui);
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
            } else if (id == ID_ACCESS_SHOW || id == ID_PERM_INFO) {
                show_content_view(ui, ContentView::Access);
            } else if (id == ID_ACCESS_FOLDERS) {
                do_open_folder(ui);
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
                SettingsSection sec = SettingsSection::Providers;
                if (sel == 1) {
                    sec = SettingsSection::Editor;
                } else if (sel == 2) {
                    sec = SettingsSection::Advanced;
                }
                show_content_view(ui, ContentView::Settings, sec);
            } else if (id == ID_SET_OA_SIGNIN) {
                ui->session.login_chatgpt();
                refresh_settings_pane(ui);
                refresh_chrome(ui);
            } else if (id == ID_SET_OA_SIGNOUT) {
                ui->session.logout();
                refresh_settings_pane(ui);
                refresh_chrome(ui);
            } else if (id == ID_SET_CL_KEY) {
                const std::wstring key = prompt_text(hwnd, L"Anthropic API key", L"");
                if (!key.empty()) {
                    if (claude_api_key_save(key)) {
                        MessageBoxW(hwnd, L"Claude API key saved to Windows Credential Manager.", L"Claude",
                                    MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd, L"Could not save API key to Credential Manager.", L"Claude",
                                    MB_OK | MB_ICONERROR);
                    }
                }
                ui->session.sync_claude_models();
                refresh_settings_pane(ui);
                refresh_models(ui);
                refresh_chrome(ui);
            } else if (id == ID_SET_CL_CODE) {
                std::wstring err;
                if (!claude_code_login_launch(&err)) {
                    MessageBoxW(hwnd, err.c_str(), L"Claude Code", MB_OK | MB_ICONWARNING);
                } else {
                    ui->claude_auth_polls = 45;  // ~90s at 2s timer — pick up OAuth when console finishes
                    MessageBoxW(hwnd,
                                L"Complete Claude Code login in the console/browser.\n"
                                L"Settings will refresh when the session is detected.\n"
                                L"(Chat send remains Codex-only for now.)",
                                L"Claude Code", MB_OK | MB_ICONINFORMATION);
                }
                claude_code_session_status(true);
                ui->session.sync_claude_models();
                refresh_settings_pane(ui);
                refresh_models(ui);
                refresh_chrome(ui);
            } else if (id == ID_SET_CL_DISC) {
                std::wstring err;
                if (claude_code_session_status(true).logged_in) {
                    if (!claude_code_logout(&err) && !err.empty()) {
                        MessageBoxW(hwnd, err.c_str(), L"Claude Code", MB_OK | MB_ICONWARNING);
                    }
                }
                claude_api_key_clear();
                claude_clear_connection_state();
                if (ui->session.settings.default_provider == "claude") {
                    ui->session.set_default_provider("openai");
                }
                save_settings(ui->session.paths.settings_path, ui->session.settings);
                claude_code_session_status(true);
                ui->session.sync_claude_models();
                refresh_settings_pane(ui);
                refresh_models(ui);
                refresh_chrome(ui);
            } else if (id == ID_SET_CODEX) {
                browse_codex(ui);
            } else if (id == ID_SET_COPY_RUNTIME) {
                PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_COPY_RUNTIME, 0), 0);
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
                            ui->session.active_thread_id.clear();
                            ui->session.settings.last_thread_id.clear();
                            SetWindowTextW(ui->transcript, L"");
                            SetWindowTextW(ui->composer, L"");
                            ui->shown_stream.clear();
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
        case WM_SCYLLA_LINE: {
            auto* line = reinterpret_cast<std::string*>(lparam);
            if (line) {
                const int model_epoch = ui->session.models_epoch;
                const std::size_t thread_n = ui->session.threads.size();
                const std::size_t conv_n = ui->session.store.conversations.size();
                const std::string active_before = ui->session.active_thread_id;
                ui->session.handle_line(*line);
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
                if (!ui->sel_list) {
                    refresh_chrome(ui);
                }
            }
            return 0;
        }
        case WM_DPICHANGED: {
            RECT* r = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            DeleteObject(ui->font);
            DeleteObject(ui->font_small);
            DeleteObject(ui->font_semi);
            DeleteObject(ui->font_title);
            DeleteObject(ui->font_mono);
            ui->font = make_font_dip(hwnd, 13, false, L"Segoe UI Variable");
            ui->font_small = make_font_dip(hwnd, 12, false, L"Segoe UI");
            ui->font_semi = make_font_dip(hwnd, 14, true, L"Segoe UI Variable");
            ui->font_title = make_font_dip(hwnd, 18, true, L"Segoe UI Variable");
            ui->font_mono = make_font_dip(hwnd, 14, false, L"Cascadia Mono");
            apply_fonts(ui);
            if (ui->editor && ui->active_doc >= 0) {
                editor_apply_chrome(ui->editor, ui->font_mono, 13, static_cast<int>(GetDpiForWindow(hwnd)));
            }
            layout(ui);
            return 0;
        }
        case WM_TIMER:
            if (wparam == 1) {
                check_external(ui);
                write_recovery(ui);
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
            }
            return 0;
        case WM_CLOSE: {
            pull_editor(ui);
            write_recovery(ui);
            for (int i = 0; i < static_cast<int>(ui->docs.size()); ++i) {
                if (!ui->docs[i].dirty) {
                    continue;
                }
                std::wstring msg = L"Save " + folder_name(ui->docs[i].path) + L" before closing?";
                const int choice = MessageBoxW(hwnd, msg.c_str(), L"Scylla", MB_YESNOCANCEL | MB_ICONWARNING);
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
            persist_window(ui);
            ui->session.stop_runtime();
            clear_composer_images(ui);
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            clear_composer_images(ui);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

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
    fill_rect(mem, rc, kWindow);
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
                fill_rect(dc, rc, kWindow);
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
        {FVIRTKEY | FCONTROL | FALT, 'A', ID_FOCUS_COMPOSER},
        {FVIRTKEY | FCONTROL | FALT, 'H', ID_TOGGLE_HISTORY},
        {FVIRTKEY | FCONTROL | FALT, 'E', ID_TOGGLE_FILES},
        {FVIRTKEY | FCONTROL | FALT, VK_RETURN, ID_FOCUS},
        {FVIRTKEY | FCONTROL | FALT, 'N', ID_NEW},
        {FVIRTKEY, VK_ESCAPE, ID_CONTENT_BACK},
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
        if (!TranslateAcceleratorW(hwnd, accel, &msg) && !IsDialogMessageW(hwnd, &msg)) {
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
