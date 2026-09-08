#pragma once
#include <string>
#include <windows.h>

namespace scylla {

struct PathCheck {
    bool ok = false;
    std::wstring canonical;
    std::wstring reason;
};

std::wstring win32_error(DWORD code);
PathCheck validate_workspace(const std::wstring& requested);
bool path_is_unc(const std::wstring& p);
bool path_is_drive_root(const std::wstring& canonical);
std::wstring path_dirname(const std::wstring& p);
std::wstring path_filename(const std::wstring& p);
bool path_is_under(const std::wstring& candidate, const std::wstring& root);
std::wstring path_lower(const std::wstring& p);
bool path_is_windowsapps(const std::wstring& p);
std::wstring package_full_name_from_windowsapps_path(const std::wstring& p);
std::wstring package_family_from_windowsapps_path(const std::wstring& p);
bool packaged_is_full_trust(const std::wstring& exe_or_root);

}  // namespace scylla
