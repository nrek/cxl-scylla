#pragma once

// Composer / chat-log token helpers: /scylla-query, !scylla_ Keyring names, @file links.
// Names and paths only — never secret values.

#include "scyllagpt/keyring.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

struct CharSpan {
    std::size_t begin = 0;
    std::size_t end = 0;  // exclusive
};

inline bool ieq_ascii(wchar_t a, wchar_t b) {
    if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
    return a == b;
}

inline bool starts_with_ci(std::wstring_view hay, std::wstring_view needle) {
    if (hay.size() < needle.size()) return false;
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (!ieq_ascii(hay[i], needle[i])) return false;
    }
    return true;
}

// All whitespace-bounded `/scylla-query` tokens (exact command, not /scylla-querying).
inline std::vector<CharSpan> find_scylla_query_spans(std::wstring_view text) {
    std::vector<CharSpan> out;
    static constexpr wchar_t kCmd[] = L"/scylla-query";
    constexpr std::size_t kLen = 13;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != L'/') {
            ++i;
            continue;
        }
        if (i > 0 && !iswspace(text[i - 1])) {
            ++i;
            continue;
        }
        if (i + kLen > text.size()) break;
        bool match = true;
        for (std::size_t j = 0; j < kLen; ++j) {
            if (!ieq_ascii(text[i + j], kCmd[j])) {
                match = false;
                break;
            }
        }
        if (!match) {
            ++i;
            continue;
        }
        const std::size_t after = i + kLen;
        if (after < text.size()) {
            const wchar_t next = text[after];
            if (next != L' ' && next != L'\t' && next != L'\r' && next != L'\n') {
                i = after;
                continue;
            }
        }
        out.push_back({i, after});
        i = after;
    }
    return out;
}

inline bool slash_skill_matches_prefix(std::wstring_view token) {
    if (token.empty() || token[0] != L'/') return false;
    // Token must be a prefix of the skill command (e.g. /scy → /scylla-query).
    return starts_with_ci(L"/scylla-query", token);
}

// Completion insert text for slash popup (single skill in v1).
inline std::wstring slash_skill_completion_for_token(std::wstring_view token) {
    if (!slash_skill_matches_prefix(token)) return {};
    return L"/scylla-query";
}

inline std::string normalize_bang_keyring_insert(std::string_view name) {
    std::string n(name);
    while (!n.empty() && (n.front() == '!' || n.front() == ' ')) n.erase(n.begin());
    if (n.rfind("scylla_", 0) != 0) {
        n.insert(0, "scylla_");
    }
    return "!" + n;
}

// The typed token is matched against the form that will be inserted (scylla_<name>), with `-`
// and `_` treated as the same separator. So `!scy`, `!scylla_rds`, and `!rds` all reach a Keyring
// entry named `rds-scylla-user`.
inline bool keyring_name_matches_bang_filter(std::string_view name, std::wstring_view bang_token) {
    if (bang_token.empty() || bang_token[0] != L'!') return false;
    const auto canon = [](std::string_view in) {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            out.push_back(c == '-' ? '_' : c);
        }
        return out;
    };
    std::string filter;
    for (std::size_t i = 1; i < bang_token.size(); ++i) {
        const wchar_t c = bang_token[i];
        if (c < 128) filter.push_back(static_cast<char>(c));
    }
    if (filter.empty()) return true;
    const auto insert = normalize_bang_keyring_insert(name);
    return canon(insert.substr(1)).find(canon(filter)) != std::string::npos;
}

inline std::vector<std::pair<std::string, std::wstring>> filter_keyring_bang_completions(
    const std::vector<SecretRef>& refs, std::wstring_view bang_token) {
    std::vector<std::pair<std::string, std::wstring>> out;
    if (bang_token.empty() || bang_token[0] != L'!') return out;
    for (const auto& r : refs) {
        if (!keyring_name_matches_bang_filter(r.name, bang_token)) continue;
        const std::wstring desc = r.description.empty()
                                      ? std::wstring(r.name.begin(), r.name.end())
                                      : std::wstring(r.description.begin(), r.description.end());
        out.emplace_back(normalize_bang_keyring_insert(r.name), desc);
    }
    return out;
}

struct AtFileLinkSpan {
    std::size_t begin = 0;
    std::size_t end = 0;       // exclusive display span in text
    std::wstring path;         // absolute or as written (unquoted)
    std::wstring display;      // visible label (basename or full)
};

// Finds @"path" and @token (no spaces) file-like mentions.
inline std::vector<AtFileLinkSpan> find_at_file_spans(std::wstring_view text) {
    std::vector<AtFileLinkSpan> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'@') continue;
        if (i + 1 >= text.size()) break;
        if (text[i + 1] == L'"') {
            const std::size_t start_path = i + 2;
            const std::size_t close = text.find(L'"', start_path);
            if (close == std::wstring_view::npos) continue;
            AtFileLinkSpan span;
            span.begin = i;
            span.end = close + 1;
            span.path.assign(text.substr(start_path, close - start_path));
            const auto slash = span.path.find_last_of(L"\\/");
            span.display = slash == std::wstring::npos ? span.path : span.path.substr(slash + 1);
            out.push_back(std::move(span));
            i = close;
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size() && !iswspace(text[j]) && text[j] != L'@') ++j;
        if (j == i + 1) continue;
        const auto token = text.substr(i + 1, j - (i + 1));
        // Require a path-ish token (slash, drive, or extension).
        const bool pathish = token.find(L'/') != std::wstring_view::npos ||
                             token.find(L'\\') != std::wstring_view::npos ||
                             (token.size() >= 2 && token[1] == L':') ||
                             token.find(L'.') != std::wstring_view::npos;
        if (!pathish) continue;
        AtFileLinkSpan span;
        span.begin = i;
        span.end = j;
        span.path.assign(token);
        const auto slash = span.path.find_last_of(L"\\/");
        span.display = slash == std::wstring::npos ? span.path : span.path.substr(slash + 1);
        out.push_back(std::move(span));
        i = j - 1;
    }
    return out;
}

inline bool composer_text_has_bang_keyring_token(std::wstring_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'!') continue;
        if (i > 0 && !iswspace(text[i - 1])) continue;
        if (starts_with_ci(text.substr(i), L"!scylla_")) return true;
        // !scy… still counts once completed as scylla_ via autocomplete; also bare !scylla_
        if (i + 1 < text.size()) {
            std::size_t j = i + 1;
            while (j < text.size() && !iswspace(text[j])) ++j;
            const auto tok = text.substr(i, j - i);
            if (tok.size() > 1 && starts_with_ci(tok, L"!scylla")) return true;
        }
    }
    return false;
}

inline std::string keyring_bang_token_instructions() {
    return "Keyring references in this message use the form !scylla_NAME (or !NAME when already "
           "prefixed). Each token is a Keyring secret NAME only — never a secret value. Map roles "
           "to these names; do not ask the user to paste passwords or keys.\n\n";
}

}  // namespace scyllagpt
