#include "scyllagpt/project_environment.h"

#include "scyllagpt/json.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/utf.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>

#pragma comment(lib, "rpcrt4.lib")

namespace scyllagpt {
namespace {

bool read_all(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

bool write_all(const std::wstring& path, const std::string& body) {
    const std::wstring parent = path.substr(0, path.find_last_of(L"\\/"));
    if (!parent.empty()) {
        ensure_dir(parent);
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == body.size();
}

Json avail_to_json(const EnvVarAvailability& a) {
    Json j = Json::object();
    j["humanTerminal"] = Json::boolean(a.human_terminal);
    j["agentTerminal"] = Json::boolean(a.agent_terminal);
    j["approvedRecipes"] = Json::boolean(a.approved_recipes);
    return j;
}

EnvVarAvailability avail_from_json(const Json& j) {
    EnvVarAvailability a;
    if (j.is_object()) {
        a.human_terminal = j.at("humanTerminal").as_bool(true);
        a.agent_terminal = j.at("agentTerminal").as_bool(false);
        a.approved_recipes = j.at("approvedRecipes").as_bool(true);
    }
    return a;
}

Json var_to_json(const ProjectEnvironmentVariable& v) {
    Json j = Json::object();
    j["name"] = Json::string(v.name);
    j["type"] = Json::string(v.kind == EnvVarKind::SecretRef ? "secret-ref" : "plain");
    if (v.kind == EnvVarKind::Plain) {
        j["value"] = Json::string(v.plain_value);
    } else {
        j["secretId"] = Json::string(v.secret_id);
    }
    j["availability"] = avail_to_json(v.availability);
    return j;
}

ProjectEnvironmentVariable var_from_json(const Json& j) {
    ProjectEnvironmentVariable v;
    v.name = j.at("name").as_string("");
    const std::string typ = j.at("type").as_string("plain");
    if (typ == "secret-ref") {
        v.kind = EnvVarKind::SecretRef;
        v.secret_id = j.at("secretId").as_string("");
    } else {
        v.kind = EnvVarKind::Plain;
        v.plain_value = j.at("value").as_string("");
    }
    v.availability = avail_from_json(j.at("availability"));
    return v;
}

Json env_to_json(const ProjectEnvironment& e) {
    Json j = Json::object();
    j["id"] = Json::string(e.id);
    j["projectId"] = Json::string(e.project_id);
    j["name"] = Json::string(e.name);
    j["description"] = Json::string(e.description);
    j["inheritsFromId"] = Json::string(e.inherits_from_id);
    Json vars = Json::array();
    for (const auto& v : e.variables) {
        vars.push(var_to_json(v));
    }
    j["variables"] = std::move(vars);
    return j;
}

ProjectEnvironment env_from_json(const Json& j) {
    ProjectEnvironment e;
    e.id = j.at("id").as_string("");
    e.project_id = j.at("projectId").as_string("");
    e.name = j.at("name").as_string("");
    e.description = j.at("description").as_string("");
    e.inherits_from_id = j.at("inheritsFromId").as_string("");
    const Json& vars = j.at("variables");
    if (vars.is_array()) {
        for (const auto& item : vars.array_items()) {
            auto v = var_from_json(item);
            if (!v.name.empty()) {
                e.variables.push_back(std::move(v));
            }
        }
    }
    return e;
}

}  // namespace

const char* env_resolve_status_string(EnvResolveStatus s) {
    switch (s) {
        case EnvResolveStatus::Ok:
            return "ok";
        case EnvResolveStatus::NotFound:
            return "not_found";
        case EnvResolveStatus::KeyringLocked:
            return "keyring_locked";
        case EnvResolveStatus::MissingSecret:
            return "missing_secret";
        case EnvResolveStatus::Cycle:
            return "cycle";
        case EnvResolveStatus::DuplicateName:
            return "duplicate_name";
        case EnvResolveStatus::InvalidName:
            return "invalid_name";
        case EnvResolveStatus::IoError:
            return "io_error";
    }
    return "unknown";
}

bool ProjectEnvironmentManager::is_valid_var_name(std::string_view name) {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string ProjectEnvironmentManager::make_environment_id() {
    UUID u{};
    UuidCreate(&u);
    RPC_CSTR str = nullptr;
    if (UuidToStringA(&u, &str) != RPC_S_OK || !str) {
        return "env-" + std::to_string(GetTickCount64());
    }
    std::string out(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
    return out;
}

bool ProjectEnvironmentManager::load(const std::wstring& path) {
    projects_.clear();
    if (!file_exists(path)) {
        return true;
    }
    std::string raw;
    if (!read_all(path, raw)) {
        return false;
    }
    std::string err;
    Json root = Json::parse(raw, &err);
    if (!root.is_object()) {
        return false;
    }
    const Json& arr = root.at("projects");
    if (!arr.is_array()) {
        return true;
    }
    for (const auto& pj : arr.array_items()) {
        ProjectEnvironmentState st;
        st.project_id = pj.at("projectId").as_string("");
        st.active_environment_id = pj.at("activeEnvironmentId").as_string("");
        const Json& envs = pj.at("environments");
        if (envs.is_array()) {
            for (const auto& ej : envs.array_items()) {
                auto e = env_from_json(ej);
                if (!e.id.empty()) {
                    st.environments.push_back(std::move(e));
                }
            }
        }
        if (!st.project_id.empty()) {
            projects_.push_back(std::move(st));
        }
    }
    return true;
}

bool ProjectEnvironmentManager::save(const std::wstring& path) const {
    Json arr = Json::array();
    for (const auto& st : projects_) {
        Json pj = Json::object();
        pj["projectId"] = Json::string(st.project_id);
        pj["activeEnvironmentId"] = Json::string(st.active_environment_id);
        Json envs = Json::array();
        for (const auto& e : st.environments) {
            envs.push(env_to_json(e));
        }
        pj["environments"] = std::move(envs);
        arr.push(std::move(pj));
    }
    Json root = Json::object();
    root["version"] = Json::number(1);
    root["projects"] = std::move(arr);
    return write_all(path, root.dump());
}

ProjectEnvironmentState* ProjectEnvironmentManager::state_for(std::string_view project_id) {
    for (auto& st : projects_) {
        if (st.project_id == project_id) {
            return &st;
        }
    }
    return nullptr;
}

const ProjectEnvironmentState* ProjectEnvironmentManager::state_for(std::string_view project_id) const {
    for (const auto& st : projects_) {
        if (st.project_id == project_id) {
            return &st;
        }
    }
    return nullptr;
}

ProjectEnvironmentState& ProjectEnvironmentManager::ensure_project(std::string_view project_id) {
    if (auto* st = state_for(project_id)) {
        return *st;
    }
    ProjectEnvironmentState st;
    st.project_id.assign(project_id.data(), project_id.size());
    projects_.push_back(std::move(st));
    return projects_.back();
}

std::string ProjectEnvironmentManager::add_environment(std::string_view project_id, std::string_view name,
                                                       std::string_view description) {
    if (project_id.empty() || name.empty()) {
        return {};
    }
    auto& st = ensure_project(project_id);
    ProjectEnvironment e;
    e.id = make_environment_id();
    e.project_id = st.project_id;
    e.name.assign(name.data(), name.size());
    e.description.assign(description.data(), description.size());
    st.environments.push_back(std::move(e));
    if (st.active_environment_id.empty()) {
        st.active_environment_id = st.environments.back().id;
    }
    return st.environments.back().id;
}

bool ProjectEnvironmentManager::rename_environment(std::string_view project_id, std::string_view env_id,
                                                   std::string_view name) {
    auto* e = find_environment_mut(project_id, env_id);
    if (!e || name.empty()) {
        return false;
    }
    e->name.assign(name.data(), name.size());
    return true;
}

bool ProjectEnvironmentManager::delete_environment(std::string_view project_id, std::string_view env_id) {
    auto* st = state_for(project_id);
    if (!st) {
        return false;
    }
    auto it = std::find_if(st->environments.begin(), st->environments.end(),
                           [&](const ProjectEnvironment& e) { return e.id == env_id; });
    if (it == st->environments.end()) {
        return false;
    }
    st->environments.erase(it);
    if (st->active_environment_id == env_id) {
        st->active_environment_id = st->environments.empty() ? "" : st->environments.front().id;
    }
    for (auto& e : st->environments) {
        if (e.inherits_from_id == env_id) {
            e.inherits_from_id.clear();
        }
    }
    return true;
}

bool ProjectEnvironmentManager::duplicate_environment(std::string_view project_id, std::string_view env_id,
                                                      std::string_view new_name) {
    const auto* src = find_environment(project_id, env_id);
    if (!src) {
        return false;
    }
    auto& st = ensure_project(project_id);
    ProjectEnvironment copy = *src;
    copy.id = make_environment_id();
    copy.name = new_name.empty() ? (src->name + " Copy") : std::string(new_name);
    // Secret refs copy as references, not values (already refs).
    st.environments.push_back(std::move(copy));
    return true;
}

bool ProjectEnvironmentManager::set_active(std::string_view project_id, std::string_view env_id) {
    auto& st = ensure_project(project_id);
    if (env_id.empty()) {
        st.active_environment_id.clear();
        return true;
    }
    if (!find_environment(project_id, env_id)) {
        return false;
    }
    st.active_environment_id.assign(env_id.data(), env_id.size());
    return true;
}

std::string ProjectEnvironmentManager::active_environment_id(std::string_view project_id) const {
    const auto* st = state_for(project_id);
    return st ? st->active_environment_id : std::string{};
}

const ProjectEnvironment* ProjectEnvironmentManager::find_environment(std::string_view project_id,
                                                                      std::string_view env_id) const {
    const auto* st = state_for(project_id);
    if (!st) {
        return nullptr;
    }
    for (const auto& e : st->environments) {
        if (e.id == env_id) {
            return &e;
        }
    }
    return nullptr;
}

ProjectEnvironment* ProjectEnvironmentManager::find_environment_mut(std::string_view project_id,
                                                                    std::string_view env_id) {
    auto* st = state_for(project_id);
    if (!st) {
        return nullptr;
    }
    for (auto& e : st->environments) {
        if (e.id == env_id) {
            return &e;
        }
    }
    return nullptr;
}

bool ProjectEnvironmentManager::add_variable(std::string_view project_id, std::string_view env_id,
                                             ProjectEnvironmentVariable var) {
    if (!is_valid_var_name(var.name)) {
        return false;
    }
    auto* e = find_environment_mut(project_id, env_id);
    if (!e) {
        return false;
    }
    for (const auto& existing : e->variables) {
        if (existing.name == var.name) {
            return false;
        }
    }
    if (var.kind == EnvVarKind::SecretRef) {
        var.availability.agent_terminal = false;  // Strict default
    }
    e->variables.push_back(std::move(var));
    return true;
}

bool ProjectEnvironmentManager::update_variable(std::string_view project_id, std::string_view env_id,
                                                std::string_view name,
                                                const ProjectEnvironmentVariable& var) {
    auto* e = find_environment_mut(project_id, env_id);
    if (!e) {
        return false;
    }
    for (auto& existing : e->variables) {
        if (existing.name == name) {
            existing = var;
            existing.name.assign(name.data(), name.size());
            return true;
        }
    }
    return false;
}

bool ProjectEnvironmentManager::remove_variable(std::string_view project_id, std::string_view env_id,
                                                std::string_view name) {
    auto* e = find_environment_mut(project_id, env_id);
    if (!e) {
        return false;
    }
    auto it = std::find_if(e->variables.begin(), e->variables.end(),
                           [&](const ProjectEnvironmentVariable& v) { return v.name == name; });
    if (it == e->variables.end()) {
        return false;
    }
    e->variables.erase(it);
    return true;
}

EnvResolveResult ProjectEnvironmentManager::resolve_chain(std::string_view project_id,
                                                          std::string_view env_id, Keyring& keyring,
                                                          bool agent_terminal,
                                                          std::vector<std::string>& stack) const {
    EnvResolveResult out;
    if (env_id.empty()) {
        return out;
    }
    for (const auto& s : stack) {
        if (s == env_id) {
            out.status = EnvResolveStatus::Cycle;
            out.message = "environment inheritance cycle";
            return out;
        }
    }
    const auto* env = find_environment(project_id, env_id);
    if (!env) {
        out.status = EnvResolveStatus::NotFound;
        out.message = "environment not found";
        return out;
    }
    stack.push_back(std::string(env_id));
    if (!env->inherits_from_id.empty()) {
        out = resolve_chain(project_id, env->inherits_from_id, keyring, agent_terminal, stack);
        if (out.status != EnvResolveStatus::Ok) {
            return out;
        }
    }
    stack.pop_back();

    auto upsert = [&](ResolvedEnvEntry entry) {
        for (auto& existing : out.entries) {
            if (existing.name == entry.name) {
                existing = std::move(entry);
                return;
            }
        }
        out.entries.push_back(std::move(entry));
    };

    for (const auto& v : env->variables) {
        if (agent_terminal) {
            if (!v.availability.agent_terminal) {
                continue;
            }
            if (v.kind == EnvVarKind::SecretRef) {
                // Strict: never inject protected into agent terminals.
                continue;
            }
        } else {
            if (!v.availability.human_terminal) {
                continue;
            }
        }

        if (v.kind == EnvVarKind::Plain) {
            ResolvedEnvEntry e;
            e.name = v.name;
            e.value = utf16(v.plain_value);
            e.from_secret = false;
            upsert(std::move(e));
            continue;
        }

        // SecretRef
        if (!keyring.is_unlocked()) {
            out.status = EnvResolveStatus::KeyringLocked;
            out.message = "Keyring locked; protected variable " + v.name + " requires unlock";
            return out;
        }
        const auto refs = keyring.list_refs();
        const SecretRef* match = nullptr;
        for (const auto& r : refs) {
            if (r.id == v.secret_id || r.name == v.secret_id) {
                match = &r;
                break;
            }
        }
        if (!match) {
            out.status = EnvResolveStatus::MissingSecret;
            out.message = "missing secret reference for " + v.name;
            return out;
        }
        // Resolve value via authorize + build env (no get_secret_value).
        const std::string op = std::string("env_resolve:") + v.name;
        if (keyring.authorize_use(match->name, op) != KeyringStatus::Ok) {
            out.status = EnvResolveStatus::MissingSecret;
            out.message = "cannot authorize secret for " + v.name;
            return out;
        }
        std::wstring frag;
        if (keyring.build_authorized_env_block(op, frag) != KeyringStatus::Ok) {
            out.status = EnvResolveStatus::MissingSecret;
            out.message = "cannot resolve secret for " + v.name;
            return out;
        }
        // frag is NAME=value\0
        std::wstring value;
        const size_t eq = frag.find(L'=');
        if (eq != std::wstring::npos) {
            size_t end = frag.find(L'\0', eq + 1);
            if (end == std::wstring::npos) {
                end = frag.size();
            }
            value = frag.substr(eq + 1, end - (eq + 1));
        }
        if (!frag.empty()) {
            SecureZeroMemory(frag.data(), frag.size() * sizeof(wchar_t));
        }
        ResolvedEnvEntry e;
        e.name = v.name;  // env var name, not secret name
        e.value = std::move(value);
        e.from_secret = true;
        out.includes_protected = true;
        upsert(std::move(e));
    }
    return out;
}

EnvResolveResult ProjectEnvironmentManager::resolve_for_terminal(std::string_view project_id,
                                                                 std::string_view env_id,
                                                                 Keyring& keyring,
                                                                 bool agent_terminal) const {
    std::vector<std::string> stack;
    return resolve_chain(project_id, env_id, keyring, agent_terminal, stack);
}

std::wstring ProjectEnvironmentManager::merge_into_process_env(
    const std::vector<ResolvedEnvEntry>& entries) {
    LPWCH env = GetEnvironmentStringsW();
    std::wstring out;
    if (env) {
        for (LPWCH p = env; *p;) {
            const std::wstring entry(p);
            const size_t eq = entry.find(L'=');
            const std::wstring name = eq == std::wstring::npos ? entry : entry.substr(0, eq);
            bool override = false;
            for (const auto& e : entries) {
                if (utf16(e.name) == name) {
                    override = true;
                    break;
                }
            }
            if (!override) {
                out += entry;
                out.push_back(L'\0');
            }
            p += entry.size() + 1;
        }
        FreeEnvironmentStringsW(env);
    }
    for (const auto& e : entries) {
        out += utf16(e.name);
        out.push_back(L'=');
        out += e.value;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

}  // namespace scyllagpt
