#pragma once
#include <string>
#include <windows.h>

namespace scylla {

bool grant_sid_access(const std::wstring& path, PSID sid, DWORD access, std::wstring& err);
bool revoke_sid_access(const std::wstring& path, PSID sid, std::wstring& err);
bool dacl_has_deny_for_sid(const std::wstring& path, PSID sid);

}  // namespace scylla
