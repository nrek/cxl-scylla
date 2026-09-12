#pragma once
#include "scyllagpt/json.h"
#include <string>

namespace scyllagpt {
inline bool valid_chat_mode(const std::string& mode) {
    return mode == "execute" || mode == "plan" || mode == "ask";
}
inline const char* chat_mode_instructions(const std::string& mode) {
    if (mode == "plan") return
        "Plan mode: inspect project and Knowledge files read-only. Do not edit files or run mutating commands. "
        "Develop a concrete implementation plan with scope, steps, validation and open questions. "
        "Return the complete plan as Markdown in your final response; Scylla saves it to project Knowledge. "
        "Do not implement the plan. Do not use external tools that change state.\n";
    if (mode == "ask") return
        "Ask mode: discover and explain information in project and Knowledge files read-only. "
        "Do not edit files, run mutating commands, create plans, or use external tools that change state. "
        "Respond with findings and relevant file references.\n";
    return "Execute mode: carry out the requested work and validate the result.\n";
}
inline void apply_chat_mode_policy(Json& params, const std::string& mode) {
    if (mode == "execute") return;
    params["approvalPolicy"] = Json::string("never");
    Json sandbox = Json::object();
    sandbox["type"] = Json::string("readOnly");
    sandbox["networkAccess"] = Json::boolean(false);
    params["sandboxPolicy"] = std::move(sandbox);
}
}
