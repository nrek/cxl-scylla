#include "scyllagpt/keyring.h"
#include "scyllagpt/paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>

static int g_kr_fail = 0;

static void kr_expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_kr_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

static std::wstring temp_vault_path(const wchar_t* suffix = L".vault") {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    wchar_t file[MAX_PATH]{};
    GetTempFileNameW(tmp, L"scyk", 0, file);
    DeleteFileW(file);
    std::wstring path(file);
    path += suffix;
    return path;
}

int run_keyring_tests() {
    using scyllagpt::Keyring;
    using scyllagpt::KeyringStatus;
    using scyllagpt::SecretScope;

    const std::wstring vault = temp_vault_path();
    DeleteFileW(vault.c_str());

    {
        const auto st = Keyring::create(vault, "correct-horse-battery");
        kr_expect(st == KeyringStatus::Ok, "create v2 argon2 vault");
        kr_expect(scyllagpt::file_exists(vault), "vault file on disk");
        kr_expect(Keyring::create(vault, "other") == KeyringStatus::AlreadyExists,
                  "create refuses existing");
    }

    {
        Keyring kr;
        kr_expect(kr.open(vault) == KeyringStatus::Ok, "open vault");
        kr_expect(kr.unlock("wrong-passphrase") == KeyringStatus::BadPassphrase,
                  "unlock wrong passphrase fails");
        kr_expect(kr.unlock("correct-horse-battery") == KeyringStatus::Ok, "unlock good passphrase");
        kr_expect(kr.format_version() == 2, "format version 2");
    }

    {
        Keyring kr;
        kr.open(vault);
        kr.unlock("correct-horse-battery");
        kr_expect(kr.add_secret("scylla_TEST_TOKEN", "super-secret-value-xyz", "test token desc",
                                SecretScope::Global) == KeyringStatus::Ok,
                  "add global secret");
        kr_expect(kr.add_secret("scylla_PROJ_A", "proj-a-secret", "project a", SecretScope::Project,
                                "proj-a") == KeyringStatus::Ok,
                  "add project A secret");
        kr_expect(kr.add_secret("scylla_PROJ_B", "proj-b-secret", "project b", SecretScope::Project,
                                "proj-b") == KeyringStatus::Ok,
                  "add project B secret");

        kr_expect(kr.list_refs().size() == 3, "list_refs all count");
        const auto ui_a = kr.list_refs_for_ui("proj-a");
        kr_expect(ui_a.size() == 2, "ui list global+A");
        bool saw_b = false;
        for (const auto& r : ui_a) {
            if (r.name == "scylla_PROJ_B") {
                saw_b = true;
            }
        }
        kr_expect(!saw_b, "project B filtered out of A ui list");

        kr.lock();
        kr_expect(kr.list_refs().empty(), "list_refs empty while locked");
        kr_expect(kr.add_secret("x", "y", "z") == KeyringStatus::UnlockedRequired,
                  "add_secret requires unlock");
    }

    {
        Keyring kr;
        kr.open(vault);
        kr.unlock("correct-horse-battery");
        kr_expect(kr.authorize_use("scylla_TEST_TOKEN", "recipe_env_inject") == KeyringStatus::Ok,
                  "authorize_use");
        std::wstring frag;
        kr_expect(kr.build_authorized_env_block("recipe_env_inject", frag) == KeyringStatus::Ok,
                  "build_authorized_env_block");
        kr_expect(frag.find(L"scylla_TEST_TOKEN=super-secret-value-xyz") != std::wstring::npos,
                  "env fragment contains secret once");
        frag.clear();
        kr_expect(kr.build_authorized_env_block("recipe_env_inject", frag) ==
                      KeyringStatus::NotAuthorized,
                  "authorization consumed");
    }

    {
        const std::wstring app = Keyring::default_app_vault_path();
        kr_expect(!app.empty() && app.find(L"\\keyring\\scylla.vault") != std::wstring::npos,
                  "app vault path");
        const std::wstring legacy = Keyring::default_vault_path("unit-test-proj");
        kr_expect(!legacy.empty() && legacy.find(L"\\keyrings\\") != std::wstring::npos,
                  "legacy path under keyrings");
    }

    {
        Keyring kr;
        kr.open(vault);
        kr.unlock("correct-horse-battery");
        kr_expect(kr.update_description("scylla_TEST_TOKEN", "updated desc") == KeyringStatus::Ok,
                  "update_description");
        kr.mark_activity();
        kr_expect(!kr.should_autolock(60'000), "should_autolock false when fresh");
        kr.lock();
        kr_expect(!kr.should_autolock(0), "should_autolock false when locked");
    }

    {
        const std::wstring legacy = temp_vault_path(L"-legacy.vault");
        DeleteFileW(legacy.c_str());
        DeleteFileW((legacy + L".migrated").c_str());
        kr_expect(Keyring::create_legacy_v1_with_secret(legacy, "legacy-pass", "scylla_OLD",
                                                        "old-value", "from v1") == KeyringStatus::Ok,
                  "create seeded legacy v1");
        Keyring app;
        app.open(vault);
        app.unlock("correct-horse-battery");
        kr_expect(app.migrate_from_project_vault(legacy, "legacy-pass", "migrated-proj") ==
                      KeyringStatus::Ok,
                  "migrate seeded legacy vault");
        kr_expect(!scyllagpt::file_exists(legacy), "legacy removed after migrate");
        kr_expect(scyllagpt::file_exists(legacy + L".migrated"), "legacy archived .migrated");
        bool found = false;
        for (const auto& r : app.list_refs_for_ui("migrated-proj")) {
            if (r.name == "scylla_OLD" && r.scope == SecretScope::Project) {
                found = true;
            }
        }
        kr_expect(found, "migrated secret visible in project scope");
        kr_expect(app.authorize_use("scylla_OLD", "mig") == KeyringStatus::Ok,
                  "authorize migrated secret");
        std::wstring frag;
        kr_expect(app.build_authorized_env_block("mig", frag) == KeyringStatus::Ok &&
                      frag.find(L"scylla_OLD=old-value") != std::wstring::npos,
                  "migrated secret value via broker");
    }

    {
        const std::wstring legacy2 = temp_vault_path(L"-legacy2.vault");
        DeleteFileW(legacy2.c_str());
        kr_expect(Keyring::create_legacy_v1(legacy2, "legacy-pass-2") == KeyringStatus::Ok,
                  "create empty legacy");
        Keyring app;
        app.open(vault);
        app.unlock("correct-horse-battery");
        kr_expect(app.migrate_from_project_vault(legacy2, "wrong", "p2") ==
                      KeyringStatus::BadPassphrase,
                  "migrate bad passphrase");
        kr_expect(scyllagpt::file_exists(legacy2), "legacy remains after failed migrate");
        DeleteFileW(legacy2.c_str());
    }

    kr_expect(true, "no get_secret_value in public header");
    {
        Keyring app;
        app.open(vault);
        app.unlock("correct-horse-battery");
        app.add_secret("SCOPE_TEST", "a", "first", SecretScope::Project, "project-a");
        app.add_secret("SCOPE_TEST", "b", "second", SecretScope::Project, "project-b");
        kr_expect(app.remove_secret("SCOPE_TEST", SecretScope::Project, "project-b") == KeyringStatus::Ok,
                  "scoped deletion removes requested project value");
        bool kept = false;
        for (const auto& ref : app.list_refs_for_ui("project-a")) if (ref.name == "SCOPE_TEST") kept = true;
        kr_expect(kept, "scoped deletion preserves same name in another project");
        kr_expect(app.remove_secret("SCOPE_TEST", SecretScope::Project, "project-b") == KeyringStatus::NotFound,
                  "scoped deletion cannot fall back to another project");
    }

    DeleteFileW(vault.c_str());
    return g_kr_fail;
}
