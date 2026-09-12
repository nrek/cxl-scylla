#include "scyllagpt/broker_mcp.h"
#include "scyllagpt/ssh_query_executor.h"
#include "scyllagpt/single_instance.h"
#include "scyllagpt/window.h"

#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace {

bool has_switch(std::wstring_view name) {
    const wchar_t* raw = GetCommandLineW();
    if (!raw) return false;
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(raw, &count);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < count && !found; ++i) found = name == argv[i];
    LocalFree(argv);
    return found;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    if (scyllagpt::ssh_askpass_mode_requested()) {
        return scyllagpt::run_ssh_askpass_helper();
    }
    if (has_switch(L"--mcp-query-broker")) {
        return scyllagpt::run_broker_mcp_helper();
    }

    scyllagpt::InstanceLock instance = scyllagpt::try_acquire_instance();
    if (!instance.owned) {
        scyllagpt::activate_existing_instance();
        scyllagpt::release_instance(&instance);
        return 0;
    }

    const int code = scyllagpt::run_ui(inst, show);
    scyllagpt::release_instance(&instance);
    return code;
}
