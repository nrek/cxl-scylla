#include "scyllagpt/single_instance.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {
namespace {

constexpr wchar_t kMutexName[] = L"Global\\ScyllaGPT.Workbench";
constexpr wchar_t kWindowClassHint[] = L"ScyllaGPTWindow";

}  // namespace

InstanceLock try_acquire_instance() {
    InstanceLock lock{};
    HANDLE h = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!h) {
        return lock;
    }
    lock.handle = h;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another process holds it; we did not become owner for product purposes.
        ReleaseMutex(h);
        lock.owned = false;
        return lock;
    }
    lock.owned = true;
    return lock;
}

void release_instance(InstanceLock* lock) {
    if (!lock || !lock->handle) {
        return;
    }
    if (lock->owned) {
        ReleaseMutex(static_cast<HANDLE>(lock->handle));
        lock->owned = false;
    }
    CloseHandle(static_cast<HANDLE>(lock->handle));
    lock->handle = nullptr;
}

bool activate_existing_instance() {
    HWND wnd = FindWindowW(kWindowClassHint, nullptr);
    if (!wnd) {
        // Fall back: any top-level window whose title starts with Scylla.
        wnd = FindWindowW(nullptr, L"Scylla");
    }
    if (!wnd) {
        return false;
    }
    if (IsIconic(wnd)) {
        ShowWindow(wnd, SW_RESTORE);
    }
    SetForegroundWindow(wnd);
    return true;
}

}  // namespace scyllagpt
