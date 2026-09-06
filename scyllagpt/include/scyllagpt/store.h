#pragma once

#include "scyllagpt/json.h"

#include <string>
#include <vector>

namespace scyllagpt {

std::string make_uuid();
std::wstring canonicalize_path(const std::wstring& path);
std::wstring file_identity(const std::wstring& path);
std::uint64_t fnv1a64(const void* data, std::size_t n);

struct Project {
    std::string id;
    std::wstring name;
    std::wstring root;
    std::wstring identity;
    std::string last_thread_id;
    int files_w = 220;
    int agent_w = 400;
    int history_w = 232;
};

struct Conversation {
    std::string id;
    std::string project_id;
    std::string account;
    std::string backend = "codex-app-server";
    std::string provider_id = "openai";  // openai | claude (Phase 1: chat still Codex-only)
    std::string thread_id;
    std::string title;
    std::string preview;
    bool pinned = false;
    bool archived = false;
    bool resumable = true;
};

struct ContextChip {
    std::string id;
    std::string kind;  // file | selection | buffer | image
    std::wstring path;
    std::string label;
    std::string body;
    bool unsaved = false;
    int line0 = 0;
    int line1 = 0;
};

struct WorkspaceStore {
    std::vector<Project> projects;
    std::vector<Conversation> conversations;
    std::string active_project_id;
    bool history_all_projects = false;

    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    Project* open_or_create(const std::wstring& folder);
    Project* by_id(const std::string& id);
    const Project* by_id(const std::string& id) const;
    Project* active();

    Conversation* upsert_thread(const std::string& project_id, const std::string& account, const std::string& thread_id,
                                 const std::string& title, const std::string& preview);
    Conversation* by_thread(const std::string& thread_id);
    const Conversation* by_thread(const std::string& thread_id) const;
    bool remove_thread(const std::string& thread_id);
    std::vector<Conversation*> list_visible(const std::string& account, const std::wstring& search);
};

std::string snapshot_context(const std::vector<ContextChip>& chips);

}  // namespace scyllagpt
