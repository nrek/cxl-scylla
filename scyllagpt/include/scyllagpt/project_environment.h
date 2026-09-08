#pragma once

// Project Environment Manager — plain + secret-ref variables (App Keyring Phase 2).
// Protected values are references only; resolution requires an unlocked Keyring.

#include "scyllagpt/keyring.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

enum class EnvVarKind {
    Plain = 0,
    SecretRef = 1,
};

struct EnvVarAvailability {
    bool human_terminal = true;
    bool agent_terminal = false;  // protected defaults OFF; plain may enable
    bool approved_recipes = true;
};

struct ProjectEnvironmentVariable {
    std::string name;
    EnvVarKind kind = EnvVarKind::Plain;
    std::string plain_value;   // when Plain
    std::string secret_id;     // when SecretRef (Keyring secret id or reference name)
    EnvVarAvailability availability;
};

struct ProjectEnvironment {
    std::string id;
    std::string project_id;
    std::string name;
    std::string description;
    std::string inherits_from_id;  // empty = none; cycle detection on resolve
    std::vector<ProjectEnvironmentVariable> variables;
};

struct ProjectEnvironmentState {
    std::string project_id;
    std::string active_environment_id;  // empty = None
    std::vector<ProjectEnvironment> environments;
};

enum class EnvResolveStatus {
    Ok = 0,
    NotFound,
    KeyringLocked,
    MissingSecret,
    Cycle,
    DuplicateName,
    InvalidName,
    IoError,
};

struct ResolvedEnvEntry {
    std::string name;
    std::wstring value;  // plaintext for injection only
    bool from_secret = false;
};

struct EnvResolveResult {
    EnvResolveStatus status = EnvResolveStatus::Ok;
    std::vector<ResolvedEnvEntry> entries;
    std::string message;  // safe diagnostics, never secret values
    bool includes_protected = false;
};

const char* env_resolve_status_string(EnvResolveStatus s);

class ProjectEnvironmentManager {
public:
    ProjectEnvironmentManager() = default;

    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    ProjectEnvironmentState* state_for(std::string_view project_id);
    const ProjectEnvironmentState* state_for(std::string_view project_id) const;

    // Ensure a state row exists for project_id (empty environments).
    ProjectEnvironmentState& ensure_project(std::string_view project_id);

    std::string add_environment(std::string_view project_id, std::string_view name,
                                std::string_view description = {});
    bool rename_environment(std::string_view project_id, std::string_view env_id,
                            std::string_view name);
    bool delete_environment(std::string_view project_id, std::string_view env_id);
    bool duplicate_environment(std::string_view project_id, std::string_view env_id,
                               std::string_view new_name);

    bool set_active(std::string_view project_id, std::string_view env_id);  // empty = None
    std::string active_environment_id(std::string_view project_id) const;
    const ProjectEnvironment* find_environment(std::string_view project_id,
                                               std::string_view env_id) const;
    ProjectEnvironment* find_environment_mut(std::string_view project_id, std::string_view env_id);

    bool add_variable(std::string_view project_id, std::string_view env_id,
                      ProjectEnvironmentVariable var);
    bool update_variable(std::string_view project_id, std::string_view env_id,
                         std::string_view name, const ProjectEnvironmentVariable& var);
    bool remove_variable(std::string_view project_id, std::string_view env_id,
                         std::string_view name);

    // Resolve for human terminal: plain + protected (if Keyring unlocked).
    // agent_terminal=true: plain only (Strict — never protected secrets).
    EnvResolveResult resolve_for_terminal(std::string_view project_id, std::string_view env_id,
                                          Keyring& keyring, bool agent_terminal) const;

    // Merge resolved entries into a full Unicode environment block (for CreateProcess).
    static std::wstring merge_into_process_env(const std::vector<ResolvedEnvEntry>& entries);

    static bool is_valid_var_name(std::string_view name);
    static std::string make_environment_id();

    const std::vector<ProjectEnvironmentState>& all() const { return projects_; }

private:
    EnvResolveResult resolve_chain(std::string_view project_id, std::string_view env_id, Keyring& keyring,
                                   bool agent_terminal, std::vector<std::string>& stack) const;

    std::vector<ProjectEnvironmentState> projects_;
};

}  // namespace scyllagpt
