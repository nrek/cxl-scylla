#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "scyllagpt/utf.h"

namespace scyllagpt {
inline std::string workflow_lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
inline std::string workflow_trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n\"'");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n\"'") - a + 1);
}
inline std::set<std::string> workflow_words(std::string text) {
    text = workflow_lower(text);
    for (auto& c : text) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) c = ' ';
    static const std::set<std::string> noise = {"the","and","for","with","this","that","from","when","only","use","user","work","project","projects","file","files","rule","rules","skill","skills","before","after","under","into","should","must","app","agent","workflow","scylla","using","used","read","write","name","path","paths","all","any","are","not","has","have","how","its","our","you","your","can","will","then","than","need","make","also","etc"};
    std::set<std::string> result;
    std::istringstream words(text); std::string word;
    while (words >> word) if (word.size() > 2 && !noise.count(word)) result.insert(word);
    return result;
}
inline bool workflow_wildcard(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) { ++p; ++t; }
        else if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = t; }
        else if (star != std::string::npos) { p = star + 1; t = ++retry; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
struct WorkflowSourceMetadata {
    std::string name, description;
    std::vector<std::string> globs;
    bool always = false;
};
// Read only bounded metadata once per file version. Deleted/denied sources never
// enter the candidate catalog; cached entries cannot independently grant access.
inline WorkflowSourceMetadata workflow_metadata(const std::wstring& path) {
    namespace fs = std::filesystem;
    struct Cached { fs::file_time_type stamp; std::uintmax_t size; WorkflowSourceMetadata meta; };
    static std::map<std::wstring, Cached> cache;
    std::error_code ec;
    const auto stamp = fs::last_write_time(path, ec); if (ec) return {};
    const auto size = fs::file_size(path, ec); if (ec) return {};
    auto found = cache.find(path);
    if (found != cache.end() && found->second.stamp == stamp && found->second.size == size) return found->second.meta;
    WorkflowSourceMetadata meta;
    meta.name = workflow_lower(utf8(fs::path(path).stem().wstring()));
    if (meta.name == "skill") meta.name = workflow_lower(utf8(fs::path(path).parent_path().filename().wstring()));
    std::ifstream file{fs::path(path)};
    std::string body(16384, '\0'); file.read(body.data(), body.size()); body.resize(static_cast<std::size_t>(file.gcount()));
    std::istringstream lines(body); std::string line, key;
    if (std::getline(lines, line) && workflow_trim(line) == "---") {
        while (std::getline(lines, line) && workflow_trim(line) != "---") {
            auto clean = workflow_trim(line);
            if (clean.starts_with("- ") && key == "globs") { meta.globs.push_back(workflow_lower(workflow_trim(clean.substr(2)))); continue; }
            auto colon = clean.find(':');
            if (colon == std::string::npos) {
                if (key == "description") meta.description += " " + clean;
                continue;
            }
            key = clean.substr(0, colon); auto value = workflow_trim(clean.substr(colon + 1));
            if (key == "description") meta.description = value;
            if (key == "alwaysApply") meta.always = value == "true";
            if (key == "globs" && !value.empty()) {
                if (value.front() == '[' && value.back() == ']') value = value.substr(1, value.size() - 2);
                std::istringstream entries(value); std::string entry;
                while (std::getline(entries, entry, ',')) meta.globs.push_back(workflow_lower(workflow_trim(entry)));
            }
        }
    }
    if (cache.size() >= 1024) cache.clear();
    cache[path] = {stamp, size, meta};
    return meta;
}
inline int workflow_relevance(const WorkflowSourceMetadata& meta, const std::string& task,
                              const std::string& project, const std::vector<std::string>& project_files,
                              bool local_instruction) {
    const auto name = meta.name;
    const auto text = workflow_lower(task);
    const auto repo = workflow_lower(project);
    const auto words = workflow_words(text);
    auto has = [&](std::initializer_list<const char*> terms) {
        for (const auto* term : terms) if (words.count(term)) return true;
        return false;
    };
    // Legacy workspace rules sometimes mark narrowly scoped guidance alwaysApply.
    // Family and intent gates take precedence over that flag.
    for (const auto* family : {"synq", "synapse", "scout", "blind-insight", "scylla"}) {
        if (name.starts_with(family) && repo.find(family) == std::string::npos) return 0;
    }
    if (name.find("backfill") != std::string::npos && !has({"backfill","backfills","gaps","recovery","keystones","fit_metrics"})) return 0;
    if ((name.find("deploy") != std::string::npos || name.find("production") != std::string::npos) && !has({"deploy","deployment","deployments","production","staging","release","server","host"})) return 0;
    if (name == "reports-organization" && !has({"report","reports","audit","digest"})) return 0;
    if (name == "project-initialization" && !(has({"new","initialize","scaffold","create"}) && (text.find("project") != std::string::npos || has({"repo","repository"})))) return 0;
    if (name.find("commit") != std::string::npos && !has({"commit","commits","push"})) return 0;
    if (name.find("linear") != std::string::npos && !has({"linear","ticket","tickets"})) return 0;
    if (name.find("drive-share") != std::string::npos && !has({"drive","share","shared"})) return 0;
    if (!meta.globs.empty()) {
        bool match = false;
        for (const auto& glob : meta.globs) for (const auto& file : project_files)
            if (workflow_wildcard(glob, file) || workflow_wildcard(glob, repo + "/" + file)) match = true;
        if (!match) return 0;
    }
    if (local_instruction) return 100;
    if (name == "reports-organization" || name == "project-initialization" || name.find("commit") != std::string::npos ||
        name.find("deploy") != std::string::npos || name.find("backfill") != std::string::npos) return 55;
    static const std::set<std::string> baseline = {"agent-context-bootstrap","agent-work-lifecycle","workspace-knowledge","workspace-repo-scope","plans-management","handoff-logging","local-only-operator-executes"};
    if (baseline.count(name)) return 70;
    if (!meta.globs.empty()) return 60;
    int score = 0;
    for (const auto& token : workflow_words(name + " " + meta.description)) if (words.count(token)) ++score;
    return score >= 2 ? 20 + score : 0;
}
} // namespace scyllagpt
