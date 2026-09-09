#pragma once

#include "scyllagpt/json.h"

#include <string>

namespace scyllagpt {

// Last Turn in thread.turns with status == "inProgress" (Codex TurnStatus).
inline std::string latest_in_progress_turn_id(const Json& thread) {
    std::string found;
    const Json& turns = thread.at("turns");
    if (!turns.is_array()) {
        return found;
    }
    for (const auto& turn : turns.array_items()) {
        if (turn.at("status").as_string() == "inProgress") {
            const std::string id = turn.at("id").as_string();
            if (!id.empty()) {
                found = id;
            }
        }
    }
    return found;
}

}  // namespace scyllagpt
