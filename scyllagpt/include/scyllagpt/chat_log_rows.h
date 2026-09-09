#pragma once

// Row reconciliation for the chat log. Kept free of Win32 so the reuse/create/destroy decision is
// unit-testable: rebuilding every message window on each streaming delta was the cause of the
// flicker, input lag, and scroll jank during agent turns.

#include <cstddef>
#include <string>
#include <vector>

namespace scyllagpt {

enum class ChatRowAction {
    Reuse,   // body window already renders this text
    Retext,  // body window exists but content changed
    Create,  // no body window yet
};

struct ChatLogRowDiff {
    std::vector<ChatRowAction> rows;  // one entry per incoming message
    std::size_t destroy_from = 0;     // destroy existing bodies at index >= destroy_from
};

inline ChatLogRowDiff chat_log_plan_rows(const std::vector<std::wstring>& existing,
                                         const std::vector<std::wstring>& next) {
    ChatLogRowDiff diff;
    diff.rows.reserve(next.size());
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (i >= existing.size()) {
            diff.rows.push_back(ChatRowAction::Create);
        } else if (existing[i] == next[i]) {
            diff.rows.push_back(ChatRowAction::Reuse);
        } else {
            diff.rows.push_back(ChatRowAction::Retext);
        }
    }
    diff.destroy_from = next.size();
    return diff;
}

}  // namespace scyllagpt
