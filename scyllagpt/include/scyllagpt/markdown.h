#pragma once
#include <string>
#include <vector>
#include <cwctype>

namespace scyllagpt {
struct MarkdownRun {
    std::wstring text, link;
    bool bold = false, italic = false, code = false, strike = false;
    int heading = 0;
};
inline void markdown_inline(const std::wstring& text, std::vector<MarkdownRun>& out, MarkdownRun style = {}, int depth = 0) {
    if (depth > 12) { style.text = text; out.push_back(style); return; }
    std::wstring plain;
    auto flush = [&] { if (!plain.empty()) { auto run = style; run.text = plain; out.push_back(run); plain.clear(); } };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == L'\\' && i + 1 < text.size() && std::iswpunct(text[i + 1])) { plain += text[i + 1]; i += 2; continue; }
        if (text[i] == L'`') {
            auto n = std::size_t{1}; while (i + n < text.size() && text[i + n] == L'`') ++n;
            const auto end = text.find(std::wstring(n, L'`'), i + n);
            if (end != std::wstring::npos) {
                flush(); auto run = style; run.code = true; run.text = text.substr(i + n, end - i - n); out.push_back(run); i = end + n; continue;
            }
        }
        if (text[i] == L'<' && (text.compare(i + 1, 8, L"https://") == 0 || text.compare(i + 1, 7, L"http://") == 0)) {
            const auto end = text.find(L'>', i + 1);
            if (end != std::wstring::npos) {
                flush(); auto run = style; run.text = run.link = text.substr(i + 1, end - i - 1);
                out.push_back(run); i = end + 1; continue;
            }
        }
        if (text[i] == L'[') {
            const auto close = text.find(L"](", i + 1);
            if (close != std::wstring::npos) {
                auto end = close + 2; int balance = 1; bool angle = end < text.size() && text[end] == L'<';
                for (; end < text.size(); ++end) {
                    if (text[end] == L'\\') { ++end; continue; }
                    if (angle) { if (text[end] == L'>') angle = false; continue; }
                    if (text[end] == L'(') ++balance;
                    if (text[end] == L')' && --balance == 0) break;
                }
                if (end < text.size()) {
                    auto target = text.substr(close + 2, end - close - 2);
                    if (target.size() >= 2 && target.front() == L'<' && target.back() == L'>') target = target.substr(1, target.size() - 2);
                    flush(); auto run = style; run.link = target;
                    markdown_inline(text.substr(i + 1, close - i - 1), out, run, depth + 1); i = end + 1; continue;
                }
            }
        }
        bool handled = false;
        for (const auto& delimiter : {std::wstring(L"**"), std::wstring(L"__"), std::wstring(L"~~"), std::wstring(L"*"), std::wstring(L"_")}) {
            if (text.compare(i, delimiter.size(), delimiter) != 0) continue;
            if (delimiter == L"_" && i && std::iswalnum(text[i - 1])) continue;
            const auto end = text.find(delimiter, i + delimiter.size());
            if (end == std::wstring::npos || end == i + delimiter.size()) continue;
            flush(); auto run = style;
            if (delimiter == L"~~") run.strike = true;
            else if (delimiter.size() == 2) run.bold = true;
            else run.italic = true;
            markdown_inline(text.substr(i + delimiter.size(), end - i - delimiter.size()), out, run, depth + 1);
            i = end + delimiter.size(); handled = true; break;
        }
        if (!handled) plain += text[i++];
    }
    flush();
}
inline std::vector<MarkdownRun> parse_markdown(std::wstring text) {
    // Normalize CRLF/CR once; RichEdit character positions use a single CR.
    std::wstring normalized;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') { normalized += L'\n'; if (i + 1 < text.size() && text[i + 1] == L'\n') ++i; }
        else normalized += text[i];
    }
    std::vector<MarkdownRun> out;
    bool fenced = false; wchar_t fence = 0; std::size_t fence_length = 0;
    for (std::size_t start = 0; start < normalized.size();) {
        auto end = normalized.find(L'\n', start); if (end == std::wstring::npos) end = normalized.size();
        auto line = normalized.substr(start, end - start); start = end < normalized.size() ? end + 1 : end;
        auto first = line.find_first_not_of(L' '); if (first == std::wstring::npos) first = line.size();
        auto marker = first; while (marker < line.size() && (line[marker] == L'`' || line[marker] == L'~') && line[marker] == line[first]) ++marker;
        if (first <= 3 && marker - first >= 3 && (!fenced || (line[first] == fence && marker - first >= fence_length && line.find_first_not_of(L" \t", marker) == std::wstring::npos))) {
            if (!fenced) { fence = line[first]; fence_length = marker - first; }
            fenced = !fenced; continue;
        }
        MarkdownRun style;
        if (fenced) { style.code = true; style.text = line + L"\r"; out.push_back(style); continue; }
        if (first <= 3 && first < line.size() && line[first] == L'#') {
            auto n = first; while (n < line.size() && line[n] == L'#') ++n;
            if (n - first <= 6 && (n == line.size() || line[n] == L' ')) {
                style.heading = static_cast<int>(n - first); style.bold = true; line = line.substr(n == line.size() ? n : n + 1);
            }
        } else if (first + 1 < line.size() && (line[first] == L'-' || line[first] == L'*' || line[first] == L'+') && line[first + 1] == L' ') {
            line = line.substr(0, first) + L"• " + line.substr(first + 2);
        } else if (first < line.size() && line[first] == L'>') {
            line = L"│ " + line.substr(first + (first + 1 < line.size() && line[first + 1] == L' ' ? 2 : 1)); style.italic = true;
        }
        if (line == L"---" || line == L"***" || line == L"___") line = L"────────────────────────";
        markdown_inline(line, out, style);
        style.text = L"\r"; out.push_back(style);
    }
    return out;
}
} // namespace scyllagpt
