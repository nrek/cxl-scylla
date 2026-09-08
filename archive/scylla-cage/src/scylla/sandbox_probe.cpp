#include "sandbox_probe.h"

#include <windows.h>
#include <iostream>

namespace scylla {
namespace {

std::wstring read_reg_sz(HKEY root, const wchar_t* sub, const wchar_t* value) {
    wchar_t buf[256]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegGetValueW(root, sub, value, RRF_RT_REG_SZ, &type, buf, &size) != ERROR_SUCCESS) {
        return L"";
    }
    return buf;
}

bool file_exists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

HostCapabilities probe_host_capabilities() {
    HostCapabilities c;
    const wchar_t* nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    c.product_name = read_reg_sz(HKEY_LOCAL_MACHINE, nt, L"ProductName");
    c.edition_id = read_reg_sz(HKEY_LOCAL_MACHINE, nt, L"EditionID");
    c.display_version = read_reg_sz(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion");
    c.build = read_reg_sz(HKEY_LOCAL_MACHINE, nt, L"CurrentBuildNumber");
    const std::wstring dll = L"C:\\Windows\\System32\\processmodel.dll";
    c.processmodel_present = file_exists(dll);
    if (c.processmodel_present) {
        HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) {
            auto proc = GetProcAddress(h, "Experimental_CreateProcessInSandbox");
            c.experimental_export_present = proc != nullptr;
            FreeLibrary(h);
        } else {
            c.experimental_export_present = false;
            c.experimental_error = L"LoadLibraryExW(processmodel.dll) failed";
        }
    }
    if (c.experimental_export_present) {
        c.experimental_usable = L"NO";
        c.experimental_error =
            L"export present; not invoked in v0.0.1 (no stable SandboxSpec header). Legacy AppContainer is the required backend.";
    } else {
        c.experimental_usable = L"NO";
        if (c.experimental_error.empty()) {
            c.experimental_error = L"export not found";
        }
    }
    c.selected_backend = L"LEGACY_APPCONTAINER";
    return c;
}

void print_host_capabilities(const HostCapabilities& c) {
    std::wcout << L"SCYLLA HOST CAPABILITIES\n\n";
    std::wcout << L"Windows: " << (c.product_name.empty() ? L"unknown" : c.product_name);
    if (!c.edition_id.empty()) {
        std::wcout << L" (" << c.edition_id << L")";
    }
    std::wcout << L"\n";
    std::wcout << L"Build: " << (c.build.empty() ? L"unknown" : c.build);
    if (!c.display_version.empty()) {
        std::wcout << L"  DisplayVersion=" << c.display_version;
    }
    std::wcout << L"\n";
    std::wcout << L"AppContainer: AVAILABLE (legacy CreateAppContainerProfile)\n";
    std::wcout << L"processmodel.dll: " << (c.processmodel_present ? L"YES" : L"NO") << L"\n";
    std::wcout << L"Experimental Sandbox API:\n";
    std::wcout << L"    export: " << (c.experimental_export_present ? L"YES" : L"NO") << L"\n";
    std::wcout << L"    usable: " << c.experimental_usable << L"\n";
    std::wcout << L"    error: " << c.experimental_error << L"\n";
    std::wcout << L"Selected backend:\n    " << c.selected_backend << L"\n";
}

}  // namespace scylla
