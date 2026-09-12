#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

enum class TerminalProfileSource {
    Detected,
    Custom,
};

enum class WorkingDirectoryMode {
    Project,
    Home,
    Custom,
};

struct TerminalProfile {
    std::string id;
    std::wstring name;
    std::wstring executable;
    std::wstring args;
    TerminalProfileSource source = TerminalProfileSource::Detected;
    bool enabled = true;
    WorkingDirectoryMode working_directory_mode = WorkingDirectoryMode::Project;
    std::wstring custom_working_directory;
};

// Detect installed shells when the executable exists on disk.
// Order: PowerShell 7, Windows PowerShell, cmd, WSL distros, Git Bash, Cygwin.
std::vector<TerminalProfile> discover_terminal_profiles();

// Apply enablement overrides (missing keys leave profile.enabled unchanged).
void apply_terminal_profile_enablement(std::vector<TerminalProfile>* profiles,
                                       const std::map<std::string, bool>& enabled_by_id);

// Profiles with enabled==true only (for New Terminal menus).
std::vector<TerminalProfile> enabled_terminal_profiles(const std::vector<TerminalProfile>& profiles);

// Resolve default: preferred id if enabled, else first enabled, else empty.
const TerminalProfile* find_default_terminal_profile(const std::vector<TerminalProfile>& profiles,
                                                    std::string_view preferred_id);

// Custom profiles JSON under appdata (terminals.json).
std::vector<TerminalProfile> load_custom_terminal_profiles(const std::wstring& path);
bool save_custom_terminal_profiles(const std::wstring& path, const std::vector<TerminalProfile>& custom);

// Merge discovered + custom, then apply enablement overrides.
std::vector<TerminalProfile> merge_terminal_profiles(const std::vector<TerminalProfile>& discovered,
                                                    const std::vector<TerminalProfile>& custom,
                                                    const std::map<std::string, bool>& enabled_by_id);

// --- Pure / unit-testable helpers (no HWND) ---

// Returns "ask" | "allow" | "block". Unknown / empty → "ask".
std::string normalize_agent_terminal_policy(std::string_view raw);
bool is_valid_agent_terminal_policy(std::string_view raw);
std::string terminal_agent_policy(const std::map<std::string, std::string>& policies,
                                 const std::string& id, std::string_view fallback);

std::string terminal_profile_source_string(TerminalProfileSource s);
TerminalProfileSource parse_terminal_profile_source(std::string_view s);

std::string working_directory_mode_string(WorkingDirectoryMode m);
WorkingDirectoryMode parse_working_directory_mode(std::string_view s);

// Stable id from prefix + display name (lowercase ascii slug).
std::string make_terminal_profile_id(std::string_view prefix, std::wstring_view name);

// Parse `wsl -l -q` stdout (UTF-16LE with optional BOM, or UTF-8).
std::vector<std::wstring> parse_wsl_list_quiet(std::string_view raw_bytes);

// Resolve cwd for a profile given project root and user profile home.
std::wstring resolve_profile_cwd(const TerminalProfile& profile, const std::wstring& project_root,
                                 const std::wstring& home_directory);

// True if path basename is a known shell executable name (case-insensitive).
bool is_known_shell_executable(std::wstring_view path_or_name);

}  // namespace scyllagpt
