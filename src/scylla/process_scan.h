#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace scylla {

struct ProcessImage {
    DWORD pid = 0;
    std::wstring path;
    std::wstring filename;
};

struct AppContainerInfo {
    bool queried = false;
    bool is_appcontainer = false;
    std::wstring sid;
};

std::vector<ProcessImage> snapshot_process_images();
bool process_is_related(const ProcessImage& p, const std::vector<std::wstring>& install_roots,
                        const std::vector<std::wstring>& known_filenames);
AppContainerInfo query_process_appcontainer(DWORD pid);
std::vector<DWORD> job_process_ids(HANDLE job);
bool process_in_list(DWORD pid, const std::vector<DWORD>& ids);
std::wstring image_path_for_pid(DWORD pid);
bool terminate_process(DWORD pid);

}  // namespace scylla
