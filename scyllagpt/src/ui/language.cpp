#include "scyllagpt/language.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {
namespace {

std::wstring lower_ext(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    const auto base = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const auto dot = base.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return L"";
    }
    std::wstring e = base.substr(dot);
    CharLowerBuffW(e.data(), static_cast<DWORD>(e.size()));
    return e;
}

std::wstring lower_name(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
    CharLowerBuffW(base.data(), static_cast<DWORD>(base.size()));
    return base;
}

}  // namespace

std::string detect_language_id(const std::wstring& path) {
    const std::wstring name = lower_name(path);
    if (name == L"dockerfile" || name.rfind(L"dockerfile.", 0) == 0) {
        return "bash";
    }
    if (name == L"makefile" || name == L"gnumakefile") {
        return "makefile";
    }
    if (name == L"cmakelists.txt") {
        return "cmake";
    }
    if (name == L".gitignore" || name == L".gitattributes" || name == L".editorconfig" || name == L"license") {
        return "props";
    }
    const std::wstring e = lower_ext(path);
    if (e == L".cpp" || e == L".cc" || e == L".cxx" || e == L".c" || e == L".h" || e == L".hpp" || e == L".hh" ||
        e == L".hxx" || e == L".inl" || e == L".ixx" || e == L".cs" || e == L".js" || e == L".jsx" || e == L".mjs" ||
        e == L".cjs" || e == L".ts" || e == L".tsx" || e == L".java") {
        return "cpp";
    }
    if (e == L".py" || e == L".pyw" || e == L".pyi") {
        return "python";
    }
    if (e == L".json" || e == L".jsonc") {
        return "json";
    }
    if (e == L".html" || e == L".htm" || e == L".xhtml" || e == L".xml" || e == L".svg" || e == L".xaml") {
        return "hypertext";
    }
    if (e == L".css" || e == L".scss" || e == L".less") {
        return "css";
    }
    if (e == L".md" || e == L".mdx" || e == L".markdown") {
        return "markdown";
    }
    if (e == L".ps1" || e == L".psm1" || e == L".psd1") {
        return "powershell";
    }
    if (e == L".sh" || e == L".bash" || e == L".zsh" || e == L".fish") {
        return "bash";
    }
    if (e == L".yml" || e == L".yaml") {
        return "yaml";
    }
    if (e == L".toml" || e == L".ini" || e == L".conf" || e == L".cfg" || e == L".env" || e == L".properties") {
        return "props";
    }
    if (e == L".sql") {
        return "sql";
    }
    if (e == L".rs") {
        return "rust";
    }
    if (e == L".go") {
        return "go";
    }
    if (e == L".php") {
        return "phpscript";
    }
    if (e == L".rb") {
        return "ruby";
    }
    if (e == L".cmake") {
        return "cmake";
    }
    if (e == L".mak" || e == L".mk") {
        return "makefile";
    }
    return "null";
}

IndentInfo editor_detect_indent(std::wstring_view text) {
    IndentInfo info;
    int space_lines = 0;
    int tab_lines = 0;
    int space_widths[9]{};
    std::size_t i = 0;
    while (i < text.size() && (space_lines + tab_lines) < 200) {
        int sp = 0;
        bool tabs = false;
        while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) {
            if (text[i] == L'\t') {
                tabs = true;
            } else {
                ++sp;
            }
            ++i;
        }
        if (i < text.size() && text[i] != L'\r' && text[i] != L'\n') {
            if (tabs) {
                ++tab_lines;
            } else if (sp > 0 && sp <= 8) {
                ++space_lines;
                ++space_widths[sp];
            }
        }
        while (i < text.size() && text[i] != L'\n') {
            ++i;
        }
        if (i < text.size()) {
            ++i;
        }
    }
    if (tab_lines > space_lines) {
        info.use_tabs = true;
        info.tab_width = 4;
        return info;
    }
    int best = 4;
    int best_n = 0;
    for (int w : {2, 4, 8, 3}) {
        if (space_widths[w] > best_n) {
            best_n = space_widths[w];
            best = w;
        }
    }
    info.use_tabs = false;
    info.tab_width = best;
    return info;
}

}  // namespace scyllagpt
