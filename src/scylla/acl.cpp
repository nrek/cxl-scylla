#include "acl.h"
#include "path.h"

#include <aclapi.h>
#include <sddl.h>

namespace scylla {

bool grant_sid_access(const std::wstring& path, PSID sid, DWORD access, std::wstring& err) {
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    DWORD st = GetNamedSecurityInfoW(
        path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
    if (st != ERROR_SUCCESS) {
        err = L"GetNamedSecurityInfo: " + win32_error(st);
        return false;
    }

    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = access;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);

    PACL new_dacl = nullptr;
    st = SetEntriesInAclW(1, &ea, old_dacl, &new_dacl);
    if (st != ERROR_SUCCESS) {
        LocalFree(sd);
        err = L"SetEntriesInAcl: " + win32_error(st);
        return false;
    }
    st = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl,
        nullptr);
    LocalFree(new_dacl);
    LocalFree(sd);
    if (st != ERROR_SUCCESS) {
        err = L"SetNamedSecurityInfo: " + win32_error(st);
        return false;
    }
    return true;
}

bool revoke_sid_access(const std::wstring& path, PSID sid, std::wstring& err) {
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    DWORD st = GetNamedSecurityInfoW(
        path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd);
    if (st != ERROR_SUCCESS) {
        err = L"GetNamedSecurityInfo: " + win32_error(st);
        return false;
    }
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = 0;
    ea.grfAccessMode = REVOKE_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    PACL new_dacl = nullptr;
    st = SetEntriesInAclW(1, &ea, old_dacl, &new_dacl);
    if (st != ERROR_SUCCESS) {
        LocalFree(sd);
        err = L"SetEntriesInAcl(revoke): " + win32_error(st);
        return false;
    }
    st = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl,
        nullptr);
    LocalFree(new_dacl);
    LocalFree(sd);
    if (st != ERROR_SUCCESS) {
        err = L"SetNamedSecurityInfo(revoke): " + win32_error(st);
        return false;
    }
    return true;
}

bool dacl_has_deny_for_sid(const std::wstring& path, PSID sid) {
    if (!sid) {
        return false;
    }
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD st = GetNamedSecurityInfoW(
        path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd);
    if (st != ERROR_SUCCESS || dacl == nullptr) {
        if (sd) {
            LocalFree(sd);
        }
        return false;
    }
    ACL_SIZE_INFORMATION asi{};
    if (!GetAclInformation(dacl, &asi, sizeof(asi), AclSizeInformation)) {
        LocalFree(sd);
        return false;
    }
    bool found = false;
    for (DWORD i = 0; i < asi.AceCount; ++i) {
        LPVOID ace = nullptr;
        if (!GetAce(dacl, i, &ace) || ace == nullptr) {
            continue;
        }
        const auto* hdr = static_cast<ACE_HEADER*>(ace);
        if (hdr->AceType != ACCESS_DENIED_ACE_TYPE) {
            continue;
        }
        const auto* denied = static_cast<ACCESS_DENIED_ACE*>(ace);
        PSID ace_sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&denied->SidStart));
        if (EqualSid(sid, ace_sid)) {
            found = true;
            break;
        }
    }
    LocalFree(sd);
    return found;
}

}  // namespace scylla
