#pragma once

#include <string>

namespace scyllagpt {

// Read entire file (share read). Empty on failure.
std::string read_file_bytes(const std::wstring& path);

// Atomic write: path.tmp -> flush -> MoveFileEx REPLACE. Avoids truncate-on-crash.
bool write_file_bytes_atomic(const std::wstring& path, const std::string& body);

}  // namespace scyllagpt
