#include "scyllagpt/knowledge.h"

#include "scyllagpt/json.h"
#include "scyllagpt/store.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>

namespace scyllagpt {
namespace {

std::string read_all(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return {};
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    return raw;
}

bool write_all(const std::wstring& path, const std::string& body) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

SourceHealth health_from_string(const std::string& s) {
    const std::string v = to_lower_ascii(s);
    if (v == "healthy") {
        return SourceHealth::Healthy;
    }
    if (v == "unavailable") {
        return SourceHealth::Unavailable;
    }
    if (v == "permission_denied" || v == "permission-denied") {
        return SourceHealth::PermissionDenied;
    }
    if (v == "partially_accessible" || v == "partially-accessible") {
        return SourceHealth::PartiallyAccessible;
    }
    if (v == "disabled") {
        return SourceHealth::Disabled;
    }
    return SourceHealth::Unknown;
}

std::string health_to_string(SourceHealth h) {
    switch (h) {
        case SourceHealth::Healthy:
            return "healthy";
        case SourceHealth::Unavailable:
            return "unavailable";
        case SourceHealth::PermissionDenied:
            return "permission_denied";
        case SourceHealth::PartiallyAccessible:
            return "partially_accessible";
        case SourceHealth::Disabled:
            return "disabled";
        default:
            return "unknown";
    }
}

std::string normalize_alias(std::string alias) {
    while (!alias.empty() && (alias.front() == '@' || alias.front() == ' ')) {
        alias.erase(alias.begin());
    }
    while (!alias.empty() && alias.back() == ' ') {
        alias.pop_back();
    }
    return alias;
}

bool source_matches_project(const KnowledgeSource& s, const std::string& project_id) {
    if (s.project_ids.empty()) {
        return true;  // all projects
    }
    if (project_id.empty()) {
        return false;
    }
    for (const auto& pid : s.project_ids) {
        if (pid == project_id) {
            return true;
        }
    }
    return false;
}

// Longest-prefix match on normalized relative override paths, matching whole segments only so
// "plan" never captures "plans\a.md".
bool relative_covers(const std::wstring& candidate, const std::wstring& rel) {
    if (candidate.empty()) {
        return true;  // root override covers everything below it
    }
    if (rel.size() < candidate.size()) {
        return false;
    }
    if (_wcsnicmp(rel.c_str(), candidate.c_str(), candidate.size()) != 0) {
        return false;
    }
    return rel.size() == candidate.size() || rel[candidate.size()] == L'\\';
}

bool path_under_root(const std::wstring& file, const std::wstring& root) {
    if (file.empty() || root.empty()) {
        return false;
    }
    if (file.size() < root.size()) {
        return false;
    }
    if (_wcsnicmp(file.c_str(), root.c_str(), root.size()) != 0) {
        return false;
    }
    if (file.size() == root.size()) {
        return true;
    }
    const wchar_t sep = file[root.size()];
    return sep == L'\\' || sep == L'/';
}

void detect_skills_recursive(const std::wstring& dir, const std::wstring& root, int depth_left,
                             std::vector<DetectedSkill>& out) {
    if (depth_left < 0 || out.size() >= 256) {
        return;
    }
    const std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        const std::wstring child = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue;
            }
            detect_skills_recursive(child, root, depth_left - 1, out);
        } else if (_wcsicmp(fd.cFileName, L"SKILL.md") == 0) {
            DetectedSkill sk;
            sk.skill_md_path = canonicalize_path(child);
            if (sk.skill_md_path.size() > root.size() + 1) {
                sk.relative_dir = sk.skill_md_path.substr(root.size() + 1);
                const auto slash = sk.relative_dir.find_last_of(L"\\/");
                if (slash != std::wstring::npos) {
                    sk.relative_dir = sk.relative_dir.substr(0, slash);
                } else {
                    sk.relative_dir.clear();
                }
            }
            out.push_back(std::move(sk));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

KnowledgeSource source_from_json(const Json& j) {
    KnowledgeSource s;
    s.id = j.at("id").as_string("");
    {
        std::string label = j.at("label").as_string("");
        if (label.empty()) {
            label = j.at("name").as_string("");
        }
        s.label = utf16(label);
    }
    s.path = utf16(j.at("path").as_string());
    if (!j.at("type").is_null()) {
        s.type = source_type_from_string(j.at("type").as_string("automatic"));
    }
    s.recursive = j.at("recursive").is_null() ? true : j.at("recursive").as_bool(true);

    if (!j.at("access").is_null()) {
        s.access = access_mode_from_string(j.at("access").as_string("read_only"));
    } else if (!j.at("writable").is_null()) {
        // Legacy bool
        s.access = j.at("writable").as_bool(false) ? AccessMode::ReadWrite : AccessMode::ReadOnly;
    }

    s.agent_available = j.at("agent_available").is_null() ? true : j.at("agent_available").as_bool(true);
    s.enabled = j.at("enabled").is_null() ? true : j.at("enabled").as_bool(true);

    const Json& ovr = j.at("permission_overrides");
    if (ovr.is_array()) {
        for (const Json& item : ovr.array_items()) {
            if (!item.is_object()) {
                continue;
            }
            FolderPermissionOverride o;
            std::string rel = item.at("path").as_string("");
            if (rel.empty()) {
                rel = item.at("relative_path").as_string("");
            }
            o.relative_path = normalize_override_path(utf16(rel));
            if (o.relative_path.empty()) {
                continue;  // the root is expressed by KnowledgeSource::access, not an override
            }
            o.mode = access_mode_from_string(item.at("mode").as_string("inherit"));
            if (o.mode == AccessMode::Inherit) {
                continue;  // inherit is the absence of an override
            }
            s.overrides.push_back(std::move(o));
        }
    }

    const Json& pids = j.at("project_ids");
    if (pids.is_array()) {
        for (const Json& item : pids.array_items()) {
            const std::string pid = item.as_string("");
            if (!pid.empty()) {
                s.project_ids.push_back(pid);
            }
        }
    } else {
        // Legacy single project_id
        const std::string legacy = j.at("project_id").as_string("");
        if (!legacy.empty()) {
            s.project_ids.push_back(legacy);
        }
    }

    {
        std::string alias = j.at("source_alias").as_string("");
        if (alias.empty()) {
            alias = j.at("alias").as_string("");
        }
        s.source_alias = normalize_alias(alias);
    }
    if (!j.at("health").is_null()) {
        s.health = health_from_string(j.at("health").as_string());
    }
    return s;
}

Json source_to_json(const KnowledgeSource& s) {
    Json j = Json::object();
    j["id"] = Json::string(s.id);
    j["label"] = Json::string(utf8(s.label));
    j["name"] = Json::string(utf8(s.label));
    j["path"] = Json::string(utf8(s.path));
    j["type"] = Json::string(source_type_to_string(s.type));
    j["recursive"] = Json::boolean(s.recursive);
    j["access"] = Json::string(access_mode_to_string(s.access));
    // Legacy mirror for older readers
    j["writable"] = Json::boolean(s.access == AccessMode::ReadWrite);
    j["agent_available"] = Json::boolean(s.agent_available);
    j["enabled"] = Json::boolean(s.enabled);

    Json ovr = Json::array();
    for (const auto& o : s.overrides) {
        Json item = Json::object();
        item["path"] = Json::string(utf8(o.relative_path));
        item["mode"] = Json::string(access_mode_to_string(o.mode));
        ovr.push(std::move(item));
    }
    j["permission_overrides"] = std::move(ovr);

    Json arr = Json::array();
    for (const auto& pid : s.project_ids) {
        arr.push(Json::string(pid));
    }
    j["project_ids"] = std::move(arr);
    if (s.project_ids.size() == 1) {
        j["project_id"] = Json::string(s.project_ids.front());
    } else if (s.project_ids.empty()) {
        j["project_id"] = Json::string("");
    }

    j["source_alias"] = Json::string(s.source_alias);
    if (s.health != SourceHealth::Unknown) {
        j["health"] = Json::string(health_to_string(s.health));
    }
    return j;
}

}  // namespace

const wchar_t* source_type_label(SourceType t) {
    switch (t) {
        case SourceType::Knowledge:
            return L"Knowledge";
        case SourceType::Skills:
            return L"Skills";
        case SourceType::Instructions:
            return L"Instructions";
        default:
            return L"Automatic";
    }
}

const wchar_t* access_mode_label(AccessMode m) {
    switch (m) {
        case AccessMode::ReadWrite:
            return L"Read + Write";
        case AccessMode::NoAccess:
            return L"No Access";
        case AccessMode::Inherit:
            return L"Inherit";
        default:
            return L"Read Only";
    }
}

const wchar_t* source_health_label(SourceHealth h) {
    switch (h) {
        case SourceHealth::Healthy:
            return L"Healthy";
        case SourceHealth::Unavailable:
            return L"Unavailable";
        case SourceHealth::PermissionDenied:
            return L"Permission Denied";
        case SourceHealth::PartiallyAccessible:
            return L"Partially Accessible";
        case SourceHealth::Disabled:
            return L"Disabled";
        default:
            return L"Unknown";
    }
}

SourceType source_type_from_string(const std::string& s) {
    const std::string v = to_lower_ascii(s);
    if (v == "knowledge") {
        return SourceType::Knowledge;
    }
    if (v == "skills" || v == "skill") {
        return SourceType::Skills;
    }
    if (v == "instructions" || v == "instruction") {
        return SourceType::Instructions;
    }
    return SourceType::Automatic;
}

AccessMode access_mode_from_string(const std::string& s) {
    const std::string v = to_lower_ascii(s);
    if (v == "read_write" || v == "read-write" || v == "readwrite" || v == "rw" || v == "writable") {
        return AccessMode::ReadWrite;
    }
    if (v == "none" || v == "no_access" || v == "no-access" || v == "noaccess" || v == "denied") {
        return AccessMode::NoAccess;
    }
    if (v == "inherit" || v == "inherited") {
        return AccessMode::Inherit;
    }
    return AccessMode::ReadOnly;
}

std::string source_type_to_string(SourceType t) {
    switch (t) {
        case SourceType::Knowledge:
            return "knowledge";
        case SourceType::Skills:
            return "skills";
        case SourceType::Instructions:
            return "instructions";
        default:
            return "automatic";
    }
}

std::string access_mode_to_string(AccessMode m) {
    switch (m) {
        case AccessMode::ReadWrite:
            return "read_write";
        case AccessMode::NoAccess:
            return "none";
        case AccessMode::Inherit:
            return "inherit";
        default:
            return "read_only";
    }
}

std::wstring normalize_override_path(const std::wstring& relative_path) {
    std::wstring out;
    out.reserve(relative_path.size());
    std::wstring segment;
    auto flush = [&]() {
        if (segment.empty() || segment == L".") {
            segment.clear();
            return true;
        }
        if (segment == L"..") {
            segment.clear();
            return false;  // never let an override climb out of its source root
        }
        while (!segment.empty() && (segment.back() == L' ' || segment.back() == L'.')) {
            segment.pop_back();  // Win32 ignores trailing dots/spaces; two spellings must not differ
        }
        if (segment.empty()) {
            return true;
        }
        if (!out.empty()) {
            out.push_back(L'\\');
        }
        out += segment;
        segment.clear();
        return true;
    };
    for (const wchar_t c : relative_path) {
        if (c == L'\\' || c == L'/') {
            if (!flush()) {
                return {};
            }
        } else {
            segment.push_back(c);
        }
    }
    if (!flush()) {
        return {};
    }
    // A drive-qualified or rooted spelling is not a relative override.
    if (out.size() >= 2 && out[1] == L':') {
        return {};
    }
    return out;
}

SourceHealth probe_source_health(const KnowledgeSource& s) {
    if (!s.enabled) {
        return SourceHealth::Disabled;
    }
    if (s.path.empty()) {
        return SourceHealth::Unavailable;
    }
    const DWORD attr = GetFileAttributesW(s.path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        const DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            return SourceHealth::PermissionDenied;
        }
        return SourceHealth::Unavailable;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return SourceHealth::Unavailable;  // a source must be a folder, not a file
    }
    // Existence is not readability: a folder can be listed in Explorer and still refuse enumeration.
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((s.path + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            return SourceHealth::PermissionDenied;
        }
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES) {
            return SourceHealth::Unavailable;
        }
    } else {
        FindClose(h);
    }
    for (const auto& o : s.overrides) {
        if (o.mode == AccessMode::NoAccess) {
            return SourceHealth::PartiallyAccessible;
        }
    }
    return SourceHealth::Healthy;
}

bool is_drive_root_path(const std::wstring& path) {
    auto is_root_form = [](const std::wstring& c) {
        if (c.size() == 2 && ((c[0] >= L'A' && c[0] <= L'Z') || (c[0] >= L'a' && c[0] <= L'z')) && c[1] == L':') {
            return true;
        }
        if (c.size() == 3 && ((c[0] >= L'A' && c[0] <= L'Z') || (c[0] >= L'a' && c[0] <= L'z')) && c[1] == L':' &&
            (c[2] == L'\\' || c[2] == L'/')) {
            return true;
        }
        return false;
    };
    if (is_root_form(path)) {
        return true;
    }
    const std::wstring c = canonicalize_path(path);
    if (is_root_form(c)) {
        return true;
    }
    // UNC share root \\server\share is OK; bare \\server is not a usable folder grant.
    if (c.size() >= 2 && c[0] == L'\\' && c[1] == L'\\') {
        std::size_t slash = 2;
        while (slash < c.size() && c[slash] != L'\\' && c[slash] != L'/') {
            ++slash;
        }
        if (slash >= c.size()) {
            return true;  // \\server only
        }
    }
    return false;
}

std::vector<DetectedSkill> detect_skills(const std::wstring& root, int max_depth) {
    std::vector<DetectedSkill> out;
    const std::wstring canon = canonicalize_path(root);
    if (canon.empty() || is_drive_root_path(canon)) {
        return out;
    }
    const DWORD attr = GetFileAttributesW(canon.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return out;
    }
    const int depth = (std::max)(0, max_depth);
    detect_skills_recursive(canon, canon, depth, out);
    return out;
}

bool KnowledgeStore::load(const std::wstring& path) {
    sources_.clear();
    const std::string raw = read_all(path);
    if (raw.empty()) {
        return true;  // missing file = empty store
    }
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return false;
    }
    const Json& arr = j.at("roots").is_null() ? j.at("sources") : j.at("roots");
    if (!arr.is_array()) {
        return true;
    }
    for (const Json& item : arr.array_items()) {
        if (!item.is_object()) {
            continue;
        }
        KnowledgeSource s = source_from_json(item);
        if (s.id.empty() || s.path.empty()) {
            continue;
        }
        s.path = canonicalize_path(s.path);
        if (is_drive_root_path(s.path)) {
            continue;
        }
        s.source_alias = normalize_alias(s.source_alias);
        if (!s.enabled) {
            s.health = SourceHealth::Disabled;
        }
        sources_.push_back(std::move(s));
    }
    return true;
}

bool KnowledgeStore::save(const std::wstring& path) const {
    Json arr = Json::array();
    for (const auto& s : sources_) {
        arr.push(source_to_json(s));
    }
    Json j = Json::object();
    j["version"] = Json::number(2);
    j["sources"] = arr;
    j["roots"] = arr;  // dual key for older tooling / v1 readers
    return write_all(path, j.dump());
}

KnowledgeSource* KnowledgeStore::by_id(const std::string& id) {
    for (auto& s : sources_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

const KnowledgeSource* KnowledgeStore::by_id(const std::string& id) const {
    for (const auto& s : sources_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

KnowledgeSource* KnowledgeStore::add_source(const std::wstring& label, const std::wstring& folder, SourceType type,
                                            AccessMode access, bool agent_available,
                                            const std::vector<std::string>& project_ids,
                                            const std::string& source_alias) {
    if (folder.empty()) {
        return nullptr;
    }
    const std::wstring canon = canonicalize_path(folder);
    if (canon.empty() || is_drive_root_path(canon)) {
        return nullptr;
    }
    KnowledgeSource s;
    s.id = make_uuid();
    if (s.id.empty()) {
        return nullptr;
    }
    s.label = label.empty() ? canon : label;
    s.path = canon;
    s.type = type;
    s.recursive = true;
    s.access = access;
    s.agent_available = agent_available;
    s.enabled = true;
    s.project_ids = project_ids;
    s.source_alias = normalize_alias(source_alias);
    s.health = probe_source_health(s);
    sources_.push_back(std::move(s));
    return &sources_.back();
}

bool KnowledgeStore::remove(const std::string& id) {
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        if (it->id == id) {
            sources_.erase(it);
            return true;
        }
    }
    return false;
}

bool KnowledgeStore::update(const KnowledgeSource& source) {
    KnowledgeSource* existing = by_id(source.id);
    if (!existing) {
        return false;
    }
    const std::wstring canon = canonicalize_path(source.path);
    if (canon.empty() || is_drive_root_path(canon)) {
        return false;
    }
    *existing = source;
    existing->path = canon;
    existing->source_alias = normalize_alias(source.source_alias);
    existing->recursive = true;  // cascade to all descendants; non-recursive is not exposed in UI
    // A root may only be Read or Read + Write; No Access / Inherit belong to folder overrides.
    if (existing->access != AccessMode::ReadWrite) {
        existing->access = AccessMode::ReadOnly;
    }
    for (auto it = existing->overrides.begin(); it != existing->overrides.end();) {
        it->relative_path = normalize_override_path(it->relative_path);
        if (it->relative_path.empty() || it->mode == AccessMode::Inherit) {
            it = existing->overrides.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

std::vector<KnowledgeSource*> KnowledgeStore::list_for_project(const std::string& project_id) {
    std::vector<KnowledgeSource*> out;
    for (auto& s : sources_) {
        if (source_matches_project(s, project_id)) {
            out.push_back(&s);
        }
    }
    return out;
}

std::vector<const KnowledgeSource*> KnowledgeStore::list_for_project(const std::string& project_id) const {
    std::vector<const KnowledgeSource*> out;
    for (const auto& s : sources_) {
        if (source_matches_project(s, project_id)) {
            out.push_back(&s);
        }
    }
    return out;
}

std::vector<const KnowledgeSource*> KnowledgeStore::list_enabled_for_project(
    const std::string& project_id) const {
    std::vector<const KnowledgeSource*> out;
    for (const auto& s : sources_) {
        if (s.enabled && source_matches_project(s, project_id)) {
            out.push_back(&s);
        }
    }
    return out;
}

bool KnowledgeStore::effective_writable(const std::string& id) const {
    const KnowledgeSource* s = by_id(id);
    return s && s->enabled && s->access == AccessMode::ReadWrite;
}

bool KnowledgeStore::set_enabled(const std::string& id, bool enabled) {
    KnowledgeSource* s = by_id(id);
    if (!s) {
        return false;
    }
    s->enabled = enabled;
    s->health = enabled ? probe_source_health(*s) : SourceHealth::Disabled;
    return true;
}

bool KnowledgeStore::set_override(const std::string& id, const std::wstring& relative_path, AccessMode mode) {
    KnowledgeSource* s = by_id(id);
    if (!s) {
        return false;
    }
    const std::wstring rel = normalize_override_path(relative_path);
    if (rel.empty()) {
        return false;  // the root is edited through KnowledgeSource::access
    }
    if (mode == AccessMode::Inherit) {
        return remove_override(id, rel);
    }
    for (auto& o : s->overrides) {
        if (_wcsicmp(o.relative_path.c_str(), rel.c_str()) == 0) {
            o.mode = mode;
            return true;
        }
    }
    FolderPermissionOverride o;
    o.relative_path = rel;
    o.mode = mode;
    s->overrides.push_back(std::move(o));
    return true;
}

bool KnowledgeStore::remove_override(const std::string& id, const std::wstring& relative_path) {
    KnowledgeSource* s = by_id(id);
    if (!s) {
        return false;
    }
    const std::wstring rel = normalize_override_path(relative_path);
    for (auto it = s->overrides.begin(); it != s->overrides.end(); ++it) {
        if (_wcsicmp(it->relative_path.c_str(), rel.c_str()) == 0) {
            s->overrides.erase(it);
            return true;
        }
    }
    return false;
}

EffectiveAccess KnowledgeStore::resolve_effective_access(const std::string& id,
                                                        const std::wstring& relative_or_absolute_path) const {
    EffectiveAccess out;
    const KnowledgeSource* s = by_id(id);
    if (!s || !s->enabled) {
        return out;  // NoAccess
    }

    // Absolute paths must be proven to live under the root before they mean anything; relative
    // paths are taken as-is after normalization.
    std::wstring rel;
    const bool looks_absolute = (relative_or_absolute_path.size() >= 2 && relative_or_absolute_path[1] == L':') ||
                                (relative_or_absolute_path.size() >= 2 && relative_or_absolute_path[0] == L'\\' &&
                                 relative_or_absolute_path[1] == L'\\');
    if (looks_absolute) {
        const std::wstring full = canonicalize_path(relative_or_absolute_path);
        if (!path_under_root(full, s->path)) {
            return out;  // NoAccess — outside the granted surface
        }
        rel = full.size() > s->path.size() ? full.substr(s->path.size() + 1) : std::wstring();
    } else {
        rel = relative_or_absolute_path;
        if (!rel.empty() && normalize_override_path(rel).empty() && rel != L"." && rel != L"\\" && rel != L"/") {
            return out;  // traversal or drive-qualified — refuse rather than guess
        }
    }
    rel = normalize_override_path(rel);

    // Non-recursive sources grant nothing below their immediate children.
    if (!s->recursive && rel.find(L'\\') != std::wstring::npos) {
        return out;
    }

    const FolderPermissionOverride* best = nullptr;
    for (const auto& o : s->overrides) {
        if (o.mode == AccessMode::Inherit) {
            continue;  // an explicit Inherit defers to the next ancestor up
        }
        if (!relative_covers(o.relative_path, rel)) {
            continue;
        }
        if (!best || o.relative_path.size() > best->relative_path.size()) {
            best = &o;
        }
    }

    if (best) {
        out.mode = best->mode;
        out.from_override = true;
        out.matched_path = best->relative_path;
    } else {
        out.mode = s->access == AccessMode::ReadWrite ? AccessMode::ReadWrite : AccessMode::ReadOnly;
    }
    out.readable = out.mode != AccessMode::NoAccess;
    out.writable = out.mode == AccessMode::ReadWrite;
    return out;
}

SourceHealth KnowledgeStore::refresh_health(const std::string& id) {
    KnowledgeSource* s = by_id(id);
    if (!s) {
        return SourceHealth::Unknown;
    }
    s->health = probe_source_health(*s);
    return s->health;
}

int KnowledgeStore::refresh_all_health() {
    int n = 0;
    for (auto& s : sources_) {
        s.health = probe_source_health(s);
        ++n;
    }
    return n;
}

const KnowledgeSource* KnowledgeStore::source_for_path(const std::wstring& file_path,
                                                      const std::string& project_id) const {
    const std::wstring file = canonicalize_path(file_path);
    const KnowledgeSource* best = nullptr;
    std::size_t best_len = 0;
    for (const auto& s : sources_) {
        if (!s.enabled) {
            continue;
        }
        if (!project_id.empty() && !source_matches_project(s, project_id)) {
            continue;
        }
        if (!path_under_root(file, s.path)) {
            continue;
        }
        if (s.path.size() >= best_len) {
            best = &s;
            best_len = s.path.size();
        }
    }
    return best;
}

std::vector<std::wstring> KnowledgeStore::agent_accessible_paths(const std::string& project_id) const {
    std::vector<std::wstring> out;
    for (const auto& s : sources_) {
        if (!s.enabled || !s.agent_available) {
            continue;
        }
        if (!source_matches_project(s, project_id)) {
            continue;
        }
        if (s.access == AccessMode::NoAccess) {
            continue;
        }
        if (s.path.empty()) {
            continue;
        }
        out.push_back(canonicalize_path(s.path));
    }
    return out;
}

}  // namespace scyllagpt
