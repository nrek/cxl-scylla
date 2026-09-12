#include "scyllagpt/project_context.h"

#include "scyllagpt/store.h"
#include "scyllagpt/file_io.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/utf.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace scyllagpt {
std::wstring project_key(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
}

std::vector<std::string> open_project_names(const WorkspaceStore& store) {
    std::vector<std::string> names;
    for (const auto& root : store.open_roots()) {
        const auto name = utf8(project_key(std::filesystem::path(canonicalize_path(root)).filename().wstring()));
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

std::vector<std::wstring> scoped_knowledge_paths(const WorkspaceStore& store, const KnowledgeStore& knowledge) {
    auto paths = knowledge.agent_accessible_paths(store.active_project_id);
    for (const auto& project : store.projects) {
        if (!store.contains_open_project(project.id)) continue;
        for (const auto& path : knowledge.agent_accessible_paths(project.id)) {
            if (std::none_of(paths.begin(), paths.end(), [&](const auto& p) { return project_key(p) == project_key(path); }))
                paths.push_back(path);
        }
    }
    return paths;
}

// Match explicit ownership, never a casual mention of another repository in the body.
bool project_document_matches(const std::filesystem::path& relative, const std::string& body,
                                     const std::vector<std::string>& projects) {
    for (const auto& name : projects) {
        const auto key = utf16(name);
        for (const auto& part : relative)
            if (project_key(part.wstring()) == key || project_key(part.stem().wstring()) == key) return true;
        std::istringstream lines(body.substr(0, 8192));
        std::string line;
        while (std::getline(lines, line)) {
            auto lower = utf8(project_key(utf16(line)));
            lower.erase(std::remove(lower.begin(), lower.end(), '`'), lower.end());
            lower.erase(std::remove(lower.begin(), lower.end(), '\r'), lower.end());
            if (lower == "project: " + name || lower == "project: \"" + name + "\"" ||
                lower == "| repo | " + name + "/ |" || lower == "| project | " + name + " |") return true;
        }
    }
    return false;
}

std::string scoped_knowledge_context(const WorkspaceStore& store, const std::vector<std::wstring>& grants) {
    const auto projects = open_project_names(store);
    if (projects.empty()) return "No project is open. Do not load project history or project knowledge.\n";
    std::ostringstream out;
    out << "Open project scope (derived from the open root folder names):\n";
    for (const auto& name : projects) out << "- " << name << "\n";
    out << "Retrieve blueprints, recent handoffs, and STRATA data only for these projects. "
           "For STRATA search/recent calls, issue a separate query with the exact project filter for each name above; "
           "never issue an unfiltered workspace query. Discard results whose project metadata does not match. "
           "Shared workspace instructions may still apply.\n";
    std::set<std::wstring> seen;
    size_t remaining = 32768;
    for (const auto& grant : grants) {
        const auto base = std::filesystem::path(canonicalize_path(grant));
        if (project_key(base.filename().wstring()) != L".md") continue;
        for (const auto* kind : {L"blueprints", L"handoff"}) {
            const auto folder = base / kind;
            std::error_code ec;
            std::vector<std::filesystem::directory_entry> candidates;
            std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file(ec) || project_key(it->path().extension().wstring()) != L".md") continue;
                const auto modified = it->last_write_time(ec);
                if (ec) break;
                if (std::wstring_view(kind) == L"handoff" &&
                    std::filesystem::file_time_type::clock::now() - modified > std::chrono::hours(48)) continue;
                candidates.push_back(*it);
            }
            std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
                std::error_code ea, eb;
                return a.last_write_time(ea) > b.last_write_time(eb);
            });
            for (const auto& entry : candidates) {
                if (remaining == 0) break;
                const auto path = entry.path();
                if (!seen.insert(project_key(path.wstring())).second) continue;
                // Bound reads as well as prompt size, even for oversized markdown files.
                std::ifstream stream(path, std::ios::binary);
                std::string body(8192, '\0');
                stream.read(body.data(), static_cast<std::streamsize>(body.size()));
                body.resize(static_cast<size_t>(stream.gcount()));
                if (!project_document_matches(path.lexically_relative(folder), body, projects)) continue;
                body.resize((std::min)(body.size(), remaining));
                // A byte budget may split the final UTF-8 character.
                auto tail = body.size();
                while (tail > 0 && (static_cast<unsigned char>(body[tail - 1]) & 0xc0) == 0x80) --tail;
                if (tail > 0) {
                    const auto lead = static_cast<unsigned char>(body[tail - 1]);
                    const size_t width = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
                    if (body.size() - (tail - 1) < width) body.resize(tail - 1);
                }
                remaining -= body.size();
                out << "\n<project-knowledge path=" << Json::string(utf8(path.wstring())).dump() << ">\n"
                    << body << "\n</project-knowledge>\n";
            }
        }
    }
    return out.str();
}
} // namespace scyllagpt
