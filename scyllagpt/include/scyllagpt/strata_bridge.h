#pragma once

#include "scyllagpt/json.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

class KnowledgeStore;
// Existing standard Strata workspaces under readable Knowledge folders, in source
// order and breadth-first within each source. Does not open or create databases.
std::vector<std::wstring> discover_strata_workspaces(const KnowledgeStore& knowledge,
                                                    const std::string& project_id);
std::wstring resolve_strata_workspace(const KnowledgeStore& knowledge, const std::string& project_id,
                                     const std::wstring& project_root);

enum class StrataBridgeState {
    NotInstalled,
    Stopped,
    Running,
    Error,
};

struct StrataBridgeHit {
    std::string path;
    std::string kind;
    std::string project;
    std::string title;
    std::string snippet;
    std::string updated_at;
    std::string origin;
    std::string sync_status;
};

struct StrataBridgeStatus {
    bool ok = false;
    bool index_exists = false;
    std::int64_t total = 0;
    int bridge_version = 0;
    std::string strata_version;
    std::string workspace_root;
    std::string index_path;
    std::string message;
    Json raw;
};

struct StrataBridgeCapabilities {
    bool ok = false;
    int bridge_version = 0;
    std::string strata_version;
    std::vector<std::string> methods;
    std::string message;
    Json raw;
};

struct StrataBridgeSearchResult {
    bool ok = false;
    std::string query;
    std::string message;
    std::vector<StrataBridgeHit> hits;
    Json raw;
};

struct StrataBridgeRecentResult {
    bool ok = false;
    std::string message;
    std::vector<StrataBridgeHit> items;
    Json raw;
};

struct StrataBridgeDocument {
    bool ok = false;
    std::string path;
    std::string kind;
    std::string project;
    std::string title;
    std::string updated_at;
    std::string origin;
    std::string sync_status;
    std::string storage;
    std::string body;  // may be capped by the bridge
    bool body_truncated = false;
    std::string message;
    Json raw;
};

struct StrataBridgePending {
    bool ok = false;
    bool available = false;
    std::int64_t index_pending = 0;
    std::int64_t sync_pending = 0;
    std::int64_t total = 0;
    std::string message;
    Json raw;
};

// Parse helpers — unit-testable without a live Python process.
bool parse_bridge_envelope(const std::string& line, bool* ok, Json* result, std::string* error,
                           std::int64_t* id = nullptr);
bool parse_bridge_status_result(const Json& result, StrataBridgeStatus* out);
bool parse_bridge_capabilities_result(const Json& result, StrataBridgeCapabilities* out);
bool parse_bridge_search_result(const Json& result, StrataBridgeSearchResult* out);
bool parse_bridge_recent_result(const Json& result, StrataBridgeRecentResult* out);
bool parse_bridge_get_result(const Json& result, StrataBridgeDocument* out);
bool parse_bridge_pending_result(const Json& result, StrataBridgePending* out);

// Flatten a document body into one short line safe to show in chrome: secret-looking
// values are redacted before truncation, so a partial cut can never leak a tail.
std::string strata_sanitize_preview(const std::string& body, std::size_t max_chars = 220);

// Discover how to launch the bridge (fail soft when missing).
struct StrataBridgeLaunch {
    bool found = false;
    std::wstring exe;           // python.exe or strata.exe
    std::wstring args;          // remainder (quoted) after exe
    std::string discovery_note; // human-readable reason
};

StrataBridgeLaunch discover_strata_bridge_launch();

// Long-lived stdio JSON-lines client. Job Object kills children on destroy.
class StrataBridge {
public:
    StrataBridge() = default;
    ~StrataBridge();

    StrataBridge(const StrataBridge&) = delete;
    StrataBridge& operator=(const StrataBridge&) = delete;

    bool start(std::wstring* error = nullptr);
    void stop();
    bool running() const;
    StrataBridgeState state() const { return state_; }
    const std::string& last_error() const { return last_error_; }

    StrataBridgeCapabilities capabilities(DWORD timeout_ms = 5000);
    // Invoke the packaged library extension. Call on a worker, not the window thread.
    Json library(Json params, std::string* error, DWORD timeout_ms = 120000);
    StrataBridgeStatus status(const std::string& workspace_root = {}, DWORD timeout_ms = 8000);
    StrataBridgeSearchResult search(const std::string& query, int limit = 15,
                                    const std::string& project = {},
                                    const std::string& workspace_root = {}, DWORD timeout_ms = 12000);
    StrataBridgeRecentResult recent(const std::string& project = {}, int hours = 48, int limit = 10,
                                    const std::string& workspace_root = {}, DWORD timeout_ms = 12000);
    StrataBridgeDocument get(const std::string& path, const std::string& workspace_root = {},
                             DWORD timeout_ms = 12000);
    StrataBridgePending pending_counts(const std::string& project = {}, const std::string& workspace_root = {},
                                       DWORD timeout_ms = 20000);

    // Optional workspace root passed on each call (params.workspace_root).
    void set_default_workspace(std::wstring path) { default_workspace_ = std::move(path); }
    const std::wstring& default_workspace() const { return default_workspace_; }

private:
    bool ensure_started(std::wstring* error);
    bool write_line(const std::string& line);
    bool read_response_for(std::int64_t id, Json* out, std::string* error, DWORD timeout_ms);
    Json request(const char* method, Json params, DWORD timeout_ms, std::string* error);

    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE stdin_wr_ = nullptr;
    HANDLE stdout_rd_ = nullptr;
    DWORD pid_ = 0;
    std::int64_t next_id_ = 1;
    StrataBridgeState state_ = StrataBridgeState::NotInstalled;
    std::string last_error_;
    std::wstring default_workspace_;
    JsonlDecoder decoder_;
};

const wchar_t* strata_bridge_state_label(StrataBridgeState state);

}  // namespace scyllagpt
