#pragma once
#include "appcontainer.h"
#include "process_scan.h"

#include <string>
#include <vector>

namespace scylla {

enum class SessionState { Clean, Degraded, Refused, CleanupRequired };

struct AppProfile {
    std::wstring id;
    std::wstring display_name;
    std::wstring primary_executable;
    std::vector<std::wstring> install_roots;
    std::vector<std::wstring> known_executables;
};

struct RelatedProcess {
    DWORD pid = 0;
    std::wstring path;
    std::wstring filename;
    bool in_job = false;
    bool is_appcontainer = false;
    std::wstring sid;
    std::wstring note;
};

struct SessionFile {
    bool present = false;
    std::wstring session_name;
    std::wstring job_name;
    std::wstring stop_event_name;
    std::wstring sid;
    std::wstring profile;
    std::wstring exe;
    DWORD pid = 0;
    DWORD scylla_pid = 0;
    std::vector<std::wstring> grant_rw;
    std::vector<std::wstring> grant_ro;
    std::vector<std::wstring> grant_rx;
};

AppProfile resolve_builtin_profile(const std::wstring& id, const std::wstring& scylla_dir);
AppProfile profile_from_executable(const std::wstring& exe);
std::wstring session_file_path();
bool write_session_file(const ContainedRun& run, const AppProfile& profile, std::wstring& err);
SessionFile read_session_file();
void remove_session_file();

std::vector<RelatedProcess> find_related(const AppProfile& profile, HANDLE job, const std::wstring& expected_sid);
std::vector<std::wstring> list_sibling_executables(const std::wstring& root);

int cmd_audit(int argc, wchar_t** argv);
int cmd_strict_launch(int argc, wchar_t** argv);
int cmd_strict_status(int argc, wchar_t** argv);
int cmd_strict_processes(int argc, wchar_t** argv);
int cmd_strict_stop(int argc, wchar_t** argv);

}  // namespace scylla
