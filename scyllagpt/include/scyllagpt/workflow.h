#pragma once
#include "scyllagpt/knowledge.h"
#include "scyllagpt/utf.h"
#include <filesystem>

namespace scyllagpt {
enum class WorkflowMode { Execute, Plan, Ask };

inline bool composer_should_submit(bool ctrl, bool shift, bool alt, bool composing) {
    return ctrl && !shift && !alt && !composing;
}

inline std::wstring workflow_plan_directory(const KnowledgeStore& knowledge, const std::string& project_id,
                                             const std::wstring& project_root) {
    namespace fs = std::filesystem;
    std::wstring first;
    for (const auto* source : knowledge.list_enabled_for_project(project_id)) {
        if (!source->agent_available) continue;
        fs::path base(source->path);
        std::error_code source_error;
        if (!fs::is_directory(base, source_error)) continue;
        if (base.filename() != L"plans") base /= L"plans";
        const auto draft = (base / L"draft").wstring();
        if (!knowledge.resolve_effective_access(source->id, draft).writable) continue;
        std::error_code error;
        if (fs::is_directory(base, error)) return draft;
        if (first.empty()) first = draft;
    }
    if (!first.empty()) return first;
    return project_root.empty() ? std::wstring{} : (fs::path(project_root) / L"plans" / L"draft").wstring();
}

inline std::string workflow_instructions(WorkflowMode mode, const std::wstring& plan_directory,
                                          const std::string& project) {
    if (mode == WorkflowMode::Ask)
        return "Scylla workflow: Ask. Answer questions and inspect available context as needed. "
               "Do not create, edit, move, or delete files, execute mutations, or implement changes. "
               "Explain proposed changes in chat. This mode applies to this message only.\n\n";
    if (mode == WorkflowMode::Execute)
        return "Scylla workflow: Execute. Actively carry out the user's requested project work, edit files as needed, "
               "and verify the result. Follow applicable project rules. When implementing an approved saved plan, "
               "keep its YAML status and folder synchronized: in_queue -> in_progress -> done after verification. "
               "This mode applies to this message only.\n\n";
    return "Scylla workflow: Plan. Research and prepare a durable implementation plan; do not implement project changes. "
           "Write or update the plan as Markdown under this exact draft directory: " + utf8(plan_directory) +
           ". Create missing plan directories as needed. Do not overwrite unrelated plans. Project: " + project +
           ". Read applicable Knowledge rules and prior plans for naming. Use a descriptive .plan.md filename; "
           "prefix CS_ for commonspace/cs-space-builder, SS_ for seersite, INVIV_ for invivaria, SYNQ_ for synq, "
           "PROMP_ for v4.prompli.com/v5.prompli.com; otherwise use the project name as prefix. "
           "Required YAML frontmatter: name, overview, status: draft, project, todos: []; optional linear_task_id. "
           "Include scope, context/source references, approach, verification commands/manual checks, deployment, and resume steps. "
           "Lifecycle folders are draft, backlog, in_queue, in_progress, done; folder must match YAML status. "
           "Leave new plans in draft; do not approve or start implementation in Plan mode. Link the saved file in your reply. "
           "If writing fails, report it and include the draft in chat; never claim an unsaved plan was saved. "
           "This mode applies to this message only.\n\n";
}
} // namespace scyllagpt
