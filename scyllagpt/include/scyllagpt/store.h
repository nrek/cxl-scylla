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
    // The primary root remains the conversation/terminal cwd. Additional roots
    // are peer repositories displayed and granted as part of this project.
    std::vector<std::wstring> roots;
    std::wstring identity;
    std::string last_thread_id;
    int files_w = 300;
    int agent_w = 650;
    int history_w = 300;
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
    std::int64_t updated_at = 0;
    bool title_manual = false;
    bool title_generated = false;
    Json local_messages; // Claude print-mode history; Codex history remains provider-owned.
    bool pinned = false;
    bool archived = false;
    bool deleted = false; // Local tombstone prevents provider history from reappearing.
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
    bool add_root(std::string_view project_id, const std::wstring& folder);
    bool remove_root(std::string_view project_id, const std::wstring& folder);
    Project* by_id(const std::string& id);
    const Project* by_id(const std::string& id) const;
    Project* active();
    const Project* active() const;
    std::vector<std::wstring> open_roots() const;
    bool contains_open_path(const std::wstring& path) const;
    bool contains_open_project(const std::string& project_id) const;

    Conversation* upsert_thread(const std::string& project_id, const std::string& account, const std::string& thread_id,
                                 const std::string& title, const std::string& preview);
    Conversation* by_thread(const std::string& thread_id);
    const Conversation* by_thread(const std::string& thread_id) const;
    bool remove_thread(const std::string& thread_id);
    std::vector<Conversation*> list_visible(const std::string& account, const std::wstring& search);
};

std::string snapshot_context(const std::vector<ContextChip>& chips);

}  // namespace scyllagpt
