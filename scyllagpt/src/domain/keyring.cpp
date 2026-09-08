#include "scyllagpt/keyring.h"

#include "scyllagpt/paths.h"
#include "scyllagpt/utf.h"

#include <argon2.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

#pragma comment(lib, "bcrypt.lib")

namespace scyllagpt {
namespace {

constexpr char kMagicV1[8] = {'S', 'C', 'Y', 'L', 'V', 'L', 'T', '1'};
constexpr char kMagicV2[8] = {'S', 'C', 'Y', 'L', 'V', 'L', 'T', '2'};
constexpr std::uint32_t kFormatV1 = 1;
constexpr std::uint32_t kFormatV2 = 2;

constexpr std::uint32_t kKdfPbkdf2Sha256 = 1;
constexpr std::uint32_t kKdfArgon2id = 2;
constexpr std::uint32_t kPbkdf2Iterations = 600000;
// OWASP-ish interactive Argon2id (params stored in vault header).
constexpr std::uint32_t kArgonMCost = 19456;  // KiB (~19 MiB)
constexpr std::uint32_t kArgonTCost = 2;
constexpr std::uint32_t kArgonParallel = 1;

constexpr std::uint32_t kSaltLen = 32;
constexpr std::uint32_t kDekLen = 32;
constexpr std::uint32_t kNonceLen = 12;
constexpr std::uint32_t kTagLen = 16;
constexpr std::uint32_t kKekLen = 32;

void secure_wipe(void* p, size_t n) {
    if (p && n) {
        SecureZeroMemory(p, n);
    }
}

void secure_wipe_string(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(s.data(), s.size());
        s.clear();
        s.shrink_to_fit();
    }
}

void secure_wipe_bytes(std::vector<std::uint8_t>& v) {
    if (!v.empty()) {
        SecureZeroMemory(v.data(), v.size());
        v.clear();
        v.shrink_to_fit();
    }
}

std::uint64_t now_ms() {
    return GetTickCount64();
}

bool random_bytes(std::uint8_t* out, ULONG n) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

bool pbkdf2_sha256(std::string_view passphrase, const std::uint8_t* salt, ULONG salt_len,
                   ULONG iterations, std::uint8_t* out, ULONG out_len) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(st)) {
        return false;
    }
    st = BCryptDeriveKeyPBKDF2(alg, reinterpret_cast<PUCHAR>(const_cast<char*>(passphrase.data())),
                               static_cast<ULONG>(passphrase.size()),
                               const_cast<PUCHAR>(salt), salt_len, iterations, out, out_len, 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    return BCRYPT_SUCCESS(st);
}

bool argon2id_derive(std::string_view passphrase, const std::uint8_t* salt, ULONG salt_len,
                     std::uint32_t m_cost, std::uint32_t t_cost, std::uint32_t parallelism,
                     std::uint8_t* out, ULONG out_len) {
    const int rc = argon2id_hash_raw(t_cost, m_cost, parallelism, passphrase.data(), passphrase.size(),
                                     salt, salt_len, out, out_len);
    return rc == ARGON2_OK;
}

struct AesGcmKey {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;

    ~AesGcmKey() {
        if (key) {
            BCryptDestroyKey(key);
        }
        if (alg) {
            BCryptCloseAlgorithmProvider(alg, 0);
        }
    }

    bool open(const std::uint8_t* key_bytes, ULONG key_len) {
        NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(st)) {
            return false;
        }
        st = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
                               0);
        if (!BCRYPT_SUCCESS(st)) {
            return false;
        }
        st = BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, const_cast<PUCHAR>(key_bytes),
                                        key_len, 0);
        return BCRYPT_SUCCESS(st);
    }
};

bool aes_gcm_encrypt(const std::uint8_t* key, ULONG key_len, const std::uint8_t* nonce,
                     ULONG nonce_len, const std::uint8_t* plain, ULONG plain_len,
                     std::vector<std::uint8_t>& cipher, std::vector<std::uint8_t>& tag) {
    AesGcmKey k;
    if (!k.open(key, key_len)) {
        return false;
    }
    cipher.assign(plain_len, 0);
    tag.assign(kTagLen, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = nonce_len;
    info.pbTag = tag.data();
    info.cbTag = kTagLen;
    ULONG cb = 0;
    NTSTATUS st = BCryptEncrypt(k.key, const_cast<PUCHAR>(plain), plain_len, &info, nullptr, 0,
                                cipher.data(), static_cast<ULONG>(cipher.size()), &cb, 0);
    if (!BCRYPT_SUCCESS(st) || cb != plain_len) {
        secure_wipe_bytes(cipher);
        secure_wipe_bytes(tag);
        return false;
    }
    return true;
}

bool aes_gcm_decrypt(const std::uint8_t* key, ULONG key_len, const std::uint8_t* nonce,
                     ULONG nonce_len, const std::uint8_t* cipher, ULONG cipher_len,
                     const std::uint8_t* tag, ULONG tag_len, std::vector<std::uint8_t>& plain) {
    AesGcmKey k;
    if (!k.open(key, key_len)) {
        return false;
    }
    plain.assign(cipher_len, 0);
    std::vector<std::uint8_t> tag_copy(tag, tag + tag_len);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = nonce_len;
    info.pbTag = tag_copy.data();
    info.cbTag = tag_len;
    ULONG cb = 0;
    NTSTATUS st = BCryptDecrypt(k.key, const_cast<PUCHAR>(cipher), cipher_len, &info, nullptr, 0,
                                plain.data(), static_cast<ULONG>(plain.size()), &cb, 0);
    secure_wipe_bytes(tag_copy);
    if (!BCRYPT_SUCCESS(st) || cb != cipher_len) {
        secure_wipe_bytes(plain);
        return false;
    }
    return true;
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

bool read_u32(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& v) {
    if (end - p < 4) {
        return false;
    }
    v = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

bool read_bytes(const std::uint8_t*& p, const std::uint8_t* end, size_t n,
                std::vector<std::uint8_t>& out) {
    if (static_cast<size_t>(end - p) < n) {
        return false;
    }
    out.assign(p, p + n);
    p += n;
    return true;
}

std::string json_escape_local(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return o;
}

bool parse_string_field(const std::string& json, size_t& i, std::string& out) {
    while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < json.size()) {
        char c = json[i++];
        if (c == '"') {
            return true;
        }
        if (c == '\\' && i < json.size()) {
            char e = json[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    if (i + 4 <= json.size()) {
                        unsigned v = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = json[i++];
                            v <<= 4;
                            if (h >= '0' && h <= '9') {
                                v |= static_cast<unsigned>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                v |= static_cast<unsigned>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                v |= static_cast<unsigned>(h - 'A' + 10);
                            }
                        }
                        if (v < 0x80) {
                            out.push_back(static_cast<char>(v));
                        } else if (v < 0x800) {
                            out.push_back(static_cast<char>(0xc0 | (v >> 6)));
                            out.push_back(static_cast<char>(0x80 | (v & 0x3f)));
                        } else {
                            out.push_back(static_cast<char>(0xe0 | (v >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((v >> 6) & 0x3f)));
                            out.push_back(static_cast<char>(0x80 | (v & 0x3f)));
                        }
                    }
                    break;
                default:
                    out.push_back(e);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

// v1: [{"n":"...","d":"...","v":"..."},...]
bool parse_entries_json_v1(const std::string& json,
                           std::vector<std::tuple<std::string, std::string, std::string>>& out) {
    out.clear();
    size_t i = 0;
    while (i < json.size() && json[i] != '[') {
        ++i;
    }
    if (i >= json.size()) {
        return false;
    }
    ++i;
    while (i < json.size()) {
        while (i < json.size() && (json[i] == ' ' || json[i] == ',' || json[i] == '\n' ||
                                   json[i] == '\r' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == ']') {
            return true;
        }
        if (i >= json.size() || json[i] != '{') {
            return false;
        }
        ++i;
        std::string name, desc, value;
        while (i < json.size() && json[i] != '}') {
            while (i < json.size() && (json[i] == ' ' || json[i] == ',' || json[i] == '\n' ||
                                       json[i] == '\r' || json[i] == '\t')) {
                ++i;
            }
            if (i < json.size() && json[i] == '}') {
                break;
            }
            std::string key;
            if (!parse_string_field(json, i, key)) {
                return false;
            }
            while (i < json.size() && json[i] != ':') {
                ++i;
            }
            if (i >= json.size()) {
                return false;
            }
            ++i;
            std::string val;
            if (!parse_string_field(json, i, val)) {
                return false;
            }
            if (key == "n") {
                name = std::move(val);
            } else if (key == "d") {
                desc = std::move(val);
            } else if (key == "v") {
                value = std::move(val);
            } else {
                secure_wipe_string(val);
            }
        }
        if (i >= json.size() || json[i] != '}') {
            secure_wipe_string(value);
            return false;
        }
        ++i;
        if (name.empty()) {
            secure_wipe_string(value);
            return false;
        }
        out.emplace_back(std::move(name), std::move(desc), std::move(value));
    }
    return false;
}

struct ParsedSecret {
    std::string id;
    std::string scope;  // "global" | "project"
    std::string project_id;
    std::string name;
    std::string description;
    std::string value;
};

std::string entries_to_json_v2(const std::vector<ParsedSecret>& rows) {
    std::ostringstream oss;
    oss << "{\"version\":2,\"secrets\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) {
            oss << ',';
        }
        const auto& e = rows[i];
        oss << "{\"id\":\"" << json_escape_local(e.id) << "\",\"scope\":\""
            << json_escape_local(e.scope) << "\",\"projectId\":\"" << json_escape_local(e.project_id)
            << "\",\"displayName\":\"" << json_escape_local(e.name) << "\",\"referenceName\":\""
            << json_escape_local(e.name) << "\",\"description\":\"" << json_escape_local(e.description)
            << "\",\"value\":\"" << json_escape_local(e.value) << "\"}";
    }
    oss << "],\"recipes\":[]}";
    return oss.str();
}

bool parse_object_fields(const std::string& json, size_t& i, ParsedSecret& out) {
    while (i < json.size() && json[i] != '}') {
        while (i < json.size() && (json[i] == ' ' || json[i] == ',' || json[i] == '\n' ||
                                   json[i] == '\r' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == '}') {
            break;
        }
        std::string key;
        if (!parse_string_field(json, i, key)) {
            return false;
        }
        while (i < json.size() && json[i] != ':') {
            ++i;
        }
        if (i >= json.size()) {
            return false;
        }
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == '[') {
            // skip recipes array quickly
            int depth = 0;
            do {
                if (json[i] == '[') {
                    ++depth;
                } else if (json[i] == ']') {
                    --depth;
                }
                ++i;
            } while (i < json.size() && depth > 0);
            continue;
        }
        if (i < json.size() && (json[i] == '0' || json[i] == '1' || json[i] == '2' || json[i] == '3' ||
                                json[i] == '4' || json[i] == '5' || json[i] == '6' || json[i] == '7' ||
                                json[i] == '8' || json[i] == '9')) {
            while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
                ++i;
            }
            continue;
        }
        std::string val;
        if (!parse_string_field(json, i, val)) {
            return false;
        }
        if (key == "id") {
            out.id = std::move(val);
        } else if (key == "scope") {
            out.scope = std::move(val);
        } else if (key == "projectId") {
            out.project_id = std::move(val);
        } else if (key == "displayName" || key == "referenceName" || key == "n") {
            if (out.name.empty() || key == "referenceName" || key == "n") {
                out.name = std::move(val);
            } else {
                secure_wipe_string(val);
            }
        } else if (key == "description" || key == "d") {
            out.description = std::move(val);
        } else if (key == "value" || key == "v") {
            out.value = std::move(val);
        } else {
            secure_wipe_string(val);
        }
    }
    return true;
}

bool parse_payload_v2(const std::string& json, std::vector<ParsedSecret>& out) {
    out.clear();
    const size_t secrets_pos = json.find("\"secrets\"");
    if (secrets_pos == std::string::npos) {
        // Maybe bare v1 array
        std::vector<std::tuple<std::string, std::string, std::string>> v1;
        if (!parse_entries_json_v1(json, v1)) {
            return false;
        }
        for (auto& row : v1) {
            ParsedSecret s;
            s.name = std::move(std::get<0>(row));
            s.description = std::move(std::get<1>(row));
            s.value = std::move(std::get<2>(row));
            s.scope = "global";
            s.id = std::string("global:") + s.name;
            out.push_back(std::move(s));
        }
        return true;
    }
    size_t i = secrets_pos;
    while (i < json.size() && json[i] != '[') {
        ++i;
    }
    if (i >= json.size()) {
        return false;
    }
    ++i;
    while (i < json.size()) {
        while (i < json.size() && (json[i] == ' ' || json[i] == ',' || json[i] == '\n' ||
                                   json[i] == '\r' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == ']') {
            return true;
        }
        if (i >= json.size() || json[i] != '{') {
            return false;
        }
        ++i;
        ParsedSecret s;
        if (!parse_object_fields(json, i, s)) {
            secure_wipe_string(s.value);
            return false;
        }
        if (i >= json.size() || json[i] != '}') {
            secure_wipe_string(s.value);
            return false;
        }
        ++i;
        if (s.name.empty()) {
            secure_wipe_string(s.value);
            return false;
        }
        if (s.scope.empty()) {
            s.scope = s.project_id.empty() ? "global" : "project";
        }
        if (s.id.empty()) {
            if (s.scope == "project") {
                s.id = std::string("project:") + s.project_id + "/" + s.name;
            } else {
                s.id = std::string("global:") + s.name;
            }
        }
        out.push_back(std::move(s));
    }
    return false;
}

std::string entries_to_json_v1(
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& rows) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) {
            oss << ',';
        }
        oss << "{\"n\":\"" << json_escape_local(rows[i].first) << "\",\"d\":\""
            << json_escape_local(rows[i].second.first) << "\",\"v\":\""
            << json_escape_local(rows[i].second.second) << "\"}";
    }
    oss << ']';
    return oss.str();
}

bool valid_secret_name(std::string_view name) {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

KeyringStatus write_blob(const std::wstring& path, const std::vector<std::uint8_t>& blob) {
    const std::wstring parent = path.substr(0, path.find_last_of(L"\\/"));
    if (!parent.empty()) {
        ensure_dir(parent);
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return KeyringStatus::IoError;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, blob.data(), static_cast<DWORD>(blob.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != blob.size()) {
        return KeyringStatus::IoError;
    }
    return KeyringStatus::Ok;
}

}  // namespace

const char* keyring_status_string(KeyringStatus s) {
    switch (s) {
        case KeyringStatus::Ok:
            return "ok";
        case KeyringStatus::NotFound:
            return "not_found";
        case KeyringStatus::AlreadyExists:
            return "already_exists";
        case KeyringStatus::Locked:
            return "locked";
        case KeyringStatus::UnlockedRequired:
            return "unlocked_required";
        case KeyringStatus::BadPassphrase:
            return "bad_passphrase";
        case KeyringStatus::InvalidName:
            return "invalid_name";
        case KeyringStatus::NotAuthorized:
            return "not_authorized";
        case KeyringStatus::IoError:
            return "io_error";
        case KeyringStatus::CryptoError:
            return "crypto_error";
        case KeyringStatus::Corrupt:
            return "corrupt";
    }
    return "unknown";
}

std::string Keyring::make_secret_id(SecretScope scope, std::string_view project_id,
                                    std::string_view name) {
    if (scope == SecretScope::Project) {
        return std::string("project:") + std::string(project_id) + "/" + std::string(name);
    }
    return std::string("global:") + std::string(name);
}

bool Keyring::is_valid_project_id(std::string_view project_id) {
    if (project_id.empty() || project_id.size() > 128) {
        return false;
    }
    for (char c : project_id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    if (project_id == "." || project_id == "..") {
        return false;
    }
    return true;
}

bool Keyring::is_valid_secret_name(std::string_view name) {
    return valid_secret_name(name);
}

bool Keyring::is_preferred_secret_name(std::string_view name) {
    constexpr std::string_view kPrefix = "scylla_";
    if (name.size() <= kPrefix.size() || name.size() > 128) {
        return false;
    }
    if (name.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    for (size_t i = kPrefix.size(); i < name.size(); ++i) {
        const char c = name[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool Keyring::normalize_secret_name(std::string_view input, std::string& out_name) {
    out_name.clear();
    std::string trimmed;
    trimmed.reserve(input.size());
    size_t begin = 0;
    while (begin < input.size() &&
           (input[begin] == ' ' || input[begin] == '\t' || input[begin] == '\r' || input[begin] == '\n')) {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin &&
           (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' ||
            input[end - 1] == '\n')) {
        --end;
    }
    trimmed.assign(input.data() + begin, end - begin);
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed.compare(0, 7, "scylla_") != 0) {
        trimmed.insert(0, "scylla_");
    }
    if (is_preferred_secret_name(trimmed) || is_valid_secret_name(trimmed)) {
        out_name = std::move(trimmed);
        return true;
    }
    return false;
}

std::wstring Keyring::default_app_vault_path() {
    Paths p = make_paths();
    const std::wstring dir = join_path(p.appdata, L"keyring");
    ensure_dir(dir);
    return join_path(dir, L"scylla.vault");
}

bool Keyring::app_vault_exists() {
    return file_exists(default_app_vault_path());
}

std::wstring Keyring::default_vault_path(std::string_view project_id) {
    if (!is_valid_project_id(project_id)) {
        return L"";
    }
    Paths p = make_paths();
    const std::wstring dir = join_path(p.appdata, L"keyrings");
    ensure_dir(dir);
    return join_path(dir, utf16(std::string(project_id)) + L".vault");
}

bool Keyring::legacy_project_vault_exists(std::string_view project_id) {
    const std::wstring path = default_vault_path(project_id);
    return !path.empty() && file_exists(path);
}

Keyring::~Keyring() {
    lock();
}

Keyring::Keyring(Keyring&& o) noexcept
    : vault_path_(std::move(o.vault_path_)),
      unlocked_(o.unlocked_),
      last_activity_ms_(o.last_activity_ms_),
      format_version_(o.format_version_),
      kdf_id_(o.kdf_id_),
      argon_m_(o.argon_m_),
      argon_t_(o.argon_t_),
      argon_p_(o.argon_p_),
      pbkdf2_iters_(o.pbkdf2_iters_),
      dek_(std::move(o.dek_)),
      entries_(std::move(o.entries_)),
      authorizations_(std::move(o.authorizations_)) {
    o.unlocked_ = false;
    o.last_activity_ms_ = 0;
    o.format_version_ = 0;
}

Keyring& Keyring::operator=(Keyring&& o) noexcept {
    if (this != &o) {
        lock();
        vault_path_ = std::move(o.vault_path_);
        unlocked_ = o.unlocked_;
        last_activity_ms_ = o.last_activity_ms_;
        format_version_ = o.format_version_;
        kdf_id_ = o.kdf_id_;
        argon_m_ = o.argon_m_;
        argon_t_ = o.argon_t_;
        argon_p_ = o.argon_p_;
        pbkdf2_iters_ = o.pbkdf2_iters_;
        dek_ = std::move(o.dek_);
        entries_ = std::move(o.entries_);
        authorizations_ = std::move(o.authorizations_);
        o.unlocked_ = false;
        o.last_activity_ms_ = 0;
        o.format_version_ = 0;
    }
    return *this;
}

void Keyring::wipe_dek() {
    secure_wipe_bytes(dek_);
}

void Keyring::wipe_secrets() {
    for (auto& e : entries_) {
        secure_wipe_string(e.value);
        secure_wipe_string(e.name);
        secure_wipe_string(e.description);
        secure_wipe_string(e.id);
        secure_wipe_string(e.project_id);
    }
    entries_.clear();
    authorizations_.clear();
}

void Keyring::lock() {
    wipe_secrets();
    wipe_dek();
    unlocked_ = false;
    last_activity_ms_ = 0;
}

void Keyring::mark_activity() {
    if (unlocked_) {
        last_activity_ms_ = now_ms();
    }
}

bool Keyring::should_autolock(std::uint64_t idle_ms) const {
    if (!unlocked_) {
        return false;
    }
    if (last_activity_ms_ == 0) {
        return true;
    }
    return (now_ms() - last_activity_ms_) >= idle_ms;
}

KeyringStatus Keyring::write_vault_file_v2(const std::wstring& path,
                                           const std::vector<std::uint8_t>& salt,
                                           std::uint32_t m_cost, std::uint32_t t_cost,
                                           std::uint32_t p_cost,
                                           const std::vector<std::uint8_t>& wrapped_dek,
                                           const std::vector<std::uint8_t>& wrapped_dek_nonce,
                                           const std::vector<std::uint8_t>& wrapped_dek_tag,
                                           const std::vector<std::uint8_t>& payload_ct,
                                           const std::vector<std::uint8_t>& payload_nonce,
                                           const std::vector<std::uint8_t>& payload_tag) {
    std::vector<std::uint8_t> blob;
    blob.reserve(128 + payload_ct.size());
    blob.insert(blob.end(), kMagicV2, kMagicV2 + 8);
    write_u32(blob, kFormatV2);
    write_u32(blob, kKdfArgon2id);
    write_u32(blob, m_cost);
    write_u32(blob, t_cost);
    write_u32(blob, p_cost);
    blob.insert(blob.end(), salt.begin(), salt.end());
    blob.insert(blob.end(), wrapped_dek_nonce.begin(), wrapped_dek_nonce.end());
    blob.insert(blob.end(), wrapped_dek.begin(), wrapped_dek.end());
    blob.insert(blob.end(), wrapped_dek_tag.begin(), wrapped_dek_tag.end());
    blob.insert(blob.end(), payload_nonce.begin(), payload_nonce.end());
    write_u32(blob, static_cast<std::uint32_t>(payload_ct.size()));
    blob.insert(blob.end(), payload_ct.begin(), payload_ct.end());
    blob.insert(blob.end(), payload_tag.begin(), payload_tag.end());
    return write_blob(path, blob);
}

KeyringStatus Keyring::write_vault_file_v1(const std::wstring& path,
                                           const std::vector<std::uint8_t>& salt,
                                           std::uint32_t iterations,
                                           const std::vector<std::uint8_t>& wrapped_dek,
                                           const std::vector<std::uint8_t>& wrapped_dek_nonce,
                                           const std::vector<std::uint8_t>& wrapped_dek_tag,
                                           const std::vector<std::uint8_t>& payload_ct,
                                           const std::vector<std::uint8_t>& payload_nonce,
                                           const std::vector<std::uint8_t>& payload_tag) {
    std::vector<std::uint8_t> blob;
    blob.insert(blob.end(), kMagicV1, kMagicV1 + 8);
    write_u32(blob, kFormatV1);
    write_u32(blob, kKdfPbkdf2Sha256);
    write_u32(blob, iterations);
    blob.insert(blob.end(), salt.begin(), salt.end());
    blob.insert(blob.end(), wrapped_dek_nonce.begin(), wrapped_dek_nonce.end());
    blob.insert(blob.end(), wrapped_dek.begin(), wrapped_dek.end());
    blob.insert(blob.end(), wrapped_dek_tag.begin(), wrapped_dek_tag.end());
    blob.insert(blob.end(), payload_nonce.begin(), payload_nonce.end());
    write_u32(blob, static_cast<std::uint32_t>(payload_ct.size()));
    blob.insert(blob.end(), payload_ct.begin(), payload_ct.end());
    blob.insert(blob.end(), payload_tag.begin(), payload_tag.end());
    return write_blob(path, blob);
}

KeyringStatus Keyring::create(const std::wstring& vault_path, std::string_view passphrase) {
    if (vault_path.empty() || passphrase.empty()) {
        return KeyringStatus::CryptoError;
    }
    if (file_exists(vault_path)) {
        return KeyringStatus::AlreadyExists;
    }

    std::vector<std::uint8_t> salt(kSaltLen);
    std::vector<std::uint8_t> dek(kDekLen);
    std::vector<std::uint8_t> kek(kKekLen);
    if (!random_bytes(salt.data(), kSaltLen) || !random_bytes(dek.data(), kDekLen)) {
        return KeyringStatus::CryptoError;
    }
    if (!argon2id_derive(passphrase, salt.data(), kSaltLen, kArgonMCost, kArgonTCost, kArgonParallel,
                         kek.data(), kKekLen)) {
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }

    std::vector<std::uint8_t> wrap_nonce(kNonceLen);
    std::vector<std::uint8_t> wrap_ct;
    std::vector<std::uint8_t> wrap_tag;
    if (!random_bytes(wrap_nonce.data(), kNonceLen) ||
        !aes_gcm_encrypt(kek.data(), kKekLen, wrap_nonce.data(), kNonceLen, dek.data(), kDekLen,
                         wrap_ct, wrap_tag)) {
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }

    const std::string empty_json = "{\"version\":2,\"secrets\":[],\"recipes\":[]}";
    std::vector<std::uint8_t> payload_nonce(kNonceLen);
    std::vector<std::uint8_t> payload_ct;
    std::vector<std::uint8_t> payload_tag;
    if (!random_bytes(payload_nonce.data(), kNonceLen) ||
        !aes_gcm_encrypt(dek.data(), kDekLen, payload_nonce.data(), kNonceLen,
                         reinterpret_cast<const std::uint8_t*>(empty_json.data()),
                         static_cast<ULONG>(empty_json.size()), payload_ct, payload_tag)) {
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }

    const KeyringStatus st =
        write_vault_file_v2(vault_path, salt, kArgonMCost, kArgonTCost, kArgonParallel, wrap_ct,
                            wrap_nonce, wrap_tag, payload_ct, payload_nonce, payload_tag);
    secure_wipe_bytes(kek);
    secure_wipe_bytes(dek);
    return st;
}

KeyringStatus Keyring::create_app(std::string_view passphrase) {
    return create(default_app_vault_path(), passphrase);
}

KeyringStatus Keyring::create_legacy_v1(const std::wstring& vault_path, std::string_view passphrase) {
    return create_legacy_v1_with_secret(vault_path, passphrase, {}, {}, {});
}

KeyringStatus Keyring::create_legacy_v1_with_secret(const std::wstring& vault_path,
                                                    std::string_view passphrase,
                                                    std::string_view name,
                                                    std::string_view value,
                                                    std::string_view description) {
    if (vault_path.empty() || passphrase.empty()) {
        return KeyringStatus::CryptoError;
    }
    if (file_exists(vault_path)) {
        return KeyringStatus::AlreadyExists;
    }
    std::vector<std::uint8_t> salt(kSaltLen);
    std::vector<std::uint8_t> dek(kDekLen);
    std::vector<std::uint8_t> kek(kKekLen);
    if (!random_bytes(salt.data(), kSaltLen) || !random_bytes(dek.data(), kDekLen)) {
        return KeyringStatus::CryptoError;
    }
    if (!pbkdf2_sha256(passphrase, salt.data(), kSaltLen, kPbkdf2Iterations, kek.data(), kKekLen)) {
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }
    std::vector<std::uint8_t> wrap_nonce(kNonceLen);
    std::vector<std::uint8_t> wrap_ct;
    std::vector<std::uint8_t> wrap_tag;
    if (!random_bytes(wrap_nonce.data(), kNonceLen) ||
        !aes_gcm_encrypt(kek.data(), kKekLen, wrap_nonce.data(), kNonceLen, dek.data(), kDekLen,
                         wrap_ct, wrap_tag)) {
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }
    std::string json = "[]";
    if (!name.empty()) {
        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> rows = {
            {std::string(name), {std::string(description), std::string(value)}}};
        json = entries_to_json_v1(rows);
        secure_wipe_string(rows[0].second.second);
    }
    std::vector<std::uint8_t> payload_nonce(kNonceLen);
    std::vector<std::uint8_t> payload_ct;
    std::vector<std::uint8_t> payload_tag;
    if (!random_bytes(payload_nonce.data(), kNonceLen) ||
        !aes_gcm_encrypt(dek.data(), kDekLen, payload_nonce.data(), kNonceLen,
                         reinterpret_cast<const std::uint8_t*>(json.data()),
                         static_cast<ULONG>(json.size()), payload_ct, payload_tag)) {
        secure_wipe_string(json);
        secure_wipe_bytes(kek);
        secure_wipe_bytes(dek);
        return KeyringStatus::CryptoError;
    }
    secure_wipe_string(json);
    const KeyringStatus st =
        write_vault_file_v1(vault_path, salt, kPbkdf2Iterations, wrap_ct, wrap_nonce, wrap_tag,
                            payload_ct, payload_nonce, payload_tag);
    secure_wipe_bytes(kek);
    secure_wipe_bytes(dek);
    return st;
}

KeyringStatus Keyring::open(const std::wstring& vault_path) {
    lock();
    vault_path_.clear();
    format_version_ = 0;
    if (vault_path.empty() || !file_exists(vault_path)) {
        return KeyringStatus::NotFound;
    }
    vault_path_ = vault_path;
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::open_app() {
    return open(default_app_vault_path());
}

KeyringStatus Keyring::unlock_v1_file(const std::wstring& path, std::string_view passphrase,
                                      std::vector<Entry>& out_entries) {
    out_entries.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return KeyringStatus::IoError;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return KeyringStatus::Corrupt;
    }
    std::vector<std::uint8_t> blob(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    const BOOL ok = ReadFile(h, blob.data(), static_cast<DWORD>(blob.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != blob.size()) {
        return KeyringStatus::IoError;
    }

    const std::uint8_t* p = blob.data();
    const std::uint8_t* end = blob.data() + blob.size();
    if (static_cast<size_t>(end - p) < 8 || std::memcmp(p, kMagicV1, 8) != 0) {
        return KeyringStatus::Corrupt;
    }
    p += 8;
    std::uint32_t version = 0, kdf = 0, iterations = 0;
    if (!read_u32(p, end, version) || !read_u32(p, end, kdf) || !read_u32(p, end, iterations)) {
        return KeyringStatus::Corrupt;
    }
    if (version != kFormatV1 || kdf != kKdfPbkdf2Sha256 || iterations < 100000) {
        return KeyringStatus::Corrupt;
    }
    std::vector<std::uint8_t> salt, wrap_nonce, wrap_ct, wrap_tag, payload_nonce, payload_ct,
        payload_tag;
    std::uint32_t payload_len = 0;
    if (!read_bytes(p, end, kSaltLen, salt) || !read_bytes(p, end, kNonceLen, wrap_nonce) ||
        !read_bytes(p, end, kDekLen, wrap_ct) || !read_bytes(p, end, kTagLen, wrap_tag) ||
        !read_bytes(p, end, kNonceLen, payload_nonce) || !read_u32(p, end, payload_len) ||
        !read_bytes(p, end, payload_len, payload_ct) || !read_bytes(p, end, kTagLen, payload_tag) ||
        p != end) {
        return KeyringStatus::Corrupt;
    }

    std::vector<std::uint8_t> kek(kKekLen);
    if (!pbkdf2_sha256(passphrase, salt.data(), kSaltLen, iterations, kek.data(), kKekLen)) {
        secure_wipe_bytes(kek);
        return KeyringStatus::CryptoError;
    }
    std::vector<std::uint8_t> dek;
    if (!aes_gcm_decrypt(kek.data(), kKekLen, wrap_nonce.data(), kNonceLen, wrap_ct.data(),
                         static_cast<ULONG>(wrap_ct.size()), wrap_tag.data(), kTagLen, dek)) {
        secure_wipe_bytes(kek);
        return KeyringStatus::BadPassphrase;
    }
    secure_wipe_bytes(kek);

    std::vector<std::uint8_t> plain;
    if (!aes_gcm_decrypt(dek.data(), static_cast<ULONG>(dek.size()), payload_nonce.data(), kNonceLen,
                         payload_ct.data(), static_cast<ULONG>(payload_ct.size()), payload_tag.data(),
                         kTagLen, plain)) {
        secure_wipe_bytes(dek);
        return KeyringStatus::Corrupt;
    }
    secure_wipe_bytes(dek);

    std::string json(reinterpret_cast<const char*>(plain.data()), plain.size());
    secure_wipe_bytes(plain);
    std::vector<std::tuple<std::string, std::string, std::string>> parsed;
    if (!parse_entries_json_v1(json, parsed)) {
        secure_wipe_string(json);
        return KeyringStatus::Corrupt;
    }
    secure_wipe_string(json);
    for (auto& row : parsed) {
        Entry e;
        e.name = std::move(std::get<0>(row));
        e.description = std::move(std::get<1>(row));
        e.value = std::move(std::get<2>(row));
        e.scope = SecretScope::Project;
        out_entries.push_back(std::move(e));
    }
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::decrypt_into_memory(std::string_view passphrase) {
    HANDLE h = CreateFileW(vault_path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return KeyringStatus::IoError;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return KeyringStatus::Corrupt;
    }
    std::vector<std::uint8_t> blob(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    const BOOL ok = ReadFile(h, blob.data(), static_cast<DWORD>(blob.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != blob.size()) {
        return KeyringStatus::IoError;
    }

    const std::uint8_t* p = blob.data();
    const std::uint8_t* end = blob.data() + blob.size();
    if (static_cast<size_t>(end - p) < 8) {
        return KeyringStatus::Corrupt;
    }
    const bool is_v2 = std::memcmp(p, kMagicV2, 8) == 0;
    const bool is_v1 = std::memcmp(p, kMagicV1, 8) == 0;
    if (!is_v1 && !is_v2) {
        return KeyringStatus::Corrupt;
    }
    p += 8;
    std::uint32_t version = 0, kdf = 0;
    if (!read_u32(p, end, version) || !read_u32(p, end, kdf)) {
        return KeyringStatus::Corrupt;
    }

    std::vector<std::uint8_t> salt, wrap_nonce, wrap_ct, wrap_tag, payload_nonce, payload_ct,
        payload_tag;
    std::uint32_t payload_len = 0;
    std::uint32_t m_cost = 0, t_cost = 0, p_cost = 0, iterations = 0;

    if (is_v2) {
        if (version != kFormatV2 || kdf != kKdfArgon2id) {
            return KeyringStatus::Corrupt;
        }
        if (!read_u32(p, end, m_cost) || !read_u32(p, end, t_cost) || !read_u32(p, end, p_cost)) {
            return KeyringStatus::Corrupt;
        }
        if (m_cost < 8 || t_cost < 1 || p_cost < 1) {
            return KeyringStatus::Corrupt;
        }
    } else {
        if (version != kFormatV1 || kdf != kKdfPbkdf2Sha256) {
            return KeyringStatus::Corrupt;
        }
        if (!read_u32(p, end, iterations) || iterations < 100000) {
            return KeyringStatus::Corrupt;
        }
    }

    if (!read_bytes(p, end, kSaltLen, salt) || !read_bytes(p, end, kNonceLen, wrap_nonce) ||
        !read_bytes(p, end, kDekLen, wrap_ct) || !read_bytes(p, end, kTagLen, wrap_tag) ||
        !read_bytes(p, end, kNonceLen, payload_nonce) || !read_u32(p, end, payload_len) ||
        !read_bytes(p, end, payload_len, payload_ct) || !read_bytes(p, end, kTagLen, payload_tag) ||
        p != end) {
        return KeyringStatus::Corrupt;
    }

    std::vector<std::uint8_t> kek(kKekLen);
    bool derived = false;
    if (is_v2) {
        derived = argon2id_derive(passphrase, salt.data(), kSaltLen, m_cost, t_cost, p_cost,
                                  kek.data(), kKekLen);
    } else {
        derived = pbkdf2_sha256(passphrase, salt.data(), kSaltLen, iterations, kek.data(), kKekLen);
    }
    if (!derived) {
        secure_wipe_bytes(kek);
        return KeyringStatus::CryptoError;
    }

    std::vector<std::uint8_t> dek;
    if (!aes_gcm_decrypt(kek.data(), kKekLen, wrap_nonce.data(), kNonceLen, wrap_ct.data(),
                         static_cast<ULONG>(wrap_ct.size()), wrap_tag.data(), kTagLen, dek)) {
        secure_wipe_bytes(kek);
        return KeyringStatus::BadPassphrase;
    }
    secure_wipe_bytes(kek);

    std::vector<std::uint8_t> plain;
    if (!aes_gcm_decrypt(dek.data(), static_cast<ULONG>(dek.size()), payload_nonce.data(), kNonceLen,
                         payload_ct.data(), static_cast<ULONG>(payload_ct.size()), payload_tag.data(),
                         kTagLen, plain)) {
        secure_wipe_bytes(dek);
        return KeyringStatus::Corrupt;
    }

    std::string json(reinterpret_cast<const char*>(plain.data()), plain.size());
    secure_wipe_bytes(plain);

    std::vector<ParsedSecret> parsed;
    if (is_v2) {
        if (!parse_payload_v2(json, parsed)) {
            secure_wipe_string(json);
            secure_wipe_bytes(dek);
            return KeyringStatus::Corrupt;
        }
    } else {
        std::vector<std::tuple<std::string, std::string, std::string>> v1;
        if (!parse_entries_json_v1(json, v1)) {
            secure_wipe_string(json);
            secure_wipe_bytes(dek);
            return KeyringStatus::Corrupt;
        }
        for (auto& row : v1) {
            ParsedSecret s;
            s.name = std::move(std::get<0>(row));
            s.description = std::move(std::get<1>(row));
            s.value = std::move(std::get<2>(row));
            s.scope = "global";
            s.id = std::string("global:") + s.name;
            parsed.push_back(std::move(s));
        }
    }
    secure_wipe_string(json);

    wipe_secrets();
    wipe_dek();
    dek_ = std::move(dek);
    format_version_ = is_v2 ? kFormatV2 : kFormatV1;
    kdf_id_ = is_v2 ? kKdfArgon2id : kKdfPbkdf2Sha256;
    argon_m_ = m_cost;
    argon_t_ = t_cost;
    argon_p_ = p_cost;
    pbkdf2_iters_ = iterations;
    entries_.reserve(parsed.size());
    for (auto& row : parsed) {
        Entry e;
        e.id = std::move(row.id);
        e.name = std::move(row.name);
        e.description = std::move(row.description);
        e.value = std::move(row.value);
        e.project_id = std::move(row.project_id);
        e.scope = (row.scope == "project") ? SecretScope::Project : SecretScope::Global;
        entries_.push_back(std::move(e));
    }
    unlocked_ = true;
    mark_activity();
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::unlock(std::string_view passphrase) {
    if (vault_path_.empty()) {
        return KeyringStatus::NotFound;
    }
    if (unlocked_) {
        lock();
    }
    const KeyringStatus st = decrypt_into_memory(passphrase);
    if (st == KeyringStatus::BadPassphrase || st == KeyringStatus::Corrupt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return st;
}

KeyringStatus Keyring::persist_unlocked() {
    if (!unlocked_ || dek_.size() != kDekLen) {
        return KeyringStatus::UnlockedRequired;
    }

    HANDLE h = CreateFileW(vault_path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return KeyringStatus::IoError;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
        CloseHandle(h);
        return KeyringStatus::Corrupt;
    }
    std::vector<std::uint8_t> blob(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    ReadFile(h, blob.data(), static_cast<DWORD>(blob.size()), &got, nullptr);
    CloseHandle(h);

    const std::uint8_t* p = blob.data();
    const std::uint8_t* end = blob.data() + got;
    if (static_cast<size_t>(end - p) < 8) {
        return KeyringStatus::Corrupt;
    }
    const bool is_v2 = std::memcmp(p, kMagicV2, 8) == 0;
    p += 8;
    std::uint32_t version = 0, kdf = 0;
    if (!read_u32(p, end, version) || !read_u32(p, end, kdf)) {
        return KeyringStatus::Corrupt;
    }
    std::uint32_t m_cost = 0, t_cost = 0, p_cost = 0, iterations = 0;
    if (is_v2) {
        if (!read_u32(p, end, m_cost) || !read_u32(p, end, t_cost) || !read_u32(p, end, p_cost)) {
            return KeyringStatus::Corrupt;
        }
    } else {
        if (!read_u32(p, end, iterations)) {
            return KeyringStatus::Corrupt;
        }
    }
    std::vector<std::uint8_t> salt, wrap_nonce, wrap_ct, wrap_tag;
    if (!read_bytes(p, end, kSaltLen, salt) || !read_bytes(p, end, kNonceLen, wrap_nonce) ||
        !read_bytes(p, end, kDekLen, wrap_ct) || !read_bytes(p, end, kTagLen, wrap_tag)) {
        return KeyringStatus::Corrupt;
    }

    // Always rewrite as v2 (upgrade path when persisting an unlocked legacy file).
    std::vector<ParsedSecret> rows;
    rows.reserve(entries_.size());
    for (const auto& e : entries_) {
        ParsedSecret s;
        s.id = e.id.empty() ? make_secret_id(e.scope, e.project_id, e.name) : e.id;
        s.scope = e.scope == SecretScope::Project ? "project" : "global";
        s.project_id = e.project_id;
        s.name = e.name;
        s.description = e.description;
        s.value = e.value;
        rows.push_back(std::move(s));
    }
    std::string json = entries_to_json_v2(rows);
    for (auto& r : rows) {
        secure_wipe_string(r.value);
    }

    std::vector<std::uint8_t> payload_nonce(kNonceLen);
    std::vector<std::uint8_t> payload_ct;
    std::vector<std::uint8_t> payload_tag;
    if (!random_bytes(payload_nonce.data(), kNonceLen) ||
        !aes_gcm_encrypt(dek_.data(), kDekLen, payload_nonce.data(), kNonceLen,
                         reinterpret_cast<const std::uint8_t*>(json.data()),
                         static_cast<ULONG>(json.size()), payload_ct, payload_tag)) {
        secure_wipe_string(json);
        return KeyringStatus::CryptoError;
    }
    secure_wipe_string(json);

    // If source was v1, re-wrap DEK with Argon2id using a fresh salt so future unlocks use v2.
    if (!is_v2) {
        return KeyringStatus::Corrupt;  // should not persist v1 in-place; migrate first
    }

    const KeyringStatus st =
        write_vault_file_v2(vault_path_, salt, m_cost, t_cost, p_cost, wrap_ct, wrap_nonce, wrap_tag,
                            payload_ct, payload_nonce, payload_tag);
    if (st == KeyringStatus::Ok) {
        format_version_ = kFormatV2;
        kdf_id_ = kKdfArgon2id;
        argon_m_ = m_cost;
        argon_t_ = t_cost;
        argon_p_ = p_cost;
    }
    return st;
}

std::vector<SecretRef> Keyring::list_refs() const {
    std::vector<SecretRef> out;
    if (!unlocked_) {
        return out;
    }
    out.reserve(entries_.size());
    for (const auto& e : entries_) {
        out.push_back(SecretRef{e.id, e.name, e.description, e.scope, e.project_id});
    }
    return out;
}

std::vector<SecretRef> Keyring::list_refs_for_ui(std::string_view active_project_id) const {
    std::vector<SecretRef> out;
    if (!unlocked_) {
        return out;
    }
    for (const auto& e : entries_) {
        if (e.scope == SecretScope::Global) {
            out.push_back(SecretRef{e.id, e.name, e.description, e.scope, e.project_id});
        } else if (!active_project_id.empty() && e.project_id == active_project_id) {
            out.push_back(SecretRef{e.id, e.name, e.description, e.scope, e.project_id});
        }
    }
    return out;
}

KeyringStatus Keyring::add_secret(std::string_view name, std::string_view value,
                                  std::string_view description, SecretScope scope,
                                  std::string_view project_id) {
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    if (!valid_secret_name(name)) {
        return KeyringStatus::InvalidName;
    }
    if (scope == SecretScope::Project) {
        if (!is_valid_project_id(project_id)) {
            return KeyringStatus::InvalidName;
        }
    }
    mark_activity();
    const std::string id = make_secret_id(scope, project_id, name);
    for (auto& e : entries_) {
        if (e.name == name && e.scope == scope &&
            (scope == SecretScope::Global || e.project_id == project_id)) {
            secure_wipe_string(e.value);
            e.value.assign(value.data(), value.size());
            e.description.assign(description.data(), description.size());
            e.id = id;
            return persist_unlocked();
        }
    }
    Entry e;
    e.id = id;
    e.scope = scope;
    if (scope == SecretScope::Project) {
        e.project_id.assign(project_id.data(), project_id.size());
    }
    e.name.assign(name.data(), name.size());
    e.description.assign(description.data(), description.size());
    e.value.assign(value.data(), value.size());
    entries_.push_back(std::move(e));
    return persist_unlocked();
}

KeyringStatus Keyring::update_description(std::string_view name, std::string_view description) {
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    mark_activity();
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const Entry& e) { return e.name == name; });
    if (it == entries_.end()) {
        return KeyringStatus::NotFound;
    }
    it->description.assign(description.data(), description.size());
    return persist_unlocked();
}

KeyringStatus Keyring::remove_secret(std::string_view name) {
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    mark_activity();
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const Entry& e) { return e.name == name; });
    if (it == entries_.end()) {
        return KeyringStatus::NotFound;
    }
    secure_wipe_string(it->value);
    entries_.erase(it);
    authorizations_.erase(std::remove_if(authorizations_.begin(), authorizations_.end(),
                                         [&](const Auth& a) { return a.name == name; }),
                          authorizations_.end());
    return persist_unlocked();
}

KeyringStatus Keyring::migrate_from_project_vault(const std::wstring& legacy_path,
                                                  std::string_view passphrase,
                                                  std::string_view project_id) {
    if (!unlocked_ || format_version_ != kFormatV2) {
        return KeyringStatus::UnlockedRequired;
    }
    if (!is_valid_project_id(project_id) || legacy_path.empty() || !file_exists(legacy_path)) {
        return KeyringStatus::NotFound;
    }
    std::vector<Entry> imported;
    const KeyringStatus st = unlock_v1_file(legacy_path, passphrase, imported);
    if (st != KeyringStatus::Ok) {
        for (auto& e : imported) {
            secure_wipe_string(e.value);
        }
        return st;
    }
    mark_activity();
    for (auto& e : imported) {
        e.scope = SecretScope::Project;
        e.project_id.assign(project_id.data(), project_id.size());
        e.id = make_secret_id(SecretScope::Project, project_id, e.name);
        bool replaced = false;
        for (auto& existing : entries_) {
            if (existing.scope == SecretScope::Project && existing.project_id == project_id &&
                existing.name == e.name) {
                secure_wipe_string(existing.value);
                existing = std::move(e);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            entries_.push_back(std::move(e));
        }
    }
    const KeyringStatus persist = persist_unlocked();
    if (persist != KeyringStatus::Ok) {
        return persist;
    }
    const std::wstring archived = legacy_path + L".migrated";
    MoveFileExW(legacy_path.c_str(), archived.c_str(), MOVEFILE_REPLACE_EXISTING);
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::copy_secret_value_to_clipboard(std::string_view name) {
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    mark_activity();
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.name == name; });
    if (it == entries_.end()) {
        return KeyringStatus::NotFound;
    }
    std::wstring wide = utf16(it->value);
    if (!OpenClipboard(nullptr)) {
        return KeyringStatus::IoError;
    }
    EmptyClipboard();
    const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return KeyringStatus::IoError;
    }
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        return KeyringStatus::IoError;
    }
    std::memcpy(locked, wide.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return KeyringStatus::IoError;
    }
    CloseClipboard();
    if (!wide.empty()) {
        SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    }
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::authorize_use(std::string_view name, std::string_view operation_id) {
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    if (operation_id.empty()) {
        return KeyringStatus::InvalidName;
    }
    mark_activity();
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.name == name; });
    if (it == entries_.end()) {
        return KeyringStatus::NotFound;
    }
    authorizations_.push_back(Auth{std::string(name), std::string(operation_id)});
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::build_authorized_env_block(std::string_view operation_id,
                                                  std::wstring& out_fragment) {
    out_fragment.clear();
    if (!unlocked_) {
        return KeyringStatus::UnlockedRequired;
    }
    mark_activity();

    std::vector<size_t> consumed;
    for (size_t ai = 0; ai < authorizations_.size(); ++ai) {
        const Auth& a = authorizations_[ai];
        if (a.operation_id != operation_id) {
            continue;
        }
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [&](const Entry& e) { return e.name == a.name; });
        if (it == entries_.end()) {
            continue;
        }
        out_fragment += utf16(it->name);
        out_fragment.push_back(L'=');
        out_fragment += utf16(it->value);
        out_fragment.push_back(L'\0');
        consumed.push_back(ai);
    }
    if (consumed.empty()) {
        return KeyringStatus::NotAuthorized;
    }
    for (auto it = consumed.rbegin(); it != consumed.rend(); ++it) {
        authorizations_.erase(authorizations_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    return KeyringStatus::Ok;
}

KeyringStatus Keyring::inject_env_for_process(HANDLE process, std::string_view operation_id) {
    if (process == nullptr || process == GetCurrentProcess()) {
        std::wstring frag;
        const KeyringStatus st = build_authorized_env_block(operation_id, frag);
        if (st != KeyringStatus::Ok) {
            return st;
        }
        size_t i = 0;
        while (i < frag.size()) {
            size_t start = i;
            while (i < frag.size() && frag[i] != L'\0') {
                ++i;
            }
            if (i > start) {
                const std::wstring entry = frag.substr(start, i - start);
                const size_t eq = entry.find(L'=');
                if (eq != std::wstring::npos && eq > 0) {
                    const std::wstring name = entry.substr(0, eq);
                    const std::wstring value = entry.substr(eq + 1);
                    SetEnvironmentVariableW(name.c_str(), value.c_str());
                    std::wstring wipe = value;
                    if (!wipe.empty()) {
                        SecureZeroMemory(wipe.data(), wipe.size() * sizeof(wchar_t));
                    }
                }
            }
            if (i < frag.size() && frag[i] == L'\0') {
                ++i;
            }
        }
        if (!frag.empty()) {
            SecureZeroMemory(frag.data(), frag.size() * sizeof(wchar_t));
        }
        return KeyringStatus::Ok;
    }
    return KeyringStatus::NotAuthorized;
}

}  // namespace scyllagpt
