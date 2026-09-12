#pragma once

#include <string>

namespace scyllagpt {

// Process coordination primitives used by Scylla.
// Mutex name is fixed so either shell blocks the other.
struct InstanceLock {
    void* handle = nullptr;  // HANDLE, opaque so headers stay light
    bool owned = false;
};

// Try to acquire. On success owned=true. On failure another Scylla is running.
InstanceLock try_acquire_instance();
void release_instance(InstanceLock* lock);

// Bring an existing main window to the foreground when possible (best-effort).
bool activate_existing_instance();

}  // namespace scyllagpt
