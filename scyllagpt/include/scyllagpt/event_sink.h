#pragma once

#include <functional>
#include <string>

namespace scyllagpt {

// Shell-agnostic delivery of one JSON-RPC / runtime stdout line from a worker thread.
// Win32 adapters PostMessage; Fluent adapters marshal onto the UI dispatcher.
using LineSink = std::function<void(std::string line)>;

struct ClaudePrintResult {
    bool ok = false;
    std::string text;
    std::wstring error;
};

// Shell-agnostic delivery of a finished local Claude / HTTP provider turn.
using ClaudeDoneSink = std::function<void(ClaudePrintResult result)>;

}  // namespace scyllagpt
