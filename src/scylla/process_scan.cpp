#include "process_scan.h"
#include "path.h"

#include <tlhelp32.h>
#include <sddl.h>
#include <vector>

namespace scylla {

std::wstring image_path_for_pid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return L"";
    }
    wchar_t buf[MAX_PATH * 4]{};
    DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &n);
    CloseHandle(h);
    return ok ? buf : L"";
}

std::vector<ProcessImage> snapshot_process_images() {
    std::vector<ProcessImage> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return out;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcessImage p;
            p.pid = pe.th32ProcessID;
            p.path = image_path_for_pid(p.pid);
            p.filename = p.path.empty() ? std::wstring(pe.szExeFile) : path_filename(p.path);
            out.push_back(std::move(p));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool terminate_process(DWORD pid) {
    if (pid == 0 || pid == 4 || pid == GetCurrentProcessId()) {
        return false;
    }
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) {
        return false;
    }
    const BOOL ok = TerminateProcess(h, 1);
    if (ok) {
        WaitForSingleObject(h, 3000);
    }
    CloseHandle(h);
    return ok != 0;
}

bool process_is_related(const ProcessImage& p, const std::vector<std::wstring>& install_roots,
                        const std::vector<std::wstring>& known_filenames) {
    if (p.pid == 0 || p.pid == 4) {
        return false;
    }
    const std::wstring name = path_lower(p.filename);
    bool name_ok = known_filenames.empty();
    for (const auto& k : known_filenames) {
        if (path_lower(k) == name) {
            name_ok = true;
            break;
        }
    }
    if (!name_ok) {
        return false;
    }
    if (install_roots.empty()) {
        return name_ok;
    }
    if (p.path.empty()) {
        return false;
    }
    for (const auto& root : install_roots) {
        if (path_is_under(p.path, root)) {
            return true;
        }
    }
    return false;
}

AppContainerInfo query_process_appcontainer(DWORD pid) {
    AppContainerInfo info;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        return info;
    }
    HANDLE tok = nullptr;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
        CloseHandle(proc);
        return info;
    }
    DWORD is_ac = 0;
    DWORD ret = 0;
    if (GetTokenInformation(tok, TokenIsAppContainer, &is_ac, sizeof(is_ac), &ret)) {
        info.queried = true;
        info.is_appcontainer = is_ac != 0;
    }
    DWORD need = 0;
    GetTokenInformation(tok, TokenAppContainerSid, nullptr, 0, &need);
    if (need > 0) {
        std::vector<BYTE> buf(need);
        if (GetTokenInformation(tok, TokenAppContainerSid, buf.data(), need, &ret)) {
            auto* aci = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(buf.data());
            if (aci && aci->TokenAppContainer) {
                LPWSTR sid = nullptr;
                if (ConvertSidToStringSidW(aci->TokenAppContainer, &sid)) {
                    info.sid = sid;
                    LocalFree(sid);
                }
            }
        }
    }
    CloseHandle(tok);
    CloseHandle(proc);
    return info;
}

std::vector<DWORD> job_process_ids(HANDLE job) {
    std::vector<DWORD> ids;
    if (!job) {
        return ids;
    }
    DWORD need = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * 128;
    std::vector<BYTE> buf(need);
    for (int attempt = 0; attempt < 3; ++attempt) {
        JOBOBJECT_BASIC_PROCESS_ID_LIST* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
        if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, buf.data(), static_cast<DWORD>(buf.size()), nullptr)) {
            for (DWORD i = 0; i < list->NumberOfAssignedProcesses; ++i) {
                ids.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
            }
            return ids;
        }
        buf.resize(buf.size() * 2);
    }
    return ids;
}

bool process_in_list(DWORD pid, const std::vector<DWORD>& ids) {
    for (DWORD x : ids) {
        if (x == pid) {
            return true;
        }
    }
    return false;
}

}  // namespace scylla
