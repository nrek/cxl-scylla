#include "path.h"

#include <windows.h>
#include <cwctype>
#include <string_view>
#include <vector>

namespace scylla {
namespace {

std::wstring strip_extended(std::wstring p) {
    const std::wstring prefix = L"\\\\?\\";
    if (p.rfind(prefix, 0) == 0) {
        p.erase(0, prefix.size());
    }
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
        if (p.size() <= 3 && p.size() >= 2 && p[1] == L':') {
            break;
        }
        p.pop_back();
    }
    return p;
}

}  // namespace

std::wstring win32_error(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = L"win32 " + std::to_wstring(code);
    if (n && buf) {
        msg.push_back(L' ');
        msg += buf;
        while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r')) {
            msg.pop_back();
        }
        LocalFree(buf);
    }
    return msg;
}

bool path_is_unc(const std::wstring& p) {
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') {
        return true;
    }
    if (p.size() >= 2 && p[0] == L'/' && p[1] == L'/') {
        return true;
    }
    return false;
}

bool path_is_drive_root(const std::wstring& canonical) {
    std::wstring p = strip_extended(canonical);
    if (p.size() == 2 && p[1] == L':') {
        return true;
    }
    if (p.size() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) {
        return true;
    }
    return false;
}

PathCheck validate_workspace(const std::wstring& requested) {
    PathCheck r;
    if (requested.empty()) {
        r.reason = L"workspace path is empty";
        return r;
    }
    std::wstring in = requested;
    while (!in.empty() && (in.front() == L' ' || in.front() == L'\t')) {
        in.erase(in.begin());
    }
    if (path_is_unc(in)) {
        r.reason = L"UNC/network paths are rejected in v0.0.1";
        return r;
    }
    wchar_t full[MAX_PATH * 4]{};
    const DWORD n = GetFullPathNameW(in.c_str(), static_cast<DWORD>(sizeof(full) / sizeof(full[0])), full, nullptr);
    if (n == 0 || n >= (sizeof(full) / sizeof(full[0]))) {
        r.reason = L"could not canonicalize path: " + win32_error(GetLastError());
        return r;
    }
    std::wstring canon = strip_extended(full);
    r.canonical = canon;
    if (path_is_unc(canon)) {
        r.reason = L"canonical path is UNC; refused";
        return r;
    }
    if (path_is_drive_root(canon)) {
        r.reason = L"drive root is not a valid workspace: " + canon;
        return r;
    }
    const DWORD attr = GetFileAttributesW(canon.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        r.reason = L"path does not exist: " + canon;
        return r;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        r.reason = L"path is not a directory: " + canon;
        return r;
    }
    r.ok = true;
    return r;
}

std::wstring path_dirname(const std::wstring& p) {
    const auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return p.substr(0, pos);
}

std::wstring path_filename(const std::wstring& p) {
    const auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return p;
    }
    return p.substr(pos + 1);
}

std::wstring path_lower(const std::wstring& p) {
    std::wstring s = p;
    for (auto& c : s) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return s;
}

bool path_is_under(const std::wstring& candidate, const std::wstring& root) {
    wchar_t cfull[MAX_PATH * 4]{};
    wchar_t rfull[MAX_PATH * 4]{};
    if (!GetFullPathNameW(candidate.c_str(), MAX_PATH * 4, cfull, nullptr)) {
        return false;
    }
    if (!GetFullPathNameW(root.c_str(), MAX_PATH * 4, rfull, nullptr)) {
        return false;
    }
    std::wstring c = path_lower(strip_extended(cfull));
    std::wstring r = path_lower(strip_extended(rfull));
    if (c == r) {
        return true;
    }
    if (r.empty()) {
        return false;
    }
    if (r.back() != L'\\') {
        r.push_back(L'\\');
    }
    return c.rfind(r, 0) == 0;
}

bool path_is_windowsapps(const std::wstring& p) {
    return path_lower(p).find(L"\\windowsapps\\") != std::wstring::npos;
}

std::wstring package_full_name_from_windowsapps_path(const std::wstring& p) {
    const std::wstring low = path_lower(p);
    const auto pos = low.find(L"\\windowsapps\\");
    if (pos == std::wstring::npos) {
        return L"";
    }
    const std::wstring rest = p.substr(pos + 13);
    const auto slash = rest.find_first_of(L"\\/");
    return slash == std::wstring::npos ? rest : rest.substr(0, slash);
}

std::wstring package_family_from_windowsapps_path(const std::wstring& p) {
    const std::wstring full = package_full_name_from_windowsapps_path(p);
    const auto first = full.find(L'_');
    const auto last = full.rfind(L'_');
    if (first == std::wstring::npos || last == std::wstring::npos || last <= first) {
        return L"";
    }
    return full.substr(0, first) + L"_" + full.substr(last + 1);
}

bool packaged_is_full_trust(const std::wstring& exe_or_root) {
    if (exe_or_root.empty()) {
        return false;
    }
    std::vector<std::wstring> manifests;
    const DWORD attr = GetFileAttributesW(exe_or_root.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        manifests.push_back(exe_or_root + L"\\AppxManifest.xml");
    } else {
        const std::wstring dir = path_dirname(exe_or_root);
        manifests.push_back(dir + L"\\AppxManifest.xml");
        const std::wstring parent = path_dirname(dir);
        if (!parent.empty() && parent != dir) {
            manifests.push_back(parent + L"\\AppxManifest.xml");
        }
    }
    auto contains = [](const std::wstring& path, const char* needle) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        std::vector<char> buf(512 * 1024);
        DWORD n = 0;
        const BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr);
        CloseHandle(h);
        if (!ok || n == 0) {
            return false;
        }
        const std::string_view hay(buf.data(), n);
        return hay.find(needle) != std::string_view::npos;
    };
    for (const auto& m : manifests) {
        if (contains(m, "runFullTrust") || contains(m, "Windows.FullTrustApplication")) {
            return true;
        }
    }
    return false;
}

}  // namespace scylla
