#include "scyllagpt/file_io.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

std::string read_file_bytes(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(h);
        return {};
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    return raw;
}

bool write_file_bytes_atomic(const std::wstring& path, const std::string& body) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL wrote = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!wrote || wr != body.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace scyllagpt
