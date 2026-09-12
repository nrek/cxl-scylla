#pragma once
#include "scyllagpt/chat_history.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/utf.h"
#include <cwctype>

namespace scyllagpt {
inline bool mentions_project(std::wstring text, std::wstring name) {
    if (name.empty()) return false;
    for (auto& c : text) c = std::towlower(c);
    for (auto& c : name) c = std::towlower(c);
    auto word = [](wchar_t c) { return std::iswalnum(c) || c == L'-' || c == L'_'; };
    for (auto pos = text.find(name); pos != std::wstring::npos; pos = text.find(name, pos + 1)) {
        const auto end = pos + name.size();
        if ((pos == 0 || !word(text[pos - 1])) && (end == text.size() || !word(text[end]))) return true;
    }
    return false;
}

inline Json chat_project_paths(const WorkspaceStore& store, const Conversation* chat,
                               const std::wstring& cwd, const std::string& title, const std::string& preview) {
    std::wstring text = utf16(visible_chat_text(title) + "\n" + visible_chat_text(visible_user_text(preview)));
    if (chat) for (const auto& message : chat->local_messages.array_items()) {
        const auto body = message.at("text").as_string();
        text += L"\n" + utf16(message.at("user").as_bool(false) ? visible_user_text(body) : visible_chat_text(body));
    }
    const auto* owner = chat ? store.by_id(chat->project_id) : nullptr;
    auto primary = canonicalize_path(owner ? owner->root : cwd);
    for (auto& c : primary) c = std::towlower(c);
    Json paths = Json::array();
    for (const auto& root : store.open_roots()) {
        auto canonical = canonicalize_path(root);
        for (auto& c : canonical) c = std::towlower(c);
        const auto slash = canonical.find_last_of(L"\\/");
        const auto name = slash == std::wstring::npos ? canonical : canonical.substr(slash + 1);
        const bool owns = primary == canonical || (primary.size() > canonical.size()
            && primary.compare(0, canonical.size(), canonical) == 0
            && (primary[canonical.size()] == L'\\' || primary[canonical.size()] == L'/'));
        if (owns || mentions_project(text, name)) paths.push(Json::string(utf8(root)));
    }
    return paths;
}
}
