#include "scyllagpt/project_environment.h"
#include "scyllagpt/keyring.h"
#include "scyllagpt/paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>

static int g_env_fail = 0;

static void env_expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_env_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

static std::wstring temp_path(const wchar_t* suffix) {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    wchar_t file[MAX_PATH]{};
    GetTempFileNameW(tmp, L"scye", 0, file);
    DeleteFileW(file);
    std::wstring path(file);
    path += suffix;
    return path;
}

int run_project_environment_tests() {
    using scyllagpt::EnvResolveStatus;
    using scyllagpt::EnvVarKind;
    using scyllagpt::Keyring;
    using scyllagpt::KeyringStatus;
    using scyllagpt::ProjectEnvironmentManager;
    using scyllagpt::ProjectEnvironmentVariable;
    using scyllagpt::SecretScope;

    g_env_fail = 0;

    env_expect(ProjectEnvironmentManager::is_valid_var_name("DB_HOST"), "valid var name");
    env_expect(!ProjectEnvironmentManager::is_valid_var_name("1BAD"), "invalid var name");

    const std::wstring env_path = temp_path(L".environments.json");
    DeleteFileW(env_path.c_str());

    ProjectEnvironmentManager mgr;
    const std::string pid = "proj-test";
    const std::string eid = mgr.add_environment(pid, "Development", "dev env");
    env_expect(!eid.empty(), "add environment");
    env_expect(mgr.active_environment_id(pid) == eid, "active set on first add");

    ProjectEnvironmentVariable plain;
    plain.name = "APP_ENV";
    plain.kind = EnvVarKind::Plain;
    plain.plain_value = "development";
    env_expect(mgr.add_variable(pid, eid, plain), "add plain var");

    env_expect(mgr.save(env_path), "save environments");
    ProjectEnvironmentManager loaded;
    env_expect(loaded.load(env_path), "load environments");
    env_expect(loaded.find_environment(pid, eid) != nullptr, "loaded env exists");
    env_expect(loaded.find_environment(pid, eid)->variables.size() == 1, "loaded var count");

    const std::wstring vault = temp_path(L".vault");
    DeleteFileW(vault.c_str());
    const char* pass = "test-passphrase-for-env-suite-99";
    env_expect(Keyring::create(vault, pass) == KeyringStatus::Ok, "create vault");
    Keyring kr;
    env_expect(kr.open(vault) == KeyringStatus::Ok, "open vault");
    env_expect(kr.unlock(pass) == KeyringStatus::Ok, "unlock vault");
    env_expect(kr.add_secret("scylla_TEST_TOKEN", "secret-value-xyz", "test", SecretScope::Global, "") ==
                   KeyringStatus::Ok,
               "add secret");

    ProjectEnvironmentVariable prot;
    prot.name = "API_TOKEN";
    prot.kind = EnvVarKind::SecretRef;
    prot.secret_id = kr.list_refs().front().name;
    env_expect(mgr.add_variable(pid, eid, prot), "add secret ref");

    auto human = mgr.resolve_for_terminal(pid, eid, kr, false);
    env_expect(human.status == EnvResolveStatus::Ok, "human resolve ok");
    env_expect(human.includes_protected, "human includes protected");
    env_expect(human.entries.size() >= 2, "human entries");

    auto agent = mgr.resolve_for_terminal(pid, eid, kr, true);
    env_expect(agent.status == EnvResolveStatus::Ok, "agent resolve ok");
    bool agent_has_secret = false;
    for (const auto& e : agent.entries) {
        if (e.from_secret) {
            agent_has_secret = true;
        }
    }
    env_expect(!agent_has_secret, "agent has no protected secrets");

    const std::wstring block = ProjectEnvironmentManager::merge_into_process_env(human.entries);
    env_expect(block.size() > 4 && block.back() == L'\0', "env block double-null terminated shape");

    const std::string eid2 = mgr.add_environment(pid, "Staging", "");
    if (auto* e2 = mgr.find_environment_mut(pid, eid2)) {
        e2->inherits_from_id = eid;
    }
    if (auto* e1 = mgr.find_environment_mut(pid, eid)) {
        e1->inherits_from_id = eid2;
    }
    auto cyc = mgr.resolve_for_terminal(pid, eid, kr, false);
    env_expect(cyc.status == EnvResolveStatus::Cycle, "detect inheritance cycle");

    kr.lock();
    DeleteFileW(env_path.c_str());
    DeleteFileW(vault.c_str());
    return g_env_fail;
}
