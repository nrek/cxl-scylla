#pragma once

// Scylla App-Level Keyring — one encrypted vault for the Workbench (Phase 1).
//
// Security invariants (API contract):
// - There is NO get_secret_value() for the agent layer.
// - list_refs() returns metadata only (never secret values).
// - Plaintext values leave this module only via inject_env_for_process /
//   build_authorized_env_block after authorize_use(name, operation_id),
//   or copy_secret_value_to_clipboard (human UI only).
// - App vault: %LOCALAPPDATA%\ScyllaGPT\keyring\scylla.vault
// - Legacy per-project vaults: %LOCALAPPDATA%\ScyllaGPT\keyrings\<project_id>.vault
//   (migration only; never inside the Git workspace).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class SecretScope {
    Global = 0,
    Project = 1,
};

struct SecretRef {
    std::string id;
    std::string name;  // reference / display name (e.g. scylla_RDS_DBPWS)
    std::string description;
    SecretScope scope = SecretScope::Global;
    std::string project_id;  // set when scope == Project
};

enum class KeyringStatus {
    Ok = 0,
    NotFound,
    AlreadyExists,
    Locked,
    UnlockedRequired,
    BadPassphrase,
    InvalidName,
    NotAuthorized,
    IoError,
    CryptoError,
    Corrupt,
};

const char* keyring_status_string(KeyringStatus s);

class Keyring {
public:
    Keyring() = default;
    ~Keyring();

    Keyring(const Keyring&) = delete;
    Keyring& operator=(const Keyring&) = delete;
    Keyring(Keyring&&) noexcept;
    Keyring& operator=(Keyring&&) noexcept;

    // --- App vault (primary) ---
    static std::wstring default_app_vault_path();
    static bool app_vault_exists();
    static KeyringStatus create_app(std::string_view passphrase);
    KeyringStatus open_app();

    // Create / open at an explicit path (tests + migration helpers).
    static KeyringStatus create(const std::wstring& vault_path, std::string_view passphrase);
    KeyringStatus open(const std::wstring& vault_path);

    // Legacy per-project vault path (v1 PBKDF2). Used for migration detection only.
    static std::wstring default_vault_path(std::string_view project_id);
    static bool legacy_project_vault_exists(std::string_view project_id);
    static bool is_valid_project_id(std::string_view project_id);

    // Existing vault rule: [A-Za-z0-9_-]{1,128}
    static bool is_valid_secret_name(std::string_view name);
    // Preferred product namespace: scylla_[A-Z0-9_]+
    static bool is_preferred_secret_name(std::string_view name);
    // Accept preferred OR valid_secret_name. Optionally auto-prefix "scylla_".
    static bool normalize_secret_name(std::string_view input, std::string& out_name);

    KeyringStatus unlock(std::string_view passphrase);
    void lock();
    bool is_unlocked() const { return unlocked_; }
    bool vault_bound() const { return !vault_path_.empty(); }

    // All secrets (unlocked only).
    std::vector<SecretRef> list_refs() const;
    // Global + secrets for active_project_id (empty project_id → global only).
    std::vector<SecretRef> list_refs_for_ui(std::string_view active_project_id) const;

    KeyringStatus add_secret(std::string_view name, std::string_view value,
                             std::string_view description, SecretScope scope = SecretScope::Global,
                             std::string_view project_id = {});
    KeyringStatus update_description(std::string_view name, std::string_view description);
    KeyringStatus remove_secret(std::string_view name);

    // Import secrets from a legacy v1 project vault into this unlocked app vault (project scope).
    // On success, renames legacy_path → legacy_path + L".migrated".
    KeyringStatus migrate_from_project_vault(const std::wstring& legacy_path,
                                             std::string_view passphrase,
                                             std::string_view project_id);

    // Test / migration tooling only: create a v1 PBKDF2 vault (legacy format).
    static KeyringStatus create_legacy_v1(const std::wstring& vault_path,
                                          std::string_view passphrase);
    static KeyringStatus create_legacy_v1_with_secret(const std::wstring& vault_path,
                                                      std::string_view passphrase,
                                                      std::string_view name,
                                                      std::string_view value,
                                                      std::string_view description);

    // Human Workbench UI only — copies plaintext to the clipboard; never returns the value.
    KeyringStatus copy_secret_value_to_clipboard(std::string_view name);

    KeyringStatus authorize_use(std::string_view name, std::string_view operation_id);
    KeyringStatus inject_env_for_process(HANDLE process, std::string_view operation_id);
    KeyringStatus build_authorized_env_block(std::string_view operation_id,
                                             std::wstring& out_fragment);

    void mark_activity();
    bool should_autolock(std::uint64_t idle_ms) const;

    const std::wstring& vault_path() const { return vault_path_; }
    std::uint32_t format_version() const { return format_version_; }

private:
    struct Entry {
        std::string id;
        SecretScope scope = SecretScope::Global;
        std::string project_id;
        std::string name;
        std::string description;
        std::string value;
    };

    struct Auth {
        std::string name;
        std::string operation_id;
    };

    void wipe_secrets();
    void wipe_dek();
    KeyringStatus persist_unlocked();
    KeyringStatus decrypt_into_memory(std::string_view passphrase);
    static std::string make_secret_id(SecretScope scope, std::string_view project_id,
                                      std::string_view name);
    static KeyringStatus write_vault_file_v2(const std::wstring& path,
                                             const std::vector<std::uint8_t>& salt,
                                             std::uint32_t m_cost, std::uint32_t t_cost,
                                             std::uint32_t p_cost,
                                             const std::vector<std::uint8_t>& wrapped_dek,
                                             const std::vector<std::uint8_t>& wrapped_dek_nonce,
                                             const std::vector<std::uint8_t>& wrapped_dek_tag,
                                             const std::vector<std::uint8_t>& payload_ct,
                                             const std::vector<std::uint8_t>& payload_nonce,
                                             const std::vector<std::uint8_t>& payload_tag);
    // Legacy v1 writer (tests may still create via migrate path only — kept private for migrate unlock).
    static KeyringStatus write_vault_file_v1(const std::wstring& path,
                                             const std::vector<std::uint8_t>& salt,
                                             std::uint32_t iterations,
                                             const std::vector<std::uint8_t>& wrapped_dek,
                                             const std::vector<std::uint8_t>& wrapped_dek_nonce,
                                             const std::vector<std::uint8_t>& wrapped_dek_tag,
                                             const std::vector<std::uint8_t>& payload_ct,
                                             const std::vector<std::uint8_t>& payload_nonce,
                                             const std::vector<std::uint8_t>& payload_tag);

    // Unlock a v1 file into temporary entries (does not bind this Keyring).
    static KeyringStatus unlock_v1_file(const std::wstring& path, std::string_view passphrase,
                                        std::vector<Entry>& out_entries);

    std::wstring vault_path_;
    bool unlocked_ = false;
    std::uint64_t last_activity_ms_ = 0;
    std::uint32_t format_version_ = 0;
    std::uint32_t kdf_id_ = 0;
    std::uint32_t argon_m_ = 0;
    std::uint32_t argon_t_ = 0;
    std::uint32_t argon_p_ = 0;
    std::uint32_t pbkdf2_iters_ = 0;

    std::vector<std::uint8_t> dek_;
    std::vector<Entry> entries_;
    std::vector<Auth> authorizations_;
};

}  // namespace scyllagpt
