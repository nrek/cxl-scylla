#pragma once

#include <string>
#include <string_view>

namespace scyllagpt {

std::wstring utf16(std::string_view utf8);
std::string utf8(std::wstring_view utf16);
std::wstring narrow_to_wide(const std::string& s);

}  // namespace scyllagpt
