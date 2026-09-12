#include "scyllagpt/editor_host.h"

#include "scyllagpt/language.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/utf.h"

#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include "Scintilla.h"

#include <algorithm>
#include <commctrl.h>
#include <cstring>
#include <vector>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace scyllagpt {
namespace {

constexpr int kEditorFontPoints = 10;

sptr_t send(HWND sci, unsigned msg, uptr_t w = 0, sptr_t l = 0) {
    return SendMessageW(sci, msg, w, l);
}

struct MinimapState {
    HWND primary = nullptr;
    bool dragging = false;
};

void copy_view_styles(HWND source, HWND target) {
    if (!source || !target) return;
    char font[LF_FACESIZE]{};
    for (int style = 0; style <= STYLE_MAX; ++style) {
        send(target, SCI_STYLESETFORE, style, send(source, SCI_STYLEGETFORE, style));
        send(target, SCI_STYLESETBACK, style, send(source, SCI_STYLEGETBACK, style));
        send(target, SCI_STYLESETSIZEFRACTIONAL, style, send(source, SCI_STYLEGETSIZEFRACTIONAL, style));
        send(target, SCI_STYLESETWEIGHT, style, send(source, SCI_STYLEGETWEIGHT, style));
        send(target, SCI_STYLESETITALIC, style, send(source, SCI_STYLEGETITALIC, style));
        send(target, SCI_STYLESETUNDERLINE, style, send(source, SCI_STYLEGETUNDERLINE, style));
        send(target, SCI_STYLESETEOLFILLED, style, send(source, SCI_STYLEGETEOLFILLED, style));
        send(target, SCI_STYLESETCASE, style, send(source, SCI_STYLEGETCASE, style));
        std::memset(font, 0, sizeof(font));
        send(source, SCI_STYLEGETFONT, style, reinterpret_cast<sptr_t>(font));
        if (font[0]) send(target, SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(font));
    }
}

void minimap_navigate(HWND minimap, MinimapState* state, int y) {
    if (!state || !IsWindow(state->primary)) return;
    RECT rc{};
    GetClientRect(minimap, &rc);
    const int lines = static_cast<int>(send(state->primary, SCI_GETLINECOUNT));
    if (lines <= 0 || rc.bottom <= 0) return;
    const int target = std::clamp(MulDiv(y, lines, rc.bottom), 0, lines - 1);
    const int visible = static_cast<int>(send(state->primary, SCI_LINESONSCREEN));
    send(state->primary, SCI_SETFIRSTVISIBLELINE, (std::max)(0, target - visible / 2));
    SetFocus(state->primary);
}

LRESULT CALLBACK minimap_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR data) {
    auto* state = reinterpret_cast<MinimapState*>(data);
    switch (msg) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONDOWN:
        state->dragging = true;
        SetCapture(hwnd);
        minimap_navigate(hwnd, state, GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (state->dragging && (wp & MK_LBUTTON)) minimap_navigate(hwnd, state, GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        if (state->dragging) {
            state->dragging = false;
            ReleaseCapture();
            minimap_navigate(hwnd, state, GET_Y_LPARAM(lp));
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state && IsWindow(state->primary)) {
            const int first = static_cast<int>(send(state->primary, SCI_GETFIRSTVISIBLELINE));
            const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            send(state->primary, SCI_SETFIRSTVISIBLELINE, (std::max)(0, first - steps * 3));
            SetFocus(state->primary);
        }
        return 0;
    case WM_CONTEXTMENU:
    case WM_CHAR:
    case WM_KEYDOWN:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, minimap_subclass, id);
        delete state;
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

COLORREF to_bgr(COLORREF rgb) {
    return rgb;  // Scintilla expects COLORREF (RGB)
}

void style_set(HWND sci, int style, COLORREF fore, COLORREF back, const char* font, int size,
               bool bold = false) {
    send(sci, SCI_STYLESETFORE, style, to_bgr(fore));
    send(sci, SCI_STYLESETBACK, style, to_bgr(back));
    if (font) {
        send(sci, SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(font));
    }
    send(sci, SCI_STYLESETSIZE, style, size);
    send(sci, SCI_STYLESETBOLD, style, bold ? 1 : 0);
}

void apply_common_styles(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    send(sci, SCI_STYLERESETDEFAULT, 0, 0);
    style_set(sci, STYLE_DEFAULT, t.text, t.editor, face, size);
    send(sci, SCI_STYLECLEARALL, 0, 0);

    style_set(sci, STYLE_LINENUMBER, t.line_no, t.gutter, face, size);
    style_set(sci, STYLE_CONTROLCHAR, t.muted, t.editor, face, size);
    style_set(sci, STYLE_BRACELIGHT, t.amber_text, t.selected, face, size, true);
    style_set(sci, STYLE_BRACEBAD, t.error, t.editor, face, size, true);
    style_set(sci, STYLE_INDENTGUIDE, t.divider, t.editor, face, size);

    send(sci, SCI_SETSELBACK, 1, to_bgr(t.sel_bg));
    send(sci, SCI_SETSELFORE, 0, 0);
    send(sci, SCI_SETCARETFORE, 0, to_bgr(t.amber));
    send(sci, SCI_SETCARETLINEVISIBLE, 1, 0);
    send(sci, SCI_SETCARETLINEBACK, 0, to_bgr(t.current_line));
    send(sci, SCI_SETCARETLINEBACKALPHA, 40, 0);

    send(sci, SCI_SETWHITESPACEFORE, 1, to_bgr(t.muted));
    send(sci, SCI_SETWHITESPACEBACK, 0, 0);

    // Scintilla otherwise leaves symbol/fold margins at its system default (white).
    for (int margin = 0; margin <= 2; ++margin) {
        send(sci, SCI_SETMARGINBACKN, margin, to_bgr(t.gutter));
    }
    send(sci, SCI_SETFOLDMARGINCOLOUR, 1, to_bgr(t.gutter));
    send(sci, SCI_SETFOLDMARGINHICOLOUR, 1, to_bgr(t.gutter));

    // Fold margin
    send(sci, SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    send(sci, SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    send(sci, SCI_SETMARGINWIDTHN, 2, 12);
    send(sci, SCI_SETMARGINSENSITIVEN, 2, 1);
    send(sci, SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED, 0);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    send(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    for (int m = SC_MARKNUM_FOLDEREND; m <= SC_MARKNUM_FOLDEROPEN; ++m) {
        send(sci, SCI_MARKERSETFORE, m, to_bgr(t.muted));
        send(sci, SCI_MARKERSETBACK, m, to_bgr(t.gutter));
    }
}

void style_cpp_family(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c, bool bold = false) { style_set(sci, s, c, t.editor, face, size, bold); };
    set(SCE_C_DEFAULT, t.text);
    set(SCE_C_COMMENT, t.comment);
    set(SCE_C_COMMENTLINE, t.comment);
    set(SCE_C_COMMENTDOC, t.comment);
    set(SCE_C_NUMBER, t.number);
    set(SCE_C_WORD, t.keyword, true);
    set(SCE_C_WORD2, t.keyword);
    set(SCE_C_STRING, t.string_lit);
    set(SCE_C_CHARACTER, t.string_lit);
    set(SCE_C_UUID, t.number);
    set(SCE_C_PREPROCESSOR, t.preproc);
    set(SCE_C_OPERATOR, t.punct);
    set(SCE_C_IDENTIFIER, t.ident);
    set(SCE_C_STRINGEOL, t.error);
    set(SCE_C_VERBATIM, t.string_lit);
    set(SCE_C_REGEX, t.string_lit);
    set(SCE_C_COMMENTLINEDOC, t.comment);
    set(SCE_C_COMMENTDOCKEYWORD, t.keyword);
    set(SCE_C_COMMENTDOCKEYWORDERROR, t.error);
    set(SCE_C_GLOBALCLASS, t.ident, true);
}

void set_keywords_cpp(HWND sci) {
    static const char* k1 =
        "alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t "
        "char32_t class compl concept const consteval constexpr constinit const_cast continue co_await "
        "co_return co_yield decltype default delete do double dynamic_cast else enum explicit export "
        "extern false final float for friend goto if inline int long mutable namespace new noexcept not "
        "not_eq nullptr operator or or_eq override private protected public register reinterpret_cast "
        "requires return short signed sizeof static static_assert static_cast struct switch template "
        "this thread_local throw true try typedef typeid typename union unsigned using virtual void "
        "volatile wchar_t while xor xor_eq";
    static const char* k2 =
        "std string vector map set unordered_map unordered_set optional unique_ptr shared_ptr weak_ptr "
        "size_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t";
    send(sci, SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(k1));
    send(sci, SCI_SETKEYWORDS, 1, reinterpret_cast<sptr_t>(k2));
}

void style_python(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c, bool bold = false) { style_set(sci, s, c, t.editor, face, size, bold); };
    set(SCE_P_DEFAULT, t.text);
    set(SCE_P_COMMENTLINE, t.comment);
    set(SCE_P_NUMBER, t.number);
    set(SCE_P_STRING, t.string_lit);
    set(SCE_P_CHARACTER, t.string_lit);
    set(SCE_P_WORD, t.keyword, true);
    set(SCE_P_TRIPLE, t.string_lit);
    set(SCE_P_TRIPLEDOUBLE, t.string_lit);
    set(SCE_P_CLASSNAME, t.ident, true);
    set(SCE_P_DEFNAME, t.ident, true);
    set(SCE_P_OPERATOR, t.punct);
    set(SCE_P_IDENTIFIER, t.ident);
    set(SCE_P_COMMENTBLOCK, t.comment);
    set(SCE_P_STRINGEOL, t.error);
    set(SCE_P_WORD2, t.keyword);
    set(SCE_P_DECORATOR, t.preproc);
    set(SCE_P_FSTRING, t.string_lit);
    set(SCE_P_FCHARACTER, t.string_lit);
    set(SCE_P_FTRIPLE, t.string_lit);
    set(SCE_P_FTRIPLEDOUBLE, t.string_lit);
    static const char* kw =
        "False None True and as assert async await break class continue def del elif else except "
        "finally for from global if import in is lambda nonlocal not or pass raise return try while "
        "with yield";
    send(sci, SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(kw));
}

void style_json(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c) { style_set(sci, s, c, t.editor, face, size); };
    set(SCE_JSON_DEFAULT, t.text);
    set(SCE_JSON_NUMBER, t.number);
    set(SCE_JSON_STRING, t.string_lit);
    set(SCE_JSON_STRINGEOL, t.error);
    set(SCE_JSON_PROPERTYNAME, t.ident);
    set(SCE_JSON_ESCAPESEQUENCE, t.preproc);
    set(SCE_JSON_LINECOMMENT, t.comment);
    set(SCE_JSON_BLOCKCOMMENT, t.comment);
    set(SCE_JSON_OPERATOR, t.punct);
    set(SCE_JSON_KEYWORD, t.keyword);
    set(SCE_JSON_LDKEYWORD, t.keyword);
    set(SCE_JSON_ERROR, t.error);
}

void style_html(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c) { style_set(sci, s, c, t.editor, face, size); };
    set(SCE_H_DEFAULT, t.text);
    set(SCE_H_TAG, t.keyword);
    set(SCE_H_TAGUNKNOWN, t.error);
    set(SCE_H_ATTRIBUTE, t.ident);
    set(SCE_H_ATTRIBUTEUNKNOWN, t.muted);
    set(SCE_H_NUMBER, t.number);
    set(SCE_H_DOUBLESTRING, t.string_lit);
    set(SCE_H_SINGLESTRING, t.string_lit);
    set(SCE_H_COMMENT, t.comment);
    set(SCE_H_ENTITY, t.preproc);
    set(SCE_H_TAGEND, t.keyword);
    set(SCE_H_XMLSTART, t.keyword);
    set(SCE_H_XMLEND, t.keyword);
    set(SCE_H_SCRIPT, t.punct);
    set(SCE_H_ASP, t.preproc);
    set(SCE_H_ASPAT, t.preproc);
    set(SCE_H_CDATA, t.string_lit);
    set(SCE_H_QUESTION, t.preproc);
    set(SCE_H_VALUE, t.string_lit);
}

void style_markdown(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c, bool bold = false) { style_set(sci, s, c, t.editor, face, size, bold); };
    set(SCE_MARKDOWN_DEFAULT, t.text);
    set(SCE_MARKDOWN_LINE_BEGIN, t.text);
    set(SCE_MARKDOWN_STRONG1, t.text, true);
    set(SCE_MARKDOWN_STRONG2, t.text, true);
    set(SCE_MARKDOWN_EM1, t.ident);
    set(SCE_MARKDOWN_EM2, t.ident);
    set(SCE_MARKDOWN_HEADER1, t.keyword, true);
    set(SCE_MARKDOWN_HEADER2, t.keyword, true);
    set(SCE_MARKDOWN_HEADER3, t.keyword, true);
    set(SCE_MARKDOWN_HEADER4, t.keyword);
    set(SCE_MARKDOWN_HEADER5, t.keyword);
    set(SCE_MARKDOWN_HEADER6, t.keyword);
    set(SCE_MARKDOWN_PRECHAR, t.punct);
    set(SCE_MARKDOWN_ULIST_ITEM, t.punct);
    set(SCE_MARKDOWN_OLIST_ITEM, t.punct);
    set(SCE_MARKDOWN_BLOCKQUOTE, t.comment);
    set(SCE_MARKDOWN_STRIKEOUT, t.muted);
    set(SCE_MARKDOWN_HRULE, t.divider);
    set(SCE_MARKDOWN_LINK, t.amber_text);
    set(SCE_MARKDOWN_CODE, t.string_lit);
    set(SCE_MARKDOWN_CODE2, t.string_lit);
    set(SCE_MARKDOWN_CODEBK, t.string_lit);
}

void style_bash(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c, bool bold = false) { style_set(sci, s, c, t.editor, face, size, bold); };
    set(SCE_SH_DEFAULT, t.text);
    set(SCE_SH_ERROR, t.error);
    set(SCE_SH_COMMENTLINE, t.comment);
    set(SCE_SH_NUMBER, t.number);
    set(SCE_SH_WORD, t.keyword, true);
    set(SCE_SH_STRING, t.string_lit);
    set(SCE_SH_CHARACTER, t.string_lit);
    set(SCE_SH_OPERATOR, t.punct);
    set(SCE_SH_IDENTIFIER, t.ident);
    set(SCE_SH_SCALAR, t.preproc);
    set(SCE_SH_PARAM, t.preproc);
    set(SCE_SH_BACKTICKS, t.string_lit);
    set(SCE_SH_HERE_DELIM, t.comment);
    set(SCE_SH_HERE_Q, t.string_lit);
}

void style_props(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c) { style_set(sci, s, c, t.editor, face, size); };
    set(SCE_PROPS_DEFAULT, t.text);
    set(SCE_PROPS_COMMENT, t.comment);
    set(SCE_PROPS_SECTION, t.keyword);
    set(SCE_PROPS_ASSIGNMENT, t.punct);
    set(SCE_PROPS_DEFVAL, t.string_lit);
    set(SCE_PROPS_KEY, t.ident);
}

void style_yaml(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c) { style_set(sci, s, c, t.editor, face, size); };
    set(SCE_YAML_DEFAULT, t.text);
    set(SCE_YAML_COMMENT, t.comment);
    set(SCE_YAML_IDENTIFIER, t.ident);
    set(SCE_YAML_KEYWORD, t.keyword);
    set(SCE_YAML_NUMBER, t.number);
    set(SCE_YAML_REFERENCE, t.preproc);
    set(SCE_YAML_DOCUMENT, t.punct);
    set(SCE_YAML_TEXT, t.string_lit);
    set(SCE_YAML_ERROR, t.error);
    set(SCE_YAML_OPERATOR, t.punct);
}

void style_sql(HWND sci, int size) {
    const Theme& t = theme();
    const char* face = "Cascadia Mono";
    auto set = [&](int s, COLORREF c, bool bold = false) { style_set(sci, s, c, t.editor, face, size, bold); };
    set(SCE_SQL_DEFAULT, t.text);
    set(SCE_SQL_COMMENT, t.comment);
    set(SCE_SQL_COMMENTLINE, t.comment);
    set(SCE_SQL_COMMENTDOC, t.comment);
    set(SCE_SQL_NUMBER, t.number);
    set(SCE_SQL_WORD, t.keyword, true);
    set(SCE_SQL_STRING, t.string_lit);
    set(SCE_SQL_CHARACTER, t.string_lit);
    set(SCE_SQL_OPERATOR, t.punct);
    set(SCE_SQL_IDENTIFIER, t.ident);
    static const char* kw =
        "select insert update delete from where join left right inner outer on and or not null "
        "create table index view drop alter into values set as order by group having limit offset "
        "union all distinct case when then else end";
    send(sci, SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(kw));
}

std::string utf8_from_wide(std::wstring_view w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wide_from_utf8(std::string_view u) {
    if (u.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.data(), static_cast<int>(u.size()), out.data(), n);
    return out;
}

}  // namespace

bool editor_register(HINSTANCE inst) {
    return Scintilla_RegisterClasses(inst) != 0;
}

HWND editor_create(HWND parent, int control_id, HINSTANCE inst) {
    HWND sci = CreateWindowExW(0, L"Scintilla", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                               0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), inst,
                               nullptr);
    if (!sci) {
        return nullptr;
    }
    send(sci, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    send(sci, SCI_SETSCROLLWIDTHTRACKING, 1, 0);
    send(sci, SCI_SETSCROLLWIDTH, 1, 0);
    send(sci, SCI_SETENDATLASTLINE, 1, 0);  // do not scroll past last line
    send(sci, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
    send(sci, SCI_SETVIEWWS, SCWS_VISIBLEONLYININDENT, 0);
    send(sci, SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DIRECTWRITE, 0);
    send(sci, SCI_SETBUFFEREDDRAW, 1, 0);
    send(sci, SCI_SETMOUSEDOWNCAPTURES, 1, 0);
    send(sci, SCI_SETTABINDENTS, 1, 0);
    send(sci, SCI_SETBACKSPACEUNINDENTS, 1, 0);
    send(sci, SCI_AUTOCSETIGNORECASE, 1, 0);
    send(sci, SCI_SETMULTIPLESELECTION, 0, 0);
    send(sci, SCI_SETADDITIONALSELECTIONTYPING, 0, 0);
    send(sci, SCI_SETVIRTUALSPACEOPTIONS, SCVS_NONE, 0);
    send(sci, SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH, 0);
    send(sci, SCI_BRACEHIGHLIGHTINDICATOR, 1, 0);
    // Apply the shared skin here so both Fluent and Win32 editor hosts receive it.
    install_thin_scrollbar(sci, theme().editor);
    return sci;
}

void editor_apply_chrome(HWND sci, HFONT /*mono*/, int font_size_dip, int dpi) {
    if (!sci) {
        return;
    }
    const int size = (std::max)(kEditorFontPoints, MulDiv(font_size_dip, 72, 96));
    (void)dpi;
    apply_common_styles(sci, size > 20 ? kEditorFontPoints : size);
    // Line number margin
    send(sci, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(sci, SCI_SETMARGINWIDTHN, 0, send(sci, SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>("9999")));
    send(sci, SCI_SETMARGINWIDTHN, 1, 0);
    send(sci, SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    send(sci, SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.compact"), reinterpret_cast<sptr_t>("1"));
}

void editor_set_text(HWND sci, std::wstring_view text) {
    if (!sci) {
        return;
    }
    const bool was_ro = send(sci, SCI_GETREADONLY, 0, 0) != 0;
    if (was_ro) {
        send(sci, SCI_SETREADONLY, 0, 0);
    }
    const std::string u8 = utf8_from_wide(text);
    send(sci, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(u8.c_str()));
    send(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    send(sci, SCI_SETSAVEPOINT, 0, 0);
    if (was_ro) {
        send(sci, SCI_SETREADONLY, 1, 0);
    }
}

std::wstring editor_get_text(HWND sci) {
    if (!sci) {
        return {};
    }
    const sptr_t len = send(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0) {
        return {};
    }
    std::string buf(static_cast<std::size_t>(len) + 1, '\0');
    send(sci, SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(buf.data()));
    buf.resize(static_cast<std::size_t>(len));
    return wide_from_utf8(buf);
}

void editor_set_readonly(HWND sci, bool ro) {
    if (sci) {
        send(sci, SCI_SETREADONLY, ro ? 1 : 0, 0);
    }
}

void editor_clear_undo(HWND sci) {
    if (sci) {
        send(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    }
}

void editor_set_language(HWND sci, const std::wstring& path, bool large_file) {
    if (!sci) {
        return;
    }
    std::string id = detect_language_id(path);
    // Prefer Lexilla names that exist; fallback chain.
    auto try_create = [](const char* name) -> Scintilla::ILexer5* {
        return CreateLexer(name);
    };
    Scintilla::ILexer5* lex = try_create(id.c_str());
    if (!lex && id == "powershell") {
        id = "bash";
        lex = try_create("bash");
    }
    if (!lex && (id == "rust" || id == "go" || id == "ruby" || id == "phpscript" || id == "css")) {
        // css exists; if miss try null
        if (id == "css") {
            lex = try_create("css");
        }
        if (!lex) {
            id = "null";
            lex = try_create("null");
        }
    }
    if (!lex) {
        id = "null";
        lex = try_create("null");
    }
    if (lex) {
        send(sci, SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lex));
    }

    // Keep lexer styles aligned with editor chrome. A larger hardcoded value here
    // previously overrode chrome and made content much larger than the file tree.
    const int size = kEditorFontPoints;
    apply_common_styles(sci, size);
    if (id == "cpp") {
        style_cpp_family(sci, size);
        set_keywords_cpp(sci);
    } else if (id == "python") {
        style_python(sci, size);
    } else if (id == "json") {
        style_json(sci, size);
    } else if (id == "hypertext" || id == "xml") {
        style_html(sci, size);
    } else if (id == "markdown") {
        style_markdown(sci, size);
        send(sci, SCI_SETWRAPMODE, SC_WRAP_WORD, 0);
    } else if (id == "bash" || id == "powershell") {
        style_bash(sci, size);
    } else if (id == "props" || id == "cmake" || id == "makefile") {
        style_props(sci, size);
    } else if (id == "yaml") {
        style_yaml(sci, size);
    } else if (id == "sql") {
        style_sql(sci, size);
    } else if (id == "css") {
        style_cpp_family(sci, size);  // readable fallback if css styles differ
    }

    if (large_file) {
        send(sci, SCI_SETMARGINWIDTHN, 2, 0);  // hide fold
        send(sci, SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("0"));
    } else {
        send(sci, SCI_SETMARGINWIDTHN, 2, 12);
        send(sci, SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
        send(sci, SCI_COLOURISE, 0, -1);
    }

    if (id == "null" || id == "markdown") {
        // plaintext already wrapped for md
    } else {
        send(sci, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
    }
}

void editor_apply_indent(HWND sci, IndentInfo info) {
    if (!sci) {
        return;
    }
    send(sci, SCI_SETTABWIDTH, info.tab_width, 0);
    send(sci, SCI_SETUSETABS, info.use_tabs ? 1 : 0, 0);
    send(sci, SCI_SETINDENT, info.tab_width, 0);
}

void editor_save_view(HWND sci, EditorViewState* out) {
    if (!sci || !out) {
        return;
    }
    out->first_line = send(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
    out->anchor = send(sci, SCI_GETANCHOR, 0, 0);
    out->caret = send(sci, SCI_GETCURRENTPOS, 0, 0);
    out->x_offset = static_cast<int>(send(sci, SCI_GETXOFFSET, 0, 0));
}

void editor_restore_view(HWND sci, const EditorViewState& st) {
    if (!sci) {
        return;
    }
    send(sci, SCI_SETSEL, st.anchor, st.caret);
    send(sci, SCI_SETFIRSTVISIBLELINE, st.first_line, 0);
    send(sci, SCI_SETXOFFSET, st.x_offset, 0);
    send(sci, SCI_SCROLLCARET, 0, 0);
}

bool editor_find_next(HWND sci, std::wstring_view query, bool case_sensitive) {
    if (!sci || query.empty()) {
        return false;
    }
    const std::string q = utf8_from_wide(query);
    const sptr_t start = send(sci, SCI_GETSELECTIONEND, 0, 0);
    const sptr_t len = send(sci, SCI_GETLENGTH, 0, 0);
    send(sci, SCI_SETTARGETSTART, start, 0);
    send(sci, SCI_SETTARGETEND, len, 0);
    unsigned flags = SCFIND_NONE;
    if (case_sensitive) {
        flags |= SCFIND_MATCHCASE;
    }
    send(sci, SCI_SETSEARCHFLAGS, flags, 0);
    sptr_t found = send(sci, SCI_SEARCHINTARGET, q.size(), reinterpret_cast<sptr_t>(q.c_str()));
    if (found < 0 && start > 0) {
        send(sci, SCI_SETTARGETSTART, 0, 0);
        send(sci, SCI_SETTARGETEND, len, 0);
        found = send(sci, SCI_SEARCHINTARGET, q.size(), reinterpret_cast<sptr_t>(q.c_str()));
    }
    if (found < 0) {
        return false;
    }
    const sptr_t a = send(sci, SCI_GETTARGETSTART, 0, 0);
    const sptr_t b = send(sci, SCI_GETTARGETEND, 0, 0);
    send(sci, SCI_SETSEL, a, b);
    send(sci, SCI_SCROLLCARET, 0, 0);
    return true;
}

bool editor_replace_next(HWND sci, std::wstring_view query, std::wstring_view replacement, bool case_sensitive) {
    if (!sci || query.empty()) {
        return false;
    }
    if (!editor_find_next(sci, query, case_sensitive)) {
        return false;
    }
    const std::string rep = utf8_from_wide(replacement);
    send(sci, SCI_TARGETFROMSELECTION, 0, 0);
    send(sci, SCI_REPLACETARGET, rep.size(), reinterpret_cast<sptr_t>(rep.c_str()));
    return true;
}

void editor_goto_line(HWND sci, int line_1based) {
    if (!sci || line_1based <= 0) {
        return;
    }
    send(sci, SCI_GOTOLINE, line_1based - 1, 0);
    send(sci, SCI_SCROLLCARET, 0, 0);
}

EditorSel editor_selection(HWND sci) {
    EditorSel out;
    if (!sci) {
        return out;
    }
    const sptr_t a = send(sci, SCI_GETSELECTIONSTART, 0, 0);
    const sptr_t b = send(sci, SCI_GETSELECTIONEND, 0, 0);
    if (b <= a) {
        return out;
    }
    const sptr_t n = b - a;
    std::string buf(static_cast<std::size_t>(n) + 1, '\0');
    send(sci, SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
    buf.resize(static_cast<std::size_t>(n));
    out.text = wide_from_utf8(buf);
    out.start_line = static_cast<int>(send(sci, SCI_LINEFROMPOSITION, a, 0)) + 1;
    out.end_line = static_cast<int>(send(sci, SCI_LINEFROMPOSITION, b, 0)) + 1;
    return out;
}

void editor_status(HWND sci, int* line_1based, int* col_1based) {
    if (!sci) {
        return;
    }
    const sptr_t pos = send(sci, SCI_GETCURRENTPOS, 0, 0);
    const sptr_t line = send(sci, SCI_LINEFROMPOSITION, pos, 0);
    const sptr_t col = send(sci, SCI_GETCOLUMN, pos, 0);
    if (line_1based) {
        *line_1based = static_cast<int>(line) + 1;
    }
    if (col_1based) {
        *col_1based = static_cast<int>(col) + 1;
    }
}

void editor_undo(HWND sci) {
    if (sci) {
        send(sci, SCI_UNDO, 0, 0);
    }
}

void editor_redo(HWND sci) {
    if (sci) {
        send(sci, SCI_REDO, 0, 0);
    }
}

void editor_cut(HWND sci) {
    if (sci) {
        send(sci, SCI_CUT, 0, 0);
    }
}

void editor_copy(HWND sci) {
    if (sci) {
        send(sci, SCI_COPY, 0, 0);
    }
}

void editor_paste(HWND sci) {
    if (sci) {
        send(sci, SCI_PASTE, 0, 0);
    }
}

void editor_select_all(HWND sci) {
    if (sci) {
        send(sci, SCI_SELECTALL, 0, 0);
    }
}

void editor_set_word_wrap(HWND sci, bool on) {
    if (sci) {
        send(sci, SCI_SETWRAPMODE, on ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
    }
}

void editor_set_whitespace(HWND sci, bool visible) {
    if (sci) {
        send(sci, SCI_SETVIEWWS, visible ? SCWS_VISIBLEALWAYS : SCWS_VISIBLEONLYININDENT, 0);
    }
}

HWND editor_create_minimap(HWND parent, HWND primary, int control_id, HINSTANCE inst, int /*dpi*/) {
    if (!parent || !primary) return nullptr;
    HWND minimap = editor_create(parent, control_id, inst);
    if (!minimap) return nullptr;

    const sptr_t document = send(primary, SCI_GETDOCPOINTER);
    send(primary, SCI_ADDREFDOCUMENT, 0, document);
    send(minimap, SCI_SETDOCPOINTER, 0, document);
    // Scintilla documents share text and lexer style bytes, but each view owns its
    // visual style table. Copy it after attaching the document or the secondary
    // view renders those shared styles with Scintilla's white system defaults.
    copy_view_styles(primary, minimap);
    send(minimap, SCI_SETREADONLY, 1);
    send(minimap, SCI_SETZOOM, static_cast<uptr_t>(-7));
    send(minimap, SCI_SETWRAPMODE, SC_WRAP_NONE);
    send(minimap, SCI_SETHSCROLLBAR, 0);
    send(minimap, SCI_SETVSCROLLBAR, 0);
    send(minimap, SCI_SETMARGINWIDTHN, 0, 0);
    send(minimap, SCI_SETMARGINWIDTHN, 1, 0);
    send(minimap, SCI_SETMARGINWIDTHN, 2, 0);
    send(minimap, SCI_SETINDENTATIONGUIDES, SC_IV_NONE);
    send(minimap, SCI_SETVIEWWS, SCWS_INVISIBLE);
    send(minimap, SCI_SETCARETSTYLE, CARETSTYLE_INVISIBLE);
    send(minimap, SCI_SETCARETLINEVISIBLE, 0);
    send(minimap, SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    send(minimap, SCI_SETSELALPHA, 48);
    send(minimap, SCI_SETSELFORE, 0);
    send(minimap, SCI_SETSELBACK, 1, theme().surface_active);
    send(minimap, SCI_USEPOPUP, SC_POPUP_NEVER);

    LONG_PTR style = GetWindowLongPtrW(minimap, GWL_STYLE);
    SetWindowLongPtrW(minimap, GWL_STYLE, style & ~WS_TABSTOP);
    auto* state = new MinimapState{primary, false};
    if (!SetWindowSubclass(minimap, minimap_subclass, 1, reinterpret_cast<DWORD_PTR>(state))) {
        delete state;
        DestroyWindow(minimap);
        return nullptr;
    }
    return minimap;
}

void editor_sync_minimap(HWND primary, HWND minimap) {
    if (!primary || !minimap) return;
    const int lines = static_cast<int>(send(primary, SCI_GETLINECOUNT));
    const int first_display = static_cast<int>(send(primary, SCI_GETFIRSTVISIBLELINE));
    const int visible = (std::max)(1, static_cast<int>(send(primary, SCI_LINESONSCREEN)));
    const int first = static_cast<int>(send(primary, SCI_DOCLINEFROMVISIBLE, first_display));
    const int last = (std::min)(lines - 1,
        static_cast<int>(send(primary, SCI_DOCLINEFROMVISIBLE, first_display + visible)));
    const sptr_t start = send(primary, SCI_POSITIONFROMLINE, (std::max)(0, first));
    const sptr_t end = last + 1 < lines ? send(primary, SCI_POSITIONFROMLINE, last + 1)
                                       : send(primary, SCI_GETLENGTH);
    send(minimap, SCI_SETSEL, static_cast<uptr_t>(start), end);
    const int mini_visible = (std::max)(1, static_cast<int>(send(minimap, SCI_LINESONSCREEN)));
    send(minimap, SCI_SETFIRSTVISIBLELINE, (std::max)(0, first - mini_visible / 2));
}

}  // namespace scyllagpt
