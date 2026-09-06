#pragma once

#include <cstdint>
#include <string>

namespace scyllagpt {

enum class TextEnc { Utf8, Utf8Bom, Utf16Le };

struct LoadedText {
    std::wstring text;
    TextEnc enc = TextEnc::Utf8;
    bool crlf = false;
    std::uint64_t hash = 0;
    bool binary = false;
    bool too_large = false;
    bool encoding_uncertain = false;
    std::wstring error;
};

struct SaveResult {
    bool ok = false;
    bool conflict = false;
    std::wstring error;
    std::uint64_t new_hash = 0;
};

LoadedText load_text_file(const std::wstring& path, std::size_t max_bytes = 10 * 1024 * 1024);
SaveResult save_text_file(const std::wstring& path, const std::wstring& text, TextEnc enc, bool crlf,
                           std::uint64_t expected_hash);
std::uint64_t hash_file_bytes(const std::wstring& path);
bool looks_binary_bytes(const char* p, std::size_t n);

}  // namespace scyllagpt
