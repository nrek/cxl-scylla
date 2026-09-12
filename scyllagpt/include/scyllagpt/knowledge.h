#pragma once

#include <string>
#include <vector>

namespace scyllagpt {

enum class SourceType {
    Automatic = 0,
    Knowledge,
    Skills,
    Instructions,
};

// ReadOnly / ReadWrite are the only values a source root may hold. NoAccess and Inherit are
// folder-override states; Inherit means "defer to the nearest ancestor that is not Inherit".
enum class AccessMode {
    ReadOnly = 0,
    ReadWrite = 1,
    NoAccess = 2,
    Inherit = 3,
};

enum class SourceHealth {
    Unknown = 0,
    Healthy,
    Unavailable,
    PermissionDenied,
    PartiallyAccessible,
    Disabled,
};

// Permission override on a folder below the source root. |relative_path| is normalized:
// backslash separators, no leading/trailing separator, empty means the root itself.
struct FolderPermissionOverride {
    std::wstring relative_path;
    AccessMode mode = AccessMode::Inherit;
};

// Scylla Knowledge & Skills source — folders outside the active repo tree.
struct KnowledgeSource {
    std::string id;
    std::wstring label;  // display name
    std::wstring path;
    SourceType type = SourceType::Automatic;
    bool recursive = true;
    AccessMode access = AccessMode::ReadOnly;
    bool agent_available = true;  // discoverable / context-eligible; never auto-sent
    bool enabled = true;          // disabled = config retained, but not listed / searched / refreshed
    std::vector<std::string> project_ids;  // empty = available to all projects
    std::string source_alias;              // optional @plans-style alias without leading @
    std::vector<FolderPermissionOverride> overrides;
    SourceHealth health = SourceHealth::Unknown;
};

// Result of walking root permission + folder overrides for one path.
struct EffectiveAccess {
    AccessMode mode = AccessMode::NoAccess;
    bool readable = false;
    bool writable = false;
    bool from_override = false;
    std::wstring matched_path;  // relative override that decided the result; empty = source root
};

// Detected skill entry (SKILL.md under a source).
struct DetectedSkill {
    std::wstring skill_md_path;
    std::wstring relative_dir;  // parent folder name / relative path hint
};

const wchar_t* source_type_label(SourceType t);
const wchar_t* access_mode_label(AccessMode m);
const wchar_t* source_health_label(SourceHealth h);
SourceType source_type_from_string(const std::string& s);
AccessMode access_mode_from_string(const std::string& s);
std::string source_type_to_string(SourceType t);
std::string access_mode_to_string(AccessMode m);

// Normalize a user-typed override path to the stored form: '/' → '\', no leading/trailing
// separator, collapsed repeats. Returns empty for the root or for anything that escapes it.
std::wstring normalize_override_path(const std::wstring& relative_path);

// Filesystem probe: existence, directory-ness, and readability of the root listing.
// Never mutates the source; returns Disabled first when |s| is disabled.
SourceHealth probe_source_health(const KnowledgeSource& s);

// True for paths that are volume roots (e.g. C:\). Knowledge sources must be folders.
bool is_drive_root_path(const std::wstring& path);

// Depth-limited scan for SKILL.md under |root| (max_depth levels below root).
std::vector<DetectedSkill> detect_skills(const std::wstring& root, int max_depth = 4);

class KnowledgeStore {
public:
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    KnowledgeSource* by_id(const std::string& id);
    const KnowledgeSource* by_id(const std::string& id) const;

    // Adds a source. Generates id if empty. Canonicalizes path; refuses drive roots.
    // |project_ids| empty means all projects. Defaults: recursive=true.
    KnowledgeSource* add_source(const std::wstring& label, const std::wstring& folder, SourceType type,
                                AccessMode access, bool agent_available,
                                const std::vector<std::string>& project_ids,
                                const std::string& source_alias = {});

    // Removes registration only — never deletes disk files.
    bool remove(const std::string& id);
    bool update(const KnowledgeSource& source);

    std::vector<KnowledgeSource*> list_for_project(const std::string& project_id);
    std::vector<const KnowledgeSource*> list_for_project(const std::string& project_id) const;

    // Explorer / search / agent listing: same as list_for_project minus disabled sources.
    std::vector<const KnowledgeSource*> list_enabled_for_project(const std::string& project_id) const;

    bool effective_writable(const std::string& id) const;

    bool set_enabled(const std::string& id, bool enabled);

    // Overrides. |relative_path| is normalized before use; Inherit removes the stored entry.
    bool set_override(const std::string& id, const std::wstring& relative_path, AccessMode mode);
    bool remove_override(const std::string& id, const std::wstring& relative_path);

    // Resolution order: exact explicit override → nearest ancestor explicit override → root
    // permission. Disabled sources, unknown ids, and paths outside the root resolve to NoAccess.
    EffectiveAccess resolve_effective_access(const std::string& id,
                                             const std::wstring& relative_or_absolute_path) const;

    // Probe and store health for one / every source. Returns the resulting health / probe count.
    SourceHealth refresh_health(const std::string& id);
    int refresh_all_health();

    // Find source that contains |file_path| (canonical prefix), preferring longest match.
    // Disabled sources are never matched.
    const KnowledgeSource* source_for_path(const std::wstring& file_path,
                                          const std::string& project_id = {}) const;

    // Absolute paths of enabled + agent_available sources for |project_id| (any Access
    // except No Access). Used to expand Codex sandbox writableRoots beyond the project
    // folder so Knowledge trees (e.g. D:\projects\.md) are browsable in Agent chat.
    std::vector<std::wstring> agent_accessible_paths(const std::string& project_id) const;

    const std::vector<KnowledgeSource>& sources() const { return sources_; }

    // Legacy alias used by older call sites / tests during transition.
    const std::vector<KnowledgeSource>& roots() const { return sources_; }

private:
    std::vector<KnowledgeSource> sources_;
};

}  // namespace scyllagpt
