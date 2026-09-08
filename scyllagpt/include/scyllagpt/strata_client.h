#pragma once

#include "scyllagpt/json.h"

#include <string>
#include <vector>

namespace scyllagpt {

// Explicit Scylla project → Strata org/client/project/repo (no folder-name inference).
struct StrataBinding {
    std::string project_id;       // Scylla project id
    std::string org;
    std::string client;
    std::string strata_project;   // Strata project slug/id
    std::string repo;
};

struct StrataSettings {
    std::wstring endpoint = L"http://127.0.0.1:8765";
    std::wstring bearer;  // optional Authorization: Bearer …
};

struct StrataHit {
    std::string id;
    std::string kind;
    std::string title;
    std::string path;
    std::string project;
    std::string source;
};

struct StrataHealth {
    bool ok = false;
    int http_status = 0;
    std::string message;
    Json body;  // raw /api/stats or /health payload when parseable
};

struct StrataSearchResult {
    bool ok = false;
    int http_status = 0;
    std::string message;
    std::vector<StrataHit> hits;
    Json raw;
};

// Native HTTP client for localhost / central Strata — not MCP.
class StrataClient {
public:
    StrataSettings& settings() { return settings_; }
    const StrataSettings& settings() const { return settings_; }

    std::vector<StrataBinding>& bindings() { return bindings_; }
    const std::vector<StrataBinding>& bindings() const { return bindings_; }

    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    StrataBinding* binding_for(const std::string& project_id);
    const StrataBinding* binding_for(const std::string& project_id) const;
    void upsert_binding(const StrataBinding& b);
    bool remove_binding(const std::string& project_id);

    // GET /api/stats, then GET /health. Fail soft when server is down.
    StrataHealth health_check() const;

    // POST /api/query with {"q": query}. Fail soft when server is down.
    StrataSearchResult search(const std::string& query, int limit = 50,
                              const std::string& project_filter = {}) const;

private:
    StrataSettings settings_;
    std::vector<StrataBinding> bindings_;
};

}  // namespace scyllagpt
