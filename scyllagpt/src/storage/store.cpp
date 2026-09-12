#include "scyllagpt/store.h"

#include "scyllagpt/file_io.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#pragma comment(lib, "rpcrt4.lib")

namespace scyllagpt {
namespace {

std::string read_all(const std::wstring& path) {
    return read_file_bytes(path);
}

bool write_all(const std::wstring& path, const std::string& body) {
    return write_file_bytes_atomic(path, body);
}

std::wstring lower_copy(std::wstring s) {
    CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

}  // namespace

std::uint64_t fnv1a64(const void* data, std::size_t n) {
    auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = 14695981039346656037ull;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string make_uuid() {
    UUID u{};
    UuidCreate(&u);
    RPC_CSTR str = nullptr;
    if (UuidToStringA(&u, &str) != RPC_S_OK || !str) {
        return {};
    }
    std::string out(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
    return out;
}

std::wstring canonicalize_path(const std::wstring& path) {
    wchar_t full[MAX_PATH * 4]{};
    if (!GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, nullptr)) {
        return path;
    }
    std::wstring s = full;
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) {
        s.pop_back();
    }
    return s;
}

std::wstring file_identity(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    if (!ok) {
        return {};
    }
    return std::to_wstring(info.dwVolumeSerialNumber) + L":" + std::to_wstring(info.nFileIndexHigh) + L":" +
           std::to_wstring(info.nFileIndexLow);
}

Project* WorkspaceStore::by_id(const std::string& id) {
    for (auto& p : projects) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const Project* WorkspaceStore::by_id(const std::string& id) const {
    for (const auto& p : projects) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

Project* WorkspaceStore::active() {
    return by_id(active_project_id);
}

const Project* WorkspaceStore::active() const {
    return by_id(active_project_id);
}

Project* WorkspaceStore::open_or_create(const std::wstring& folder) {
    const std::wstring root = canonicalize_path(folder);
    const std::wstring ident = file_identity(root);
    for (auto& p : projects) {
        if (!ident.empty() && p.identity == ident) {
            active_project_id = p.id;
            if (p.root != root) {
                p.root = root;
            }
            if (p.roots.empty()) p.roots.push_back(p.root);
            return &p;
        }
    }
    for (auto& p : projects) {
        if (_wcsicmp(canonicalize_path(p.root).c_str(), root.c_str()) == 0) {
            active_project_id = p.id;
            if (!ident.empty()) {
                p.identity = ident;
            }
            p.root = root;
            if (p.roots.empty()) p.roots.push_back(p.root);
            return &p;
        }
    }
    Project p;
    p.id = make_uuid();
    p.root = root;
    p.roots.push_back(root);
    p.identity = ident;
    p.name = root;
    const auto slash = root.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < root.size()) {
        p.name = root.substr(slash + 1);
    }
    projects.push_back(std::move(p));
    active_project_id = projects.back().id;
    return &projects.back();
}

bool WorkspaceStore::add_root(std::string_view project_id, const std::wstring& folder) {
    Project* project = by_id(std::string(project_id));
    if (!project) return false;
    const std::wstring root = canonicalize_path(folder);
    const DWORD attrs = GetFileAttributesW(root.c_str());
    if (root.empty() || attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (project->roots.empty() && !project->root.empty()) project->roots.push_back(project->root);
    const auto duplicate = std::find_if(project->roots.begin(), project->roots.end(), [&](const auto& existing) {
        return _wcsicmp(canonicalize_path(existing).c_str(), root.c_str()) == 0;
    });
    if (duplicate != project->roots.end()) return false;
    project->roots.push_back(root);
    return true;
}

bool WorkspaceStore::remove_root(std::string_view project_id, const std::wstring& folder) {
    Project* project = by_id(std::string(project_id));
    if (!project || _wcsicmp(canonicalize_path(folder).c_str(), canonicalize_path(project->root).c_str()) == 0)
        return false;
    const auto before = project->roots.size();
    project->roots.erase(std::remove_if(project->roots.begin(), project->roots.end(), [&](const auto& existing) {
        return _wcsicmp(canonicalize_path(existing).c_str(), canonicalize_path(folder).c_str()) == 0;
    }), project->roots.end());
    return project->roots.size() != before;
}

Conversation* WorkspaceStore::by_thread(const std::string& thread_id) {
    for (auto& c : conversations) {
        if (c.thread_id == thread_id) {
            return &c;
        }
    }
    return nullptr;
}

const Conversation* WorkspaceStore::by_thread(const std::string& thread_id) const {
    for (const auto& c : conversations) {
        if (c.thread_id == thread_id) {
            return &c;
        }
    }
    return nullptr;
}

bool WorkspaceStore::remove_thread(const std::string& thread_id) {
    for (auto it = conversations.begin(); it != conversations.end(); ++it) {
        if (it->thread_id == thread_id) {
            conversations.erase(it);
            return true;
        }
    }
    return false;
}

Conversation* WorkspaceStore::upsert_thread(const std::string& project_id, const std::string& account,
                                              const std::string& thread_id, const std::string& title,
                                              const std::string& preview) {
    if (thread_id.empty()) {
        return nullptr;
    }
    if (auto* existing = by_thread(thread_id)) {
        if (existing->deleted) return nullptr;
        if (!title.empty() && (existing->title.empty() || existing->title == thread_id)) {
            existing->title = title;
        }
        if (!preview.empty()) {
            existing->preview = preview;
        }
        if (existing->account.empty()) {
            existing->account = account;
        }
        return existing;
    }
    Conversation c;
    c.id = make_uuid();
    c.project_id = project_id;
    c.account = account;
    c.thread_id = thread_id;
    c.title = title.empty() ? thread_id : title;
    c.preview = preview;
    c.updated_at = std::time(nullptr);
    conversations.push_back(std::move(c));
    return &conversations.back();
}

std::vector<std::wstring> WorkspaceStore::open_roots() const {
    const auto* project = active();
    if (!project) return {};
    auto roots = project->roots;
    if (roots.empty() && !project->root.empty()) roots.push_back(project->root);
    return roots;
}

bool WorkspaceStore::contains_open_path(const std::wstring& path) const {
    if (path.empty()) return false;
    const auto candidate = lower_copy(canonicalize_path(path));
    for (const auto& root : open_roots()) {
        const auto normalized = lower_copy(canonicalize_path(root));
        if (candidate == normalized || candidate.starts_with(normalized + L"\\")) return true;
    }
    return false;
}

bool WorkspaceStore::contains_open_project(const std::string& project_id) const {
    const auto* project = by_id(project_id);
    return project && contains_open_path(project->root);
}

std::vector<Conversation*> WorkspaceStore::list_visible(const std::string& account, const std::wstring& search) {
    std::vector<Conversation*> out;
    std::wstring q = lower_copy(search);
    for (auto& c : conversations) {
        if (c.archived || c.deleted) {
            continue;
        }
        if (!account.empty() && !c.account.empty() && c.account != account) {
            continue;
        }
        if (!contains_open_project(c.project_id)) {
            continue;
        }
        if (!q.empty()) {
            std::wstring hay = utf16(c.title + " " + c.preview);
            CharLowerBuffW(hay.data(), static_cast<DWORD>(hay.size()));
            if (hay.find(q) == std::wstring::npos) {
                continue;
            }
        }
        out.push_back(&c);
    }
    std::stable_sort(out.begin(), out.end(), [](const Conversation* a, const Conversation* b) {
        if (a->pinned != b->pinned) {
            return a->pinned && !b->pinned;
        }
        if (a->updated_at != b->updated_at) return a->updated_at > b->updated_at;
        return a->thread_id < b->thread_id;
    });
    return out;
}

bool WorkspaceStore::load(const std::wstring& path) {
    projects.clear();
    conversations.clear();
    active_project_id.clear();
    const std::string raw = read_all(path);
    if (raw.empty()) {
        return true;
    }
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return false;
    }
    active_project_id = j.at("active_project_id").as_string("");
    history_all_projects = false;  // Always current-project chats only.
    const Json& pj = j.at("projects");
    if (pj.is_array()) {
        for (const auto& it : pj.array_items()) {
            Project p;
            p.id = it.at("id").as_string();
            p.name = utf16(it.at("name").as_string());
            p.root = utf16(it.at("root").as_string());
            const Json& roots = it.at("roots");
            if (roots.is_array()) for (const auto& root : roots.array_items()) {
                const std::wstring value = utf16(root.as_string(""));
                if (!value.empty()) p.roots.push_back(value);
            }
            if (p.roots.empty() && !p.root.empty()) p.roots.push_back(p.root);
            p.identity = utf16(it.at("identity").as_string());
            p.last_thread_id = it.at("last_thread_id").as_string("");
            p.files_w = static_cast<int>(it.at("files_w").as_int(300));
            p.agent_w = static_cast<int>(it.at("agent_w").as_int(650));
            p.history_w = static_cast<int>(it.at("history_w").as_int(300));
            if (!p.id.empty()) {
                projects.push_back(std::move(p));
            }
        }
    }
    const Json& cj = j.at("conversations");
    if (cj.is_array()) {
        for (const auto& it : cj.array_items()) {
            Conversation c;
            c.id = it.at("id").as_string();
            c.project_id = it.at("project_id").as_string();
            c.account = it.at("account").as_string();
            c.backend = it.at("backend").as_string("codex-app-server");
            c.provider_id = it.at("provider_id").as_string("openai");
            c.thread_id = it.at("thread_id").as_string();
            c.title = it.at("title").as_string();
            c.updated_at = it.at("updated_at").as_int(0);
            c.title_manual = it.at("title_manual").as_bool(!c.title.empty() && c.title != "New Chat");
            c.title_generated = it.at("title_generated").as_bool(false);
            c.local_messages = it.at("local_messages");
            c.preview = it.at("preview").as_string("");
            c.pinned = it.at("pinned").as_bool(false);
            c.archived = it.at("archived").as_bool(false);
            c.deleted = it.at("deleted").as_bool(false);
            c.resumable = it.at("resumable").as_bool(true);
            if (!c.id.empty()) {
                conversations.push_back(std::move(c));
            }
        }
    }
    return true;
}

bool WorkspaceStore::save(const std::wstring& path) const {
    Json j = Json::object();
    j["active_project_id"] = Json::string(active_project_id);
    j["history_all_projects"] = Json::boolean(history_all_projects);
    Json arr = Json::array();
    for (const auto& p : projects) {
        Json o = Json::object();
        o["id"] = Json::string(p.id);
        o["name"] = Json::string(utf8(p.name));
        o["root"] = Json::string(utf8(p.root));
        Json roots = Json::array();
        for (const auto& root : p.roots) roots.push(Json::string(utf8(root)));
        o["roots"] = std::move(roots);
        o["identity"] = Json::string(utf8(p.identity));
        o["last_thread_id"] = Json::string(p.last_thread_id);
        o["files_w"] = Json::number(p.files_w);
        o["agent_w"] = Json::number(p.agent_w);
        o["history_w"] = Json::number(p.history_w);
        arr.push(std::move(o));
    }
    j["projects"] = std::move(arr);
    Json ca = Json::array();
    for (const auto& c : conversations) {
        Json o = Json::object();
        o["id"] = Json::string(c.id);
        o["project_id"] = Json::string(c.project_id);
        o["account"] = Json::string(c.account);
        o["backend"] = Json::string(c.backend);
        o["provider_id"] = Json::string(c.provider_id.empty() ? "openai" : c.provider_id);
        o["thread_id"] = Json::string(c.thread_id);
        o["title"] = Json::string(c.title);
        o["preview"] = Json::string(c.preview);
        o["updated_at"] = Json::number(c.updated_at);
        o["title_manual"] = Json::boolean(c.title_manual);
        o["title_generated"] = Json::boolean(c.title_generated);
        o["local_messages"] = c.local_messages;
        o["pinned"] = Json::boolean(c.pinned);
        o["archived"] = Json::boolean(c.archived);
        o["deleted"] = Json::boolean(c.deleted);
        o["resumable"] = Json::boolean(c.resumable);
        ca.push(std::move(o));
    }
    j["conversations"] = std::move(ca);
    return write_all(path, j.dump());
}

std::string snapshot_context(const std::vector<ContextChip>& chips) {
    if (chips.empty()) {
        return {};
    }
    std::string out = "Context attached by Scylla (explicit; not a full-tree grant):\n";
    for (const auto& c : chips) {
        out += "\n### ";
        if (c.kind == "image") {
            out += "Image attachment — ";
            out += c.label;
            out += "\nPath: ";
            // Wide path → UTF-8 for the prompt note (path-only; not multimodal vision).
            out += c.body;
            out += "\n";
            continue;
        }
        out += c.unsaved ? "Unsaved buffer" : "Saved file";
        out += " — ";
        out += c.label;
        if (c.line0 > 0) {
            out += " (L" + std::to_string(c.line0);
            if (c.line1 > c.line0) {
                out += "-" + std::to_string(c.line1);
            }
            out += ")";
        }
        out += "\n```\n";
        out += c.body;
        if (!c.body.empty() && c.body.back() != '\n') {
            out += "\n";
        }
        out += "```\n";
    }
    return out;
}

}  // namespace scyllagpt
