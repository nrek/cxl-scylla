#include "scyllagpt/broker_mcp.h"
#include "scyllagpt/ssh_query_executor.h"
#include "scyllagpt/window.h"

#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace {

// The lpCmdLine parameter is unreliable for our purposes, so read the raw command line.
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
    // Askpass mode: ssh-add spawns this with SSH_ASKPASS pointing here to collect an SSH key
    // passphrase. It is selected by environment rather than a switch because SSH_ASKPASS carries a
    // bare program path with no arguments. Prints one value to stdout and exits.
    if (scyllagpt::ssh_askpass_mode_requested()) {
        return scyllagpt::run_ssh_askpass_helper();
    }
    // Secret-free child mode: Codex spawns this to expose the scylla_query tool over stdio. It puts
    // up no window and holds no Keyring; it only relays calls to the running Workbench.
    if (has_switch(L"--mcp-query-broker")) {
        return scyllagpt::run_broker_mcp_helper();
    }
    return scyllagpt::run_ui(inst, show);
}
