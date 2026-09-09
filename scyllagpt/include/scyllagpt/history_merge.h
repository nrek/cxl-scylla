#pragma once

// Transcript reconciliation between a provider thread read and the locally recorded conversation.

#include <algorithm>
#include <vector>

namespace scyllagpt {

// Some provider thread reads return assistant turns without the user turns that prompted them. In
// that case the local record is authoritative for ordering, because it interleaves both roles in
// send order. Keep local order and append only the assistant messages local has not seen.
//
// The previous implementation compared *counts* and appended any surplus provider message to the
// end of the list, which placed messages after turns that actually came later.
template <typename Message>
std::vector<Message> merge_provider_agent_messages(std::vector<Message> local,
                                                   const std::vector<Message>& provider) {
    for (const auto& message : provider) {
        if (message.user) continue;
        const bool known = std::any_of(local.begin(), local.end(), [&](const Message& existing) {
            return !existing.user && existing.text == message.text;
        });
        if (!known) local.push_back(message);
    }
    return local;
}

}  // namespace scyllagpt
