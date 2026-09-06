#pragma once

#include <string>
#include <string_view>

namespace scyllagpt {

struct IndentInfo {
    bool use_tabs = false;
    int tab_width = 4;
};

std::string detect_language_id(const std::wstring& path);
IndentInfo editor_detect_indent(std::wstring_view text);

}  // namespace scyllagpt
