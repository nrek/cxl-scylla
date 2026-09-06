#include "discover.h"

#include "path.h"

#include <windows.h>
#include <objidl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace scylla {
namespace {

std::string utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string s(static_cast<size_t>(n - 1), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string json_escape(const std::wstring& w) {
    const std::string u = utf8(w);
    std::string o;
    o.reserve(u.size() + 8);
    for (unsigned char c : u) {
        if (c == '"') {
            o += "\\\"";
        } else if (c == '\\') {
            o += "\\\\";
        } else if (c == '\n') {
            o += "\\n";
        } else if (c < 0x20) {
            continue;
        } else {
            o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

bool has_flag(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc; ++i) {
        if (key == argv[i]) {
            return true;
        }
    }
    return false;
}

std::wstring arg_after(int argc, wchar_t** argv, const std::wstring& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (key == argv[i]) {
            return argv[i + 1];
        }
    }
    return L"";
}

std::wstring module_dir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return path_dirname(exe);
}

std::wstring canonical_file(const std::wstring& p) {
    if (p.empty()) {
        return L"";
    }
    wchar_t full[MAX_PATH * 4]{};
    if (!GetFullPathNameW(p.c_str(), MAX_PATH * 4, full, nullptr)) {
        return L"";
    }
    const DWORD a = GetFileAttributesW(full);
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"";
    }
    return full;
}

std::wstring strip_icon_index(std::wstring s) {
    if (s.size() >= 2 && s.front() == L'"') {
        s.erase(s.begin());
        const auto q = s.find(L'"');
        if (q != std::wstring::npos) {
            s = s.substr(0, q);
        }
    }
    const auto comma = s.rfind(L',');
    if (comma != std::wstring::npos && comma > 2) {
        bool digits = true;
        for (size_t i = comma + 1; i < s.size(); ++i) {
            if (s[i] != L'-' && (s[i] < L'0' || s[i] > L'9')) {
                digits = false;
                break;
            }
        }
        if (digits) {
            s = s.substr(0, comma);
        }
    }
    while (!s.empty() && s.back() == L' ') {
        s.pop_back();
    }
    return s;
}

bool contains_ci(const std::wstring& hay, const std::wstring& needle) {
    return path_lower(hay).find(path_lower(needle)) != std::wstring::npos;
}

bool skip_other_name(const std::wstring& name) {
    static const wchar_t* noise[] = {
        L"Visual C++", L"Redistributable", L"Windows SDK", L"Update for", L"Hotfix",
        L"Driver", L".NET Runtime", L"Microsoft Edge WebView", L"Installer",
        L"Uninstall", L"Setup",
    };
    for (const auto* n : noise) {
        if (contains_ci(name, n)) {
            return true;
        }
    }
    return name.empty();
}

std::wstring known_id_for(const DiscoveredApp& a) {
    const std::wstring exe = path_lower(path_filename(a.executable));
    const std::wstring name = path_lower(a.display_name);
    const std::wstring pub = path_lower(a.publisher);
    const std::wstring family = path_lower(a.package_family_name);
    if (exe == L"scylla-test-parent.exe") {
        return L"scylla-test-parent";
    }
    if (exe == L"chatbox.exe" || name.find(L"chatbox") != std::wstring::npos) {
        return L"chatbox";
    }
    if (exe == L"chatgpt.exe" || name.find(L"chatgpt") != std::wstring::npos ||
        family.find(L"openai.chatgpt") != std::wstring::npos) {
        return L"chatgpt";
    }
    if (family.find(L"openai.codex") != std::wstring::npos) {
        if (name.find(L"codex") != std::wstring::npos && name.find(L"chatgpt") == std::wstring::npos) {
            return L"codex";
        }
        return L"chatgpt";
    }
    if (exe == L"cursor.exe" || name.find(L"cursor") != std::wstring::npos || pub.find(L"anysphere") != std::wstring::npos) {
        if (name.find(L"insider") != std::wstring::npos) {
            return L"cursor-insiders";
        }
        return L"cursor";
    }
    if (exe == L"codex.exe" || (name.find(L"codex") != std::wstring::npos && name.find(L"chatgpt") == std::wstring::npos)) {
        return L"codex";
    }
    if (exe == L"claude.exe" || name.find(L"claude") != std::wstring::npos) {
        return L"claude-code";
    }
    return L"";
}

void add_candidate(std::vector<DiscoveredApp>& out, DiscoveredApp a) {
    if (!a.executable.empty()) {
        a.executable = canonical_file(a.executable);
        if (a.executable.empty()) {
            return;
        }
        if (a.install_root.empty()) {
            a.install_root = path_dirname(a.executable);
        }
    }
    if (a.compatibility.empty()) {
        a.compatibility = a.available ? L"discovered" : L"unknown";
    }
    if (a.status.empty()) {
        a.status = a.available ? L"installed" : L"not_found";
    }
    out.push_back(std::move(a));
}

void collect_uninstall(HKEY root, const wchar_t* sub, std::vector<DiscoveredApp>& out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[256]{};
        DWORD nlen = 256;
        if (RegEnumKeyExW(k, i, name, &nlen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY item = nullptr;
        if (RegOpenKeyExW(k, name, 0, KEY_READ, &item) != ERROR_SUCCESS) {
            continue;
        }
        auto read = [&](const wchar_t* value) {
            wchar_t buf[MAX_PATH * 2]{};
            DWORD sz = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExW(item, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz) != ERROR_SUCCESS) {
                return std::wstring{};
            }
            return std::wstring(buf);
        };
        const std::wstring display = read(L"DisplayName");
        const std::wstring publisher = read(L"Publisher");
        const std::wstring loc = read(L"InstallLocation");
        const std::wstring icon = strip_icon_index(read(L"DisplayIcon"));
        RegCloseKey(item);
        if (display.empty() || skip_other_name(display)) {
            continue;
        }
        DiscoveredApp a;
        a.display_name = display;
        a.publisher = publisher;
        a.kind = L"win32";
        a.discovery_source = L"registry";
        a.group = L"other";
        std::wstring exe = canonical_file(icon);
        if (exe.empty() && !loc.empty()) {
            const std::wstring try1 = loc + L"\\" + display + L".exe";
            exe = canonical_file(try1);
        }
        if (exe.empty()) {
            continue;
        }
        a.executable = exe;
        a.available = true;
        add_candidate(out, std::move(a));
    }
    RegCloseKey(k);
}

void collect_app_paths(HKEY root, std::vector<DiscoveredApp>& out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths", 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return;
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[256]{};
        DWORD nlen = 256;
        if (RegEnumKeyExW(k, i, name, &nlen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY item = nullptr;
        if (RegOpenKeyExW(k, name, 0, KEY_READ, &item) != ERROR_SUCCESS) {
            continue;
        }
        wchar_t buf[MAX_PATH * 2]{};
        DWORD sz = sizeof(buf);
        DWORD type = 0;
        LONG st = RegQueryValueExW(item, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz);
        RegCloseKey(item);
        if (st != ERROR_SUCCESS) {
            continue;
        }
        DiscoveredApp a;
        a.display_name = name;
        a.kind = L"win32";
        a.discovery_source = L"app_paths";
        a.group = L"other";
        a.executable = buf;
        a.available = true;
        add_candidate(out, std::move(a));
    }
    RegCloseKey(k);
}

void collect_start_menu(const std::wstring& root, std::vector<DiscoveredApp>& out) {
    if (root.empty()) {
        return;
    }
    std::vector<std::wstring> stack = {root};
    int files = 0;
    while (!stack.empty() && files < 400) {
        const std::wstring dir = stack.back();
        stack.pop_back();
        const std::wstring glob = dir + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(glob.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") {
                continue;
            }
            const std::wstring full = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back(full);
                continue;
            }
            if (!contains_ci(n, L".lnk")) {
                continue;
            }
            ++files;
            IShellLinkW* sl = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                       reinterpret_cast<void**>(&sl)))) {
                continue;
            }
            IPersistFile* pf = nullptr;
            if (FAILED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
                sl->Release();
                continue;
            }
            if (FAILED(pf->Load(full.c_str(), STGM_READ))) {
                pf->Release();
                sl->Release();
                continue;
            }
            wchar_t target[MAX_PATH * 2]{};
            WIN32_FIND_DATAW unused{};
            sl->GetPath(target, MAX_PATH * 2, &unused, SLGP_RAWPATH);
            pf->Release();
            sl->Release();
            std::wstring exe = canonical_file(target);
            if (exe.empty() || !contains_ci(exe, L".exe")) {
                continue;
            }
            std::wstring display = n;
            if (contains_ci(display, L".lnk")) {
                display = display.substr(0, display.size() - 4);
            }
            if (skip_other_name(display)) {
                continue;
            }
            DiscoveredApp a;
            a.display_name = display;
            a.kind = L"win32";
            a.discovery_source = L"start_menu";
            a.group = L"other";
            a.executable = exe;
            a.available = true;
            add_candidate(out, std::move(a));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::wstring family_from_package_full_name(const std::wstring& full) {
    const auto first = full.find(L'_');
    const auto last = full.rfind(L'_');
    if (first == std::wstring::npos || last == std::wstring::npos || last <= first) {
        return L"";
    }
    return full.substr(0, first) + L"_" + full.substr(last + 1);
}

void collect_appx(std::vector<DiscoveredApp>& out) {
    HKEY k = nullptr;
    const wchar_t* path =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[512]{};
        DWORD nlen = 512;
        if (RegEnumKeyExW(k, i, name, &nlen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        HKEY item = nullptr;
        if (RegOpenKeyExW(k, name, 0, KEY_READ, &item) != ERROR_SUCCESS) {
            continue;
        }
        auto read = [&](const wchar_t* value) {
            wchar_t buf[MAX_PATH * 4]{};
            DWORD sz = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExW(item, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz) != ERROR_SUCCESS) {
                return std::wstring{};
            }
            return std::wstring(buf);
        };
        std::wstring family = read(L"PackageFamilyName");
        if (family.empty()) {
            family = family_from_package_full_name(name);
        }
        std::wstring root = read(L"PackageRootFolder");
        if (root.empty()) {
            root = read(L"Path");
        }
        std::wstring display = read(L"DisplayName");
        if (display.rfind(L"ms-resource:", 0) == 0) {
            display.clear();
        }
        std::wstring app_id = L"App";
        wchar_t sub[256]{};
        DWORD slen = 256;
        if (RegEnumKeyExW(item, 0, sub, &slen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && sub[0] != 0) {
            app_id = sub;
        }
        RegCloseKey(item);
        if (family.empty()) {
            continue;
        }
        if (display.empty()) {
            display = family;
        }
        DiscoveredApp a;
        a.kind = L"msix";
        a.discovery_source = L"appx";
        a.group = L"other";
        a.package_family_name = family;
        a.install_root = root;
        a.display_name = display;
        a.app_id = app_id;
        a.aumid = family + L"!" + app_id;
        if (!root.empty()) {
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW((root + L"\\*.exe").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                a.executable = root + L"\\" + fd.cFileName;
                FindClose(h);
            } else {
                h = FindFirstFileW((root + L"\\app\\*.exe").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    a.executable = root + L"\\app\\" + fd.cFileName;
                    FindClose(h);
                }
            }
        }
        a.available = !canonical_file(a.executable).empty() || !root.empty();
        if (!a.executable.empty() && canonical_file(a.executable).empty()) {
            a.executable.clear();
        }
        if (a.available && a.executable.empty()) {
            a.status = L"unverified";
            a.compatibility = L"unknown";
        }
        if (packaged_is_full_trust(!a.executable.empty() ? a.executable : a.install_root)) {
            a.compatibility = L"cannot_contain";
        }
        add_candidate(out, std::move(a));
    }
    RegCloseKey(k);
}

void collect_path_cli(const wchar_t* name, std::vector<DiscoveredApp>& out) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, name, L".exe", MAX_PATH, buf, nullptr);
    if (n == 0) {
        return;
    }
    DiscoveredApp a;
    a.display_name = name;
    a.kind = L"cli";
    a.discovery_source = L"path";
    a.group = L"other";
    a.executable = buf;
    a.available = true;
    add_candidate(out, std::move(a));
}

std::string app_json(const DiscoveredApp& a) {
    std::string o = "{";
    auto field = [&](const char* k, const std::wstring& v, bool comma = true) {
        o += "\"";
        o += k;
        o += "\":";
        if (v.empty()) {
            o += "null";
        } else {
            o += "\"";
            o += json_escape(v);
            o += "\"";
        }
        if (comma) {
            o += ",";
        }
    };
    field("id", a.id);
    field("displayName", a.display_name);
    field("publisher", a.publisher);
    field("kind", a.kind);
    o += "\"available\":";
    o += a.available ? "true" : "false";
    o += ",";
    field("status", a.status);
    field("compatibility", a.compatibility);
    field("executable", a.executable);
    field("packageFamilyName", a.package_family_name);
    field("appId", a.app_id);
    field("aumid", a.aumid);
    field("installRoot", a.install_root);
    field("discoverySource", a.discovery_source);
    field("group", a.group, false);
    o += "}";
    return o;
}

DiscoveredApp known_stub(const wchar_t* id, const wchar_t* display, const wchar_t* publisher, const wchar_t* kind) {
    DiscoveredApp a;
    a.id = id;
    a.display_name = display;
    a.publisher = publisher;
    a.kind = kind;
    a.available = false;
    a.status = L"not_found";
    a.compatibility = L"unknown";
    a.group = L"ai";
    a.discovery_source = L"catalog";
    return a;
}

}  // namespace

int cmd_discover(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::vector<DiscoveredApp> raw;
    collect_uninstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", raw);
    collect_uninstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", raw);
    collect_uninstall(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", raw);
    collect_app_paths(HKEY_LOCAL_MACHINE, raw);
    collect_app_paths(HKEY_CURRENT_USER, raw);

    wchar_t programs[MAX_PATH]{};
    wchar_t common[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, programs))) {
        collect_start_menu(programs, raw);
    }
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, common))) {
        collect_start_menu(common, raw);
    }
    collect_appx(raw);
    collect_path_cli(L"codex", raw);
    collect_path_cli(L"claude", raw);
    collect_path_cli(L"cursor", raw);

    const std::wstring tp = module_dir() + L"\\scylla-test-parent.exe";
    if (!canonical_file(tp).empty()) {
        DiscoveredApp a;
        a.id = L"scylla-test-parent";
        a.display_name = L"Scylla Test Parent";
        a.publisher = L"CXL";
        a.kind = L"win32";
        a.executable = tp;
        a.available = true;
        a.status = L"installed";
        a.compatibility = L"strict_verified";
        a.discovery_source = L"local";
        a.group = L"ai";
        add_candidate(raw, std::move(a));
    }

    const std::wstring chatbox = L"C:\\Program Files\\Chatbox\\Chatbox.exe";
    if (!canonical_file(chatbox).empty()) {
        DiscoveredApp a;
        a.id = L"chatbox";
        a.display_name = L"Chatbox";
        a.publisher = L"Chatbox";
        a.kind = L"win32";
        a.executable = chatbox;
        a.available = true;
        a.status = L"installed";
        a.compatibility = L"discovered";
        a.discovery_source = L"local";
        a.group = L"ai";
        add_candidate(raw, std::move(a));
    }

    std::map<std::wstring, DiscoveredApp> known;
    known.insert({L"chatgpt", known_stub(L"chatgpt", L"ChatGPT", L"OpenAI", L"msix")});
    known.insert({L"chatbox", known_stub(L"chatbox", L"Chatbox", L"Chatbox", L"win32")});
    known.insert({L"cursor", known_stub(L"cursor", L"Cursor", L"Anysphere", L"win32")});
    known.insert({L"codex", known_stub(L"codex", L"Codex", L"OpenAI", L"cli")});
    known.insert({L"claude-code", known_stub(L"claude-code", L"Claude Code", L"Anthropic", L"cli")});
    known.insert({L"scylla-test-parent", known_stub(L"scylla-test-parent", L"Scylla Test Parent", L"CXL", L"win32")});

    std::map<std::wstring, DiscoveredApp> others;
    for (auto& a : raw) {
        std::wstring kid = a.id.empty() ? known_id_for(a) : a.id;
        if (!kid.empty()) {
            auto& slot = known[kid];
            if (slot.id.empty()) {
                slot.id = kid;
            }
            slot.available = true;
            slot.status = L"installed";
            if (a.status == L"unverified" && a.executable.empty()) {
                slot.status = L"unverified";
            }
            if (a.compatibility == L"cannot_contain") {
                slot.compatibility = L"cannot_contain";
            } else if (slot.compatibility == L"unknown" || slot.compatibility.empty()) {
                slot.compatibility = (kid == L"scylla-test-parent") ? L"strict_verified" : L"discovered";
            }
            if (!a.display_name.empty() && a.display_name != a.package_family_name) {
                slot.display_name = a.display_name;
            }
            if (!a.executable.empty()) {
                slot.executable = a.executable;
            }
            if (!a.package_family_name.empty()) {
                slot.package_family_name = a.package_family_name;
            }
            if (!a.aumid.empty()) {
                slot.aumid = a.aumid;
            }
            if (!a.app_id.empty()) {
                slot.app_id = a.app_id;
            }
            if (!a.install_root.empty()) {
                slot.install_root = a.install_root;
            }
            if (!a.kind.empty() && slot.kind != a.kind && a.kind == L"msix") {
                slot.kind = L"msix";
            }
            if (a.kind == L"cli") {
                slot.kind = L"cli";
            }
            slot.discovery_source = a.discovery_source;
            slot.group = L"ai";
            if (kid == L"cursor-insiders") {
                slot.display_name = L"Cursor Insiders";
                slot.group = L"ai";
            }
            continue;
        }
        const std::wstring key = path_lower(a.executable);
        if (key.empty()) {
            continue;
        }
        if (others.find(key) == others.end()) {
            others[key] = std::move(a);
        }
    }

    const bool known_only = has_flag(argc, argv, L"--known");
    const std::wstring only_id = arg_after(argc, argv, L"--id");

    std::vector<DiscoveredApp> ordered;
    const wchar_t* order[] = {L"chatgpt", L"chatbox", L"cursor", L"cursor-insiders", L"codex", L"claude-code",
                              L"scylla-test-parent"};
    for (const auto* id : order) {
        auto it = known.find(id);
        if (it == known.end()) {
            continue;
        }
        if (!only_id.empty() && it->second.id != only_id) {
            continue;
        }
        ordered.push_back(it->second);
    }
    if (!known_only) {
        int n = 0;
        for (auto& kv : others) {
            if (n >= 50) {
                break;
            }
            if (!only_id.empty()) {
                continue;
            }
            ordered.push_back(kv.second);
            ++n;
        }
    }

    std::string json = "{\"ok\":true,\"applications\":[";
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i) {
            json += ",";
        }
        json += app_json(ordered[i]);
    }
    json += "]}";
    std::cout << json << "\n" << std::flush;
    CoUninitialize();
    return 0;
}

}  // namespace scylla
