#pragma once

// Composer slash skill: /scylla-query — Keyring-aware SQL handoff instructions.
// Agents never receive secret values; only names and workflow text.

#include "scyllagpt/keyring.h"
#include "scyllagpt/project_environment.h"

#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

// If *text begins with /scylla-query (case-insensitive), strip the token + following
// whitespace and return true. Non-matches leave *text unchanged.
inline bool consume_scylla_query_slash(std::wstring* text) {
    if (!text || text->empty()) {
        return false;
    }
    std::wstring& s = *text;
    std::size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) {
        ++i;
    }
    static constexpr wchar_t kCmd[] = L"/scylla-query";
    constexpr std::size_t kLen = 13;  // strlen("/scylla-query")
    if (s.size() < i + kLen) {
        return false;
    }
    for (std::size_t j = 0; j < kLen; ++j) {
        wchar_t c = s[i + j];
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
        if (c != kCmd[j]) {
            return false;
        }
    }
    i += kLen;
    if (i < s.size()) {
        const wchar_t next = s[i];
        if (next != L' ' && next != L'\t' && next != L'\r' && next != L'\n') {
            // e.g. /scylla-querying — not a match
            return false;
        }
        while (i < s.size() &&
               (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) {
            ++i;
        }
    }
    s.erase(0, i);
    return true;
}

// Metadata-only catalog for the skill preamble (never values).
inline std::string format_keyring_name_catalog(const Keyring* keyring,
                                               const ProjectEnvironmentManager* environments,
                                               std::string_view project_id) {
    std::string out;
    if (!keyring || !keyring->vault_bound() || !keyring->is_unlocked()) {
        out = "(Keyring locked or unbound — ask the user for exact Keyring secret names.)\n";
    } else {
        const auto refs = keyring->list_refs_for_ui(project_id);
        if (refs.empty()) {
            out = "(No Keyring secret names listed — ask the user for exact names.)\n";
        } else {
            out += "Keyring secret names (metadata only; values stay in Scylla):\n";
            for (const auto& r : refs) {
                out += "- ";
                out += r.name;
                if (r.scope == SecretScope::Project) {
                    out += " [project]";
                } else {
                    out += " [global]";
                }
                if (!r.description.empty()) {
                    out += " — ";
                    out += r.description;
                }
                out += "\n";
            }
        }
    }

    if (environments && !project_id.empty()) {
        if (const auto* st = environments->state_for(project_id)) {
            const std::string active = environments->active_environment_id(project_id);
            if (const auto* env = active.empty() ? nullptr : environments->find_environment(project_id, active)) {
                if (!env->variables.empty()) {
                    out += "Active project environment \"";
                    out += env->name;
                    out += "\" variable names:\n";
                    for (const auto& v : env->variables) {
                        out += "- ";
                        out += v.name;
                        out += (v.kind == EnvVarKind::SecretRef) ? " [SecretRef]\n" : " [Plain]\n";
                    }
                }
            }
        }
    }
    return out;
}

// `query_tool_available` must reflect whether the scylla_query tool is actually registered for this
// session. When it is false the agent is told plainly that Scylla cannot execute SQL, because the
// previous version of this skill asked for a fenced block that nothing consumed — which produced
// answers presented as completed queries when no query had run.
inline std::string scylla_query_instructions(const std::string& keyring_name_catalog,
                                            bool query_tool_available) {
    std::string s;
    s += "Scylla workflow: /scylla-query (brokered SQL). Scylla holds database and SSH credentials "
         "in its App Keyring and executes queries itself. You do NOT SSH, open DB sockets, or hold "
         "credentials.\n\n";
    s += "Hard rules:\n";
    s += "- Never ask the user to paste secret VALUES (passwords, private keys, tokens).\n";
    s += "- Tokens like !scylla_NAME in the user message are Keyring secret NAMES only (never values).\n";
    s += "- Never state or imply that a query ran unless you called the tool and received rows back.\n";
    s += "- Never infer, estimate, or fabricate a value that was supposed to come from the database. "
         "If you have no rows, say you have no rows.\n";
    s += "- Prefer read-only connections when the user indicates RO / reader / SELECT-only.\n\n";

    if (query_tool_available) {
        s += "To run a query, call the tool:\n";
        s += "  name: scylla_query\n";
        s += "  arguments: { \"connection_alias\": \"<saved alias>\", \"sql\": \"<one statement>\", "
             "\"operation_id\": \"<short tag>\" }\n\n";
        s += "The SAVED CONNECTION owns the credential mapping — it resolves the SSH and database "
             "roles to Keyring names internally. You supply only an alias and SQL. You cannot pass a "
             "host, username, password, key, or Keyring name to the tool, and you must not try.\n\n";
        s += "If no suitable alias exists, ask the user to create one in Settings → Security → "
             "Connections and stop. Do not guess an alias.\n\n";
    } else {
        s += "IMPORTANT: brokered execution is NOT available in this session. Scylla has no "
             "agent-callable query tool registered, so no SQL you produce can run.\n\n";
        s += "Therefore:\n";
        s += "- Do NOT emit a fenced scylla-query block or any other block that looks executable. "
             "Nothing consumes it.\n";
        s += "- Do NOT say the query was submitted, handed off, or is awaiting execution. It is not.\n";
        s += "- Do state plainly that Scylla cannot execute the query yet, then offer the SQL you "
             "would run and which connection it needs, so the user can run it themselves.\n\n";
    }

    s += "Keyring / environment NAMES available in this Scylla project (metadata only";
    s += query_tool_available ? "; useful when helping the user configure a connection" : "";
    s += "):\n";
    if (keyring_name_catalog.empty()) {
        s += "(no names listed.)\n\n";
    } else {
        s += keyring_name_catalog;
        if (s.back() != '\n') {
            s += '\n';
        }
        s += '\n';
    }
    s += "This skill applies to this message only.\n\n";
    return s;
}

}  // namespace scyllagpt
