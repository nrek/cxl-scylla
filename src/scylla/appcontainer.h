#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace scylla {

struct LaunchRequest {
    std::wstring executable;
    std::wstring arguments;
    std::wstring workspace;
    std::wstring cwd;
    std::vector<std::wstring> extra_rx_paths;
    std::vector<std::wstring> extra_rw_paths;
    std::vector<std::wstring> extra_ro_paths;
    bool internet_client = true;
    bool named_lifecycle = false;
};

struct LaunchResult {
    bool ok = false;
    std::wstring error;
    DWORD exit_code = 0;
    DWORD pid = 0;
    std::wstring session_name;
    std::wstring backend;
};

struct ContainedRun {
    bool ok = false;
    std::wstring error;
    std::wstring session_name;
    std::wstring job_name;
    std::wstring stop_event_name;
    std::wstring sid_string;
    std::wstring backend = L"LEGACY_APPCONTAINER";
    DWORD pid = 0;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE job = nullptr;
    HANDLE stop_event = nullptr;
    PSID ac_sid = nullptr;
    std::vector<std::wstring> granted_rw;
    std::vector<std::wstring> granted_ro;
    std::vector<std::wstring> granted_rx;
    bool owns_appcontainer_profile = true;
};

ContainedRun start_contained(const LaunchRequest& req);
bool cleanup_contained(ContainedRun& run, std::wstring& err);
bool terminate_job(ContainedRun& run, std::wstring& err);
LaunchResult launch_appcontainer(const LaunchRequest& req);
int cmd_selftest_packaged_create(const std::wstring& exe);

}  // namespace scylla
