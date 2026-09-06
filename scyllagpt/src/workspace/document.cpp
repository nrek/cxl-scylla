#include "scyllagpt/document.h"

#include "scyllagpt/store.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace scyllagpt {
namespace {

bool read_file(const std::wstring& path, std::string& bytes, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"Could not open file";
        return false;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 0) {
        CloseHandle(h);
        err = L"Could not size file";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(sz.QuadPart));
    DWORD rd = 0;
    if (sz.QuadPart > 0) {
        ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &rd, nullptr);
        bytes.resize(rd);
    }
    CloseHandle(h);
    return true;
}

bool has_crlf(const std::wstring& t) {
    return t.find(L"\r\n") != std::wstring::npos;
}

std::wstring to_lf(std::wstring t) {
    std::wstring o;
    o.reserve(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'\r' && i + 1 < t.size() && t[i + 1] == L'\n') {
            o.push_back(L'\n');
            ++i;
        } else {
            o.push_back(t[i]);
        }
    }
    return o;
}

std::wstring to_crlf(const std::wstring& t) {
    std::wstring o;
    o.reserve(t.size() + 8);
    for (wchar_t ch : t) {
        if (ch == L'\n') {
            o += L"\r\n";
        } else if (ch != L'\r') {
            o.push_back(ch);
        }
    }
    return o;
}

}  // namespace

bool looks_binary_bytes(const char* p, std::size_t n) {
    const std::size_t lim = n < 4096 ? n : 4096;
    int nul = 0;
    for (std::size_t i = 0; i < lim; ++i) {
        if (p[i] == 0) {
            ++nul;
        }
    }
    return nul > 2;
}

std::uint64_t hash_file_bytes(const std::wstring& path) {
    std::string bytes;
    std::wstring err;
    if (!read_file(path, bytes, err)) {
        return 0;
    }
    return fnv1a64(bytes.data(), bytes.size());
}

LoadedText load_text_file(const std::wstring& path, std::size_t max_bytes) {
    LoadedText out;
    std::string bytes;
    if (!read_file(path, bytes, out.error)) {
        return out;
    }
    if (bytes.size() > max_bytes) {
        out.too_large = true;
        out.error = L"File is larger than the 10 MiB view limit";
        return out;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        out.enc = TextEnc::Utf16Le;
        const wchar_t* w = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        const std::size_t n = (bytes.size() - 2) / 2;
        out.text.assign(w, n);
        out.crlf = has_crlf(out.text);
        out.text = to_lf(out.text);
        out.hash = fnv1a64(bytes.data(), bytes.size());
        return out;
    }
    std::size_t skip = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        out.enc = TextEnc::Utf8Bom;
        skip = 3;
    } else {
        out.enc = TextEnc::Utf8;
    }
    if (looks_binary_bytes(bytes.data() + skip, bytes.size() - skip)) {
        out.binary = true;
        out.error = L"Binary or unsupported file";
        return out;
    }
    const char* src = bytes.data() + skip;
    const int slen = static_cast<int>(bytes.size() - skip);
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, slen, nullptr, 0);
    if (n <= 0 && slen > 0) {
        out.encoding_uncertain = true;
        out.error = L"Not valid UTF-8; opened read-only. Save As UTF-8 to convert.";
        const int n2 = MultiByteToWideChar(CP_ACP, 0, src, slen, nullptr, 0);
        if (n2 <= 0) {
            return out;
        }
        out.text.resize(static_cast<std::size_t>(n2));
        MultiByteToWideChar(CP_ACP, 0, src, slen, out.text.data(), n2);
        out.crlf = has_crlf(out.text);
        out.text = to_lf(out.text);
        out.hash = fnv1a64(bytes.data(), bytes.size());
        return out;
    }
    out.text.resize(static_cast<std::size_t>(n));
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, slen, out.text.data(), n);
    }
    out.crlf = has_crlf(out.text);
    out.text = to_lf(out.text);
    out.hash = fnv1a64(bytes.data(), bytes.size());
    return out;
}

SaveResult save_text_file(const std::wstring& path, const std::wstring& text, TextEnc enc, bool crlf,
                           std::uint64_t expected_hash) {
    SaveResult r;
    const std::uint64_t disk = hash_file_bytes(path);
    if (expected_hash != 0 && disk != 0 && disk != expected_hash) {
        r.conflict = true;
        r.error = L"File changed on disk";
        return r;
    }
    std::wstring payload = crlf ? to_crlf(text) : text;
    std::string bytes;
    if (enc == TextEnc::Utf16Le) {
        bytes.push_back('\xFF');
        bytes.push_back('\xFE');
        bytes.append(reinterpret_cast<const char*>(payload.data()), payload.size() * sizeof(wchar_t));
    } else {
        if (enc == TextEnc::Utf8Bom) {
            bytes.push_back('\xEF');
            bytes.push_back('\xBB');
            bytes.push_back('\xBF');
        }
        const std::string u8 = utf8(payload);
        bytes += u8;
    }
    const std::wstring tmp = path + L".scyllagpt-tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        r.error = L"Could not write temp file";
        return r;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wr, nullptr);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        r.error = L"Write failed";
        return r;
    }
    if (!ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp.c_str());
            r.error = L"ReplaceFile failed";
            return r;
        }
    }
    r.ok = true;
    r.new_hash = fnv1a64(bytes.data(), bytes.size());
    return r;
}

}  // namespace scyllagpt
