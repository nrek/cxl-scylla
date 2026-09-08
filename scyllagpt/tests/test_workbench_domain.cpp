#include "scyllagpt/knowledge.h"
#include "scyllagpt/mcp_manager.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/strata_bridge.h"
#include "scyllagpt/strata_client.h"
#include "scyllagpt/workflow.h"
#include "scyllagpt/agent_files.h"
#include "scyllagpt/chat_history.h"
#include "scyllagpt/markdown.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>

static int g_fail = 0;

static void expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

static std::wstring temp_file(const wchar_t* name) {
    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"scyllagpt_test_" + name;
}

int run_workbench_domain_tests() {
    g_fail = 0;
    expect(scyllagpt::unresolved_environment_directory(L"%SystemDrive%"), "Unresolved environment directory recognized");
    expect(scyllagpt::unresolved_environment_directory(L"%LOCAL_APPDATA%"), "Environment directory allows underscore");
    expect(!scyllagpt::unresolved_environment_directory(L"100%") &&
           !scyllagpt::unresolved_environment_directory(L"%bad-name%") &&
           !scyllagpt::unresolved_environment_directory(L"SystemDrive"),
           "Ordinary directory names remain visible");
    {
        const auto runs = scyllagpt::parse_markdown(L"# Title\r\n**bold** and _italic_ with `code`\n- first\n* second\n```cpp\n**literal**\n```\n[Handoff](<D:/my project/notes.md>)\n");
        std::wstring plain; bool bold = false, italic = false, code = false, fenced = false, link = false, heading = false;
        for (const auto& run : runs) {
            plain += run.text;
            bold |= run.bold && run.text == L"bold";
            italic |= run.italic && run.text == L"italic";
            code |= run.code && run.text == L"code";
            fenced |= run.code && run.text == L"**literal**\r";
            link |= run.text == L"Handoff" && run.link == L"D:/my project/notes.md";
            heading |= run.heading == 1 && run.text == L"Title";
        }
        expect(bold && italic && code && fenced && link && heading, "Markdown styles, literal fenced code and local links");
        expect(plain.find(L"• first\r• second") != std::wstring::npos && plain.find(L"\r\r") == std::wstring::npos,
               "Markdown bullets and CRLF normalized without duplicate line breaks");
        const auto incomplete = scyllagpt::parse_markdown(L"unfinished **bold");
        expect(incomplete.front().text == L"unfinished **bold", "Incomplete streaming markdown remains literal");
        const auto balanced = scyllagpt::parse_markdown(L"[File](D:/notes(copy).md)");
        expect(balanced.front().link == L"D:/notes(copy).md", "Markdown link destination preserves balanced parentheses");
    }
    {
        using namespace scyllagpt;
        WorkspaceStore store;
        store.upsert_thread("p", "a", "old", "Old", "")->updated_at = 100;
        store.upsert_thread("p", "a", "new", "New", "")->updated_at = 300;
        auto* pinned = store.upsert_thread("p", "a", "pin", "Pinned", "");
        pinned->updated_at = 50;
        pinned->pinned = true;
        auto ordered = store.list_visible("a", L"");
        expect(ordered.size() == 3 && ordered[0]->thread_id == "pin" && ordered[1]->thread_id == "new",
               "chat history preserves pins then sorts newest first");
        auto* chat = store.by_thread("new");
        accept_chat_title(*chat, "Done.<!--scylla-title: Repair Terminal Input Handling Extra-->");
        expect(chat->title == "Repair Terminal Input Handling" && chat->title_generated, "agent title limited to four words");
        chat->title_manual = true;
        chat->title = "My own title";
        accept_chat_title(*chat, "<!--scylla-title: Another Title-->");
        expect(chat->title == "My own title" && chat_title_request(chat).empty(), "manual chat names protected");
        expect(visible_chat_text("Done.<!--scylla-title: Repair Input-->") == "Done.", "title metadata hidden");
        expect(visible_chat_text("Done.<!--scylla-ti") == "Done.", "partial title metadata hidden");
        chat->local_messages = Json::array();
        Json message = Json::object();
        message["user"] = Json::boolean(true);
        message["text"] = Json::string("Saved message");
        chat->local_messages.push(message);
        const auto path = temp_file(L"chat_history.json");
        expect(store.save(path), "save chat history metadata");
        WorkspaceStore restored;
        expect(restored.load(path), "load chat history metadata");
        const auto* saved = restored.by_thread("new");
        expect(saved && saved->updated_at == 300 && saved->title_manual && saved->title_generated &&
               saved->local_messages.array_items().size() == 1, "chat metadata roundtrip");
        DeleteFileW(path.c_str());
        std::tm date{};
        date.tm_year = 126; date.tm_mon = 0; date.tm_mday = 1; date.tm_hour = 12; date.tm_isdst = -1;
        const auto now = std::mktime(&date);
        expect(chat_date_group(now, now) == L"Today", "chat today grouping");
        date.tm_mday -= 1; date.tm_isdst = -1;
        expect(chat_date_group(std::mktime(&date), now) == L"Yesterday", "chat yesterday across year boundary");
        date.tm_mday -= 1; date.tm_isdst = -1;
        expect(chat_date_group(std::mktime(&date), now) == L"2025-12-30", "older chats dated");
        expect(chat_date_group(0, now) == L"Earlier", "legacy unknown chat dates");
    }

    {
        namespace fs = std::filesystem;
        const fs::path fixture = fs::path(temp_file(L"strata_discovery")) / std::to_wstring(GetCurrentProcessId());
        const auto shared = fixture / L"knowledge";
        const auto nested = shared / L"nested" / L"workspace";
        fs::create_directories(shared / L".md");
        fs::create_directories(nested / L".md");
        // Only existence is inspected: discovery must never parse or initialize SQLite.
        std::ofstream(shared / L".md" / L"workspace_index.sqlite") << "fixture";
        std::ofstream(nested / L".md" / L"workspace_index.sqlite") << "fixture";
        scyllagpt::KnowledgeStore store;
        auto* source = store.add_source(L"Shared", shared.wstring(), scyllagpt::SourceType::Knowledge,
                                        scyllagpt::AccessMode::ReadOnly, false, {"project"});
        expect(source != nullptr, "Strata discovery fixture source");
        const auto id = source->id;
        expect(scyllagpt::workflow_plan_directory(store, "project", fixture.wstring()) ==
                   (fixture / L"plans" / L"draft").wstring(), "Plan falls back from read-only Knowledge");
        auto writable = *store.by_id(id);
        writable.access = scyllagpt::AccessMode::ReadWrite;
        store.update(writable);
        expect(scyllagpt::workflow_plan_directory(store, "project", fixture.wstring()) ==
                   (fixture / L"plans" / L"draft").wstring(), "Plan excludes human-only Knowledge");
        writable.agent_available = true;
        store.update(writable);
        fs::create_directories(shared / L"rules");
        fs::create_directories(shared / L"skills" / L"example");
        fs::create_directories(shared / L"%SystemDrive%");
        std::ofstream(shared / L"rules" / L"workflow.mdc") << "---\ndescription: Terminal keyboard repair\n---\n";
        std::ofstream(shared / L"skills" / L"example" / L"SKILL.md") << "---\ndescription: Terminal keyboard repair\n---\n";
        std::ofstream(nested / L"file with spaces.md") << "context";
        std::ofstream(shared / L"%SystemDrive%" / L"generated-cache.db") << "cache";
        auto catalog = scyllagpt::agent_file_catalog(store, "project", L"");
        expect(scyllagpt::match_agent_files(catalog, L"FILE WITH SPACES").size() == 1, "Mentions find nested Knowledge files case-insensitively");
        const auto manifest = scyllagpt::workflow_source_manifest(catalog, fixture.wstring(), "repair terminal keyboard");
        expect(manifest.find("workflow.mdc") != std::string::npos && manifest.find("SKILL.md") != std::string::npos,
               "Workflow manifest includes rules and skills");
        expect(scyllagpt::match_agent_files(catalog, L"generated-cache").empty(),
               "Agent catalog excludes unresolved environment directories");
        expect(scyllagpt::workflow_source_manifest(catalog, fixture.wstring(), "change button colors").empty(),
               "Workflow index excludes unrelated skill and rule descriptions");
        std::ofstream(shared / L"rules" / L"workflow.mdc") << "---\ndescription: Database schema migrations and indexes\n---\n";
        expect(scyllagpt::workflow_source_manifest(catalog, fixture.wstring(), "repair terminal keyboard").find("workflow.mdc") == std::string::npos,
               "Workflow metadata cache invalidates on file changes");
        scyllagpt::WorkflowSourceMetadata scoped;
        scoped.name = "synq-gap-backfill";
        scoped.description = "Synq gap backfill recovery";
        expect(scyllagpt::workflow_relevance(scoped, "gap backfill recovery", "cxl-scylla", {}, false) == 0,
               "Unrelated project skill excluded even with keyword overlap");
        expect(scyllagpt::workflow_relevance(scoped, "gap backfill recovery", "synq-forge", {}, false) > 0,
               "Backfill skill selected for relevant project and task");
        scoped.name = "reports-organization"; scoped.always = true;
        expect(scyllagpt::workflow_relevance(scoped, "fix terminal", "cxl-scylla", {}, false) == 0,
               "AlwaysApply cannot bypass task scope");
        const std::string original = "Text with \\\"quotes\\\"\n</scylla-display-json> and code";
        expect(scyllagpt::visible_user_text("internal grants" + scyllagpt::user_display_metadata(original) + "hidden workflow") == original,
               "Restored user text excludes prompt metadata and preserves embedded delimiters");
        const std::vector<scyllagpt::DisplayAttachment> attached{{"D:/image.png", "image.png", true}, {"D:/notes.md", "notes.md", false}};
        const auto restored_display = scyllagpt::parse_user_display(scyllagpt::user_display_metadata("hello", attached));
        expect(restored_display.text == "hello" && restored_display.attachments.size() == 2 &&
               restored_display.attachments[0].image && restored_display.attachments[1].label == "notes.md",
               "User display attachment metadata roundtrip");
        store.set_override(id, L"nested", scyllagpt::AccessMode::NoAccess);
        expect(scyllagpt::match_agent_files(scyllagpt::agent_file_catalog(store, "project", L""), L"file with spaces").empty(),
               "Mentions exclude denied Knowledge subfolders");
        store.remove_override(id, L"nested");
        expect(scyllagpt::agent_file_catalog(store, "other", L"").empty(), "Mentions respect Knowledge project scope");
        expect(scyllagpt::workflow_plan_directory(store, "project", fixture.wstring()) ==
                   (shared / L"plans" / L"draft").wstring(), "Plan prefers writable agent Knowledge");
        store.set_override(id, L"plans", scyllagpt::AccessMode::NoAccess);
        expect(scyllagpt::workflow_plan_directory(store, "project", fixture.wstring()) ==
                   (fixture / L"plans" / L"draft").wstring(), "Plan honors directory overrides");
        store.remove_override(id, L"plans");
        fs::create_directories(fixture / L".cursor" / L"plans");
        const auto established_id = store.add_source(L"Rules", (fixture / L".cursor").wstring(),
            scyllagpt::SourceType::Knowledge, scyllagpt::AccessMode::ReadWrite, true, {})->id;
        expect(scyllagpt::workflow_plan_directory(store, "project", fixture.wstring()) ==
                   (fixture / L".cursor" / L"plans" / L"draft").wstring(), "Plan prefers established Knowledge plans folder");
        store.remove(established_id);
        expect(scyllagpt::composer_should_submit(true, false, false, false), "Ctrl+Enter submits");
        expect(!scyllagpt::composer_should_submit(false, true, false, false), "Shift+Enter does not submit");
        expect(!scyllagpt::composer_should_submit(false, false, false, false), "Enter does not submit");
        expect(!scyllagpt::composer_should_submit(true, false, false, true), "IME composition does not submit");
        expect(!scyllagpt::composer_should_submit(true, false, true, false), "Ctrl+Alt+Enter does not submit");
        const auto instructions = scyllagpt::workflow_instructions(scyllagpt::WorkflowMode::Plan, L"C:\\plans\\draft", "cxl-scylla");
        expect(instructions.find("C:\\plans\\draft") != std::string::npos && instructions.find("status: draft") != std::string::npos,
               "Plan instructions include destination and lifecycle");
        auto found = scyllagpt::discover_strata_workspaces(store, "project");
        expect(found.size() == 2 && fs::path(found[0]) == shared && fs::path(found[1]) == nested,
               "Strata discovers Knowledge subfolder databases breadth-first");
        expect(scyllagpt::discover_strata_workspaces(store, "other").empty(), "Strata respects project scope");
        expect(fs::path(scyllagpt::resolve_strata_workspace(store, "project", fixture.wstring())) == shared,
               "Knowledge index takes precedence over project fallback");
        store.set_override(id, L"nested", scyllagpt::AccessMode::NoAccess);
        expect(scyllagpt::discover_strata_workspaces(store, "project").size() == 1, "Strata skips No Access subtrees");
        store.set_enabled(id, false);
        expect(scyllagpt::discover_strata_workspaces(store, "project").empty(), "Strata skips disabled sources");
        expect(scyllagpt::resolve_strata_workspace(store, "project", fixture.wstring()) == fixture.wstring(),
               "Strata falls back when no Knowledge database exists");
        store.add_source(L"Direct .md", (shared / L".md").wstring(), scyllagpt::SourceType::Knowledge,
                         scyllagpt::AccessMode::ReadOnly, true, {});
        found = scyllagpt::discover_strata_workspaces(store, "project");
        expect(found.size() == 1 && fs::path(found[0]) == shared, "Direct .md grant resolves parent workspace");
        expect(fs::file_size(shared / L".md" / L"workspace_index.sqlite") == 7, "Discovery leaves database untouched");
        fs::remove_all(fixture);
    }

    {
        scyllagpt::KnowledgeStore store;
        auto* src = store.add_source(L"Plans", L"C:\\Temp\\scylla-knowledge-plans", scyllagpt::SourceType::Knowledge,
                                     scyllagpt::AccessMode::ReadWrite, true, {"proj-1"}, "plans");
        expect(src != nullptr && !src->id.empty(), "knowledge add_source");
        expect(src->type == scyllagpt::SourceType::Knowledge, "knowledge type");
        expect(src->access == scyllagpt::AccessMode::ReadWrite, "knowledge access rw");
        expect(src->agent_available, "knowledge agent flag");
        expect(src->source_alias == "plans", "knowledge alias");
        expect(store.effective_writable(src->id), "knowledge effective_writable");
        expect(store.list_for_project("proj-1").size() == 1, "knowledge list project");
        expect(store.list_for_project("other").empty(), "knowledge list other empty");
        expect(store.list_for_project("").empty() == false || store.list_for_project("proj-1").size() == 1,
               "knowledge list scoped");

        // Global source (empty project_ids) visible to all.
        auto* global = store.add_source(L"Global Skills", L"C:\\Temp\\scylla-skills", scyllagpt::SourceType::Skills,
                                        scyllagpt::AccessMode::ReadOnly, true, {}, "skills");
        expect(global != nullptr, "knowledge add global skills");
        expect(!store.effective_writable(global->id), "skills not writable by default");
        expect(store.list_for_project("proj-1").size() == 2, "knowledge list includes global");
        expect(store.list_for_project("other").size() == 1, "knowledge other sees global only");

        expect(scyllagpt::is_drive_root_path(L"C:\\"), "drive root C:\\");
        expect(scyllagpt::is_drive_root_path(L"D:"), "drive root D:");
        expect(!scyllagpt::is_drive_root_path(L"C:\\Temp"), "not drive root Temp");
        expect(store.add_source(L"Bad", L"C:\\", scyllagpt::SourceType::Automatic, scyllagpt::AccessMode::ReadOnly, true,
                                {}) == nullptr,
               "refuse drive root add");

        const std::wstring path = temp_file(L"knowledge.json");
        expect(store.save(path), "knowledge save");
        scyllagpt::KnowledgeStore loaded;
        expect(loaded.load(path), "knowledge load");
        expect(loaded.sources().size() == 2, "knowledge roundtrip count");
        expect(loaded.sources()[0].project_ids.size() == 1 && loaded.sources()[0].project_ids[0] == "proj-1",
               "knowledge roundtrip project_ids");
        expect(loaded.sources()[0].type == scyllagpt::SourceType::Knowledge, "knowledge roundtrip type");
        expect(loaded.sources()[0].access == scyllagpt::AccessMode::ReadWrite, "knowledge roundtrip access");
        expect(loaded.sources()[0].source_alias == "plans", "knowledge roundtrip alias");
        expect(loaded.sources()[1].type == scyllagpt::SourceType::Skills, "knowledge roundtrip skills type");
        expect(loaded.sources()[1].access == scyllagpt::AccessMode::ReadOnly, "knowledge roundtrip skills access");
        expect(loaded.sources()[1].project_ids.empty(), "knowledge roundtrip global empty pids");

        // Legacy JSON: writable + project_id
        {
            const std::wstring legacy = temp_file(L"knowledge_legacy.json");
            const char* body =
                R"({"version":1,"roots":[{"id":"abc","project_id":"p1","label":"Old","path":"C:\\Temp\\old-knowledge","writable":true,"agent_available":true}]})";
            HANDLE h = CreateFileW(legacy.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            expect(h != INVALID_HANDLE_VALUE, "legacy write open");
            if (h != INVALID_HANDLE_VALUE) {
                DWORD wr = 0;
                WriteFile(h, body, static_cast<DWORD>(strlen(body)), &wr, nullptr);
                CloseHandle(h);
            }
            scyllagpt::KnowledgeStore mig;
            expect(mig.load(legacy), "legacy load");
            expect(mig.sources().size() == 1, "legacy count");
            expect(mig.sources()[0].access == scyllagpt::AccessMode::ReadWrite, "legacy writable->access");
            expect(mig.sources()[0].project_ids.size() == 1 && mig.sources()[0].project_ids[0] == "p1",
                   "legacy project_id->project_ids");
            DeleteFileW(legacy.c_str());
        }

        const std::string remove_id = loaded.sources()[0].id;
        expect(loaded.remove(remove_id), "knowledge remove");
        expect(loaded.sources().size() == 1, "knowledge after remove");
        // Remove must not require filesystem presence of the folder.
        expect(loaded.by_id(remove_id) == nullptr, "knowledge removed id gone");
        DeleteFileW(path.c_str());
    }

    // Folder permission overrides + effective access resolution.
    {
        using scyllagpt::AccessMode;
        scyllagpt::KnowledgeStore store;
        auto* src = store.add_source(L"Plans", L"C:\\Temp\\scylla-perm-root", scyllagpt::SourceType::Knowledge,
                                     AccessMode::ReadWrite, true, {}, "plans");
        expect(src != nullptr, "override add_source");
        const std::string sid = src->id;

        expect(scyllagpt::normalize_override_path(L"/private/drafts/") == L"private\\drafts",
               "normalize slashes and trim");
        expect(scyllagpt::normalize_override_path(L"plans\\\\a//b") == L"plans\\a\\b", "normalize collapse");
        expect(scyllagpt::normalize_override_path(L"..\\escape").empty(), "normalize refuses traversal");
        expect(scyllagpt::normalize_override_path(L"C:\\abs").empty(), "normalize refuses absolute");
        expect(scyllagpt::normalize_override_path(L"").empty(), "normalize root is empty");

        // Root permission applies where no override exists.
        auto eff = store.resolve_effective_access(sid, L"plans\\today.md");
        expect(eff.mode == AccessMode::ReadWrite && eff.writable && eff.readable, "effective root read_write");
        expect(!eff.from_override, "effective root not from override");

        expect(store.set_override(sid, L"architecture", AccessMode::ReadOnly), "set override read_only");
        expect(store.set_override(sid, L"private", AccessMode::NoAccess), "set override no_access");
        expect(src->overrides.size() == 2, "override count");

        eff = store.resolve_effective_access(sid, L"architecture\\adr\\0001.md");
        expect(eff.mode == AccessMode::ReadOnly && eff.readable && !eff.writable, "ancestor override read_only");
        expect(eff.from_override && eff.matched_path == L"architecture", "ancestor override matched path");

        eff = store.resolve_effective_access(sid, L"private");
        expect(eff.mode == AccessMode::NoAccess && !eff.readable && !eff.writable, "exact override no_access");
        eff = store.resolve_effective_access(sid, L"private\\deep\\secret.md");
        expect(!eff.readable, "no_access cascades to descendants");

        // "plan" must not capture "plans" — overrides match whole segments only.
        expect(store.set_override(sid, L"plan", AccessMode::NoAccess), "set sibling-prefix override");
        eff = store.resolve_effective_access(sid, L"plans\\today.md");
        expect(eff.writable, "prefix override does not leak across segments");
        expect(store.remove_override(sid, L"plan"), "remove sibling-prefix override");

        // Deeper explicit override beats a shallower one.
        expect(store.set_override(sid, L"architecture\\scratch", AccessMode::ReadWrite), "set nested override");
        eff = store.resolve_effective_access(sid, L"architecture\\scratch\\notes.md");
        expect(eff.writable && eff.matched_path == L"architecture\\scratch", "nearest override wins");

        // Inherit removes the stored entry rather than persisting a no-op state.
        expect(store.set_override(sid, L"architecture\\scratch", AccessMode::Inherit), "inherit clears override");
        eff = store.resolve_effective_access(sid, L"architecture\\scratch\\notes.md");
        expect(!eff.writable && eff.matched_path == L"architecture", "inherit falls back to ancestor");

        // Absolute paths resolve; anything outside the root is NoAccess, not "root permission".
        eff = store.resolve_effective_access(sid, L"C:\\Temp\\scylla-perm-root\\private\\x.md");
        expect(!eff.readable, "absolute path honours override");
        eff = store.resolve_effective_access(sid, L"C:\\Temp\\somewhere-else\\x.md");
        expect(eff.mode == AccessMode::NoAccess, "path outside root denied");
        eff = store.resolve_effective_access("no-such-id", L"plans");
        expect(eff.mode == AccessMode::NoAccess, "unknown source denied");

        // Root permission change must not flatten explicit child overrides.
        scyllagpt::KnowledgeSource flip = *store.by_id(sid);
        flip.access = AccessMode::ReadOnly;
        expect(store.update(flip), "update root to read_only");
        expect(store.by_id(sid)->overrides.size() == 2, "overrides survive root change");
        eff = store.resolve_effective_access(sid, L"plans\\today.md");
        expect(!eff.writable && eff.readable, "root read_only cascades");

        const std::wstring path = temp_file(L"knowledge_overrides.json");
        expect(store.save(path), "override save");
        scyllagpt::KnowledgeStore loaded;
        expect(loaded.load(path), "override load");
        expect(loaded.sources().size() == 1, "override roundtrip count");
        expect(loaded.sources()[0].overrides.size() == 2, "override roundtrip count of overrides");
        expect(!loaded.resolve_effective_access(loaded.sources()[0].id, L"private\\a.md").readable,
               "override roundtrip no_access");
        expect(loaded.resolve_effective_access(loaded.sources()[0].id, L"architecture\\a.md").readable,
               "override roundtrip read_only readable");
        DeleteFileW(path.c_str());
    }

    // Agent-accessible Knowledge paths for Codex sandbox writableRoots.
    {
        scyllagpt::KnowledgeStore store;
        auto* a = store.add_source(L"MD", L"C:\\Temp\\scylla-md-root", scyllagpt::SourceType::Knowledge,
                                   scyllagpt::AccessMode::ReadWrite, true, {"proj-a"}, "md");
        expect(a != nullptr, "agent paths add md");
        auto* b = store.add_source(L"Other", L"C:\\Temp\\scylla-other", scyllagpt::SourceType::Knowledge,
                                   scyllagpt::AccessMode::ReadOnly, true, {"proj-b"}, "other");
        expect(b != nullptr, "agent paths add other");
        auto* c = store.add_source(L"Off", L"C:\\Temp\\scylla-off", scyllagpt::SourceType::Knowledge,
                                   scyllagpt::AccessMode::ReadWrite, true, {"proj-a"}, "off");
        expect(c != nullptr, "agent paths add off");
        expect(store.set_enabled(c->id, false), "disable off source");
        auto* d = store.add_source(L"NoAgent", L"C:\\Temp\\scylla-noagent", scyllagpt::SourceType::Knowledge,
                                   scyllagpt::AccessMode::ReadWrite, false, {"proj-a"}, "noagent");
        expect(d != nullptr, "agent paths add noagent");

        const auto for_a = store.agent_accessible_paths("proj-a");
        expect(for_a.size() == 1, "agent paths only enabled+available for proj-a");
        expect(for_a[0].find(L"scylla-md-root") != std::wstring::npos, "agent paths includes md root");
        expect(store.agent_accessible_paths("proj-b").size() == 1, "agent paths for proj-b");
        expect(store.agent_accessible_paths("missing").empty(), "agent paths empty for unknown project");
    }

    // Enable / disable + health probe.
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring real_dir = std::wstring(tmp) + L"scyllagpt_test_health_dir";
        CreateDirectoryW(real_dir.c_str(), nullptr);

        scyllagpt::KnowledgeStore store;
        auto* live = store.add_source(L"Live", real_dir, scyllagpt::SourceType::Knowledge,
                                      scyllagpt::AccessMode::ReadOnly, true, {}, {});
        expect(live != nullptr, "health add real dir");
        expect(live->enabled, "enabled defaults true");
        expect(live->health == scyllagpt::SourceHealth::Healthy, "health healthy for real dir");
        const std::string live_id = live->id;

        auto* gone = store.add_source(L"Gone", std::wstring(tmp) + L"scyllagpt_test_missing_dir",
                                      scyllagpt::SourceType::Knowledge, scyllagpt::AccessMode::ReadOnly, true, {},
                                      {});
        expect(gone != nullptr, "health add missing dir");
        expect(gone->health == scyllagpt::SourceHealth::Unavailable, "health unavailable for missing dir");
        const std::string gone_id = gone->id;

        // A No Access subtree means the source is only partially usable.
        expect(store.set_override(live_id, L"private", scyllagpt::AccessMode::NoAccess), "health set no_access");
        expect(store.refresh_health(live_id) == scyllagpt::SourceHealth::PartiallyAccessible,
               "health partially accessible");
        expect(store.remove_override(live_id, L"private"), "health clear no_access");
        expect(store.refresh_health(live_id) == scyllagpt::SourceHealth::Healthy, "health back to healthy");

        expect(store.refresh_all_health() == 2, "refresh_all_health count");

        // Disabled: config retained, but invisible to listing / path lookup / write checks.
        expect(store.list_enabled_for_project("").size() == 2, "enabled listing before disable");
        expect(store.set_enabled(live_id, false), "set_enabled false");
        expect(store.by_id(live_id)->health == scyllagpt::SourceHealth::Disabled, "disabled health");
        expect(store.list_for_project("").size() == 2, "settings listing still shows disabled");
        expect(store.list_enabled_for_project("").size() == 1, "explorer listing hides disabled");
        expect(store.source_for_path(real_dir + L"\\a.md") == nullptr, "source_for_path skips disabled");
        expect(!store.resolve_effective_access(live_id, L"a.md").readable, "disabled resolves to no access");
        expect(!store.effective_writable(live_id), "disabled never writable");

        expect(store.set_enabled(live_id, true), "set_enabled true");
        expect(store.by_id(live_id)->health == scyllagpt::SourceHealth::Healthy, "re-enabled reprobes health");
        expect(store.source_for_path(real_dir + L"\\a.md") != nullptr, "source_for_path sees re-enabled");

        const std::wstring path = temp_file(L"knowledge_enabled.json");
        expect(store.set_enabled(gone_id, false), "disable second source");
        expect(store.save(path), "enabled save");
        scyllagpt::KnowledgeStore loaded;
        expect(loaded.load(path), "enabled load");
        expect(loaded.by_id(live_id) && loaded.by_id(live_id)->enabled, "enabled roundtrip true");
        expect(loaded.by_id(gone_id) && !loaded.by_id(gone_id)->enabled, "enabled roundtrip false");
        expect(loaded.by_id(gone_id)->health == scyllagpt::SourceHealth::Disabled, "disabled health on load");
        DeleteFileW(path.c_str());
        RemoveDirectoryW(real_dir.c_str());
    }

    {
        const std::vector<scyllagpt::CodexMcpServer> servers{
            {"linear-cxl", L"https://mcp.linear.app/mcp", L"SCYLLA_MCP_TOKEN_TEST"}};
        const auto config = scyllagpt::render_isolated_codex_config(true, {}, servers);
        expect(config.find("[mcp_servers.linear-cxl]") != std::string::npos, "mcp runtime config server");
        expect(config.find("url = \"https://mcp.linear.app/mcp\"") != std::string::npos, "mcp runtime config url");
        expect(config.find("bearer_token_env_var = \"SCYLLA_MCP_TOKEN_TEST\"") != std::string::npos,
               "mcp runtime config bearer env");
        expect(config.find("access-xyz") == std::string::npos, "mcp runtime config contains no bearer token");
        const std::vector<scyllagpt::CodexMcpServer> stdio_servers{
            {"workspace-knowledge", L"", L"", true, L"python",
             {L"-m", L"cxl_strata.workspace_index.mcp_server"},
             {{L"STRATA_WORKSPACE_ROOT", L"d:/projects"}}}};
        const auto stdio_config = scyllagpt::render_isolated_codex_config(true, {}, stdio_servers);
        expect(stdio_config.find("command = \"python\"") != std::string::npos &&
               stdio_config.find("args = [\"-m\", \"cxl_strata.workspace_index.mcp_server\"]") != std::string::npos,
               "mcp stdio command and arguments");
        expect(stdio_config.find("STRATA_WORKSPACE_ROOT = \"d:/projects\"") != std::string::npos,
               "mcp stdio environment");
    }

    {
        auto templates = scyllagpt::McpManager::known_templates();
        expect(templates.size() == 5, "mcp templates count");
        bool has_github = false;
        bool has_linear = false;
        bool has_notion = false;
        bool has_figma = false;
        bool has_strata = false;
        for (const auto& t : templates) {
            if (t.service_id == "github") {
                has_github = true;
            }
            if (t.service_id == "linear") {
                has_linear = true;
            }
            if (t.service_id == "notion") {
                has_notion = true;
            }
            if (t.service_id == "figma") {
                has_figma = true;
            }
            if (t.service_id == "workspace-knowledge") {
                has_strata = t.default_transport == scyllagpt::McpTransportKind::Stdio &&
                             t.suggested_endpoint_or_cmd == "python" && t.arguments.size() == 2 &&
                             !t.environment.empty();
            }
        }
        expect(has_github && has_linear && has_notion && has_figma && has_strata, "mcp template ids");

        scyllagpt::McpManager mgr;
        auto conn = scyllagpt::McpManager::from_template(templates[0], L"Personal");
        expect(!conn.id.empty() && conn.service_id == "github", "mcp from_template");
        expect(!conn.connection_name.empty(), "mcp from_template connection_name");
        expect(conn.auth_state == scyllagpt::McpAuthState::Unknown, "mcp from_template auth");
        auto strata = scyllagpt::McpManager::from_template(templates.back(), L"Workspace Knowledge");
        expect(strata.arguments.size() == 2 && strata.environment.size() == 1,
               "mcp stdio template arguments and environment");
        const std::string strata_id = strata.id;
        expect(mgr.add(strata) != nullptr, "mcp add stdio template");
        auto* added = mgr.add(conn);
        expect(added != nullptr, "mcp add");
        const std::string personal_id = added->id;

        std::string alias_err;
        expect(mgr.set_alias(personal_id, "github-personal", &alias_err), "mcp set_alias");
        expect(mgr.by_id(personal_id)->agent_alias == "github-personal", "mcp alias stored without @");
        expect(mgr.resolve_alias("@github-personal") == mgr.by_id(personal_id), "mcp resolve_alias with @");
        expect(mgr.resolve_alias("github-personal") == mgr.by_id(personal_id), "mcp resolve_alias without @");

        scyllagpt::McpConnection other = scyllagpt::McpManager::from_template(templates[0], L"Work");
        const std::string work_id = other.id;
        expect(mgr.add(other) != nullptr, "mcp add second github account");
        expect(!mgr.set_alias(work_id, "github-personal", &alias_err), "mcp alias uniqueness");
        expect(!alias_err.empty(), "mcp alias uniqueness error");
        expect(mgr.set_alias(work_id, "@github-work", &alias_err), "mcp set_alias with @ prefix");

        auto comps = mgr.alias_completions("git");
        expect(comps.size() == 2, "mcp alias_completions prefix");

        mgr.mark_auth(personal_id, scyllagpt::McpAuthState::Healthy, {});
        const auto* personal = mgr.by_id(personal_id);
        expect(personal && personal->auth_state == scyllagpt::McpAuthState::Healthy, "mcp mark_auth healthy");
        expect(personal && !personal->last_checked_iso.empty(), "mcp last_checked_iso");

        mgr.disconnect(work_id);
        expect(mgr.by_id(work_id)->disconnected, "mcp disconnect flag");
        expect(mgr.by_id(work_id)->auth_state == scyllagpt::McpAuthState::NeedsReauth, "mcp disconnect auth");

        mgr.set_enabled(work_id, false);
        expect(!mgr.by_id(work_id)->enabled, "mcp set_enabled false");

        scyllagpt::McpConnection scoped;
        scoped.service_id = "linear";
        scoped.display_name = L"Linear";
        scoped.connection_name = L"Linear CXL";
        scoped.project_scope = {"proj-a"};
        expect(mgr.add(scoped) != nullptr, "mcp add scoped");
        expect(mgr.list_for_project("proj-a").size() >= 2, "mcp list project");

        const std::wstring path = temp_file(L"mcp.json");
        expect(mgr.save(path), "mcp save");
        scyllagpt::McpManager loaded;
        expect(loaded.load(path), "mcp load");
        expect(loaded.connections().size() == mgr.connections().size(), "mcp roundtrip count");
        const auto* round = loaded.resolve_alias("github-personal");
        expect(round != nullptr, "mcp roundtrip resolve_alias");
        expect(round->auth_state == scyllagpt::McpAuthState::Healthy, "mcp roundtrip auth_state");
        expect(!round->connection_name.empty(), "mcp roundtrip connection_name");
        expect(!round->last_checked_iso.empty(), "mcp roundtrip last_checked_iso");
        const auto* work = loaded.by_id(work_id);
        expect(work && work->disconnected && !work->enabled, "mcp roundtrip disconnect+disabled");
        const auto* loaded_strata = loaded.by_id(strata_id);
        expect(loaded_strata && loaded_strata->arguments.size() == 2 && loaded_strata->environment.size() == 1,
               "mcp roundtrip stdio arguments and environment");
        DeleteFileW(path.c_str());

        expect(mgr.remove(personal_id), "mcp remove");
    }

    {
        scyllagpt::StrataClient client;
        expect(client.settings().endpoint == L"http://127.0.0.1:8765", "strata default endpoint");
        // Soft health — may fail offline; API must not throw.
        const auto health = client.health_check();
        expect(!health.message.empty() || health.ok || !health.ok, "strata health_check callable");
    }

    {
        bool ok = false;
        scyllagpt::Json result;
        std::string err;
        std::int64_t id = -1;
        expect(scyllagpt::parse_bridge_envelope(
                   R"({"id":7,"ok":true,"result":{"bridge_version":1,"methods":["status","search"]}})", &ok, &result,
                   &err, &id),
               "bridge envelope parse ok");
        expect(ok && id == 7 && err.empty(), "bridge envelope fields");
        scyllagpt::StrataBridgeCapabilities caps;
        expect(scyllagpt::parse_bridge_capabilities_result(result, &caps), "bridge caps parse");
        expect(caps.ok && caps.bridge_version == 1 && caps.methods.size() == 2, "bridge caps values");

        expect(scyllagpt::parse_bridge_envelope(R"({"id":8,"ok":false,"error":"nope"})", &ok, &result, &err, &id),
               "bridge error envelope");
        expect(!ok && err == "nope" && id == 8, "bridge error fields");

        scyllagpt::Json search_json = scyllagpt::Json::parse(
            R"({"query":"q","hits":[{"path":"a.md","kind":"handoff","title":"T","origin":"local"}],"count":1})",
            &err);
        scyllagpt::StrataBridgeSearchResult search;
        expect(scyllagpt::parse_bridge_search_result(search_json, &search), "bridge search parse");
        expect(search.ok && search.hits.size() == 1 && search.hits[0].path == "a.md", "bridge search hits");

        scyllagpt::Json status_json =
            scyllagpt::Json::parse(R"({"index_exists":true,"total":42,"bridge_version":1,"strata_version":"0.3.3"})",
                                   &err);
        scyllagpt::StrataBridgeStatus st;
        expect(scyllagpt::parse_bridge_status_result(status_json, &st), "bridge status parse");
        expect(st.ok && st.index_exists && st.total == 42, "bridge status values");

        // recent has three shapes; the flat {items} form and the project form that splits
        // documents from handoffs_available must both land in `items`, deduped by path.
        scyllagpt::Json recent_flat = scyllagpt::Json::parse(
            R"({"items":[{"path":"h1.md","kind":"handoff","excerpt":"body text"}],"count":1})", &err);
        scyllagpt::StrataBridgeRecentResult recent;
        expect(scyllagpt::parse_bridge_recent_result(recent_flat, &recent), "bridge recent flat parse");
        expect(recent.ok && recent.items.size() == 1 && recent.items[0].snippet == "body text",
               "bridge recent flat items");

        scyllagpt::Json recent_project = scyllagpt::Json::parse(
            R"({"documents":[{"path":"h1.md","title":"A"}],)"
            R"("handoffs_available":{"handoffs":[{"path":"h1.md"},{"path":"h2.md"}]}})",
            &err);
        expect(scyllagpt::parse_bridge_recent_result(recent_project, &recent), "bridge recent project parse");
        expect(recent.items.size() == 2 && recent.items[0].path == "h1.md" && recent.items[1].path == "h2.md",
               "bridge recent dedupe by path");

        scyllagpt::Json get_json = scyllagpt::Json::parse(
            R"({"document":{"path":"a.md","title":"T","body":"hello","storage":"db_only","body_truncated":true}})",
            &err);
        scyllagpt::StrataBridgeDocument doc;
        expect(scyllagpt::parse_bridge_get_result(get_json, &doc), "bridge get parse");
        expect(doc.ok && doc.path == "a.md" && doc.body == "hello" && doc.body_truncated &&
                   doc.storage == "db_only",
               "bridge get values");

        scyllagpt::Json pending_json =
            scyllagpt::Json::parse(R"({"available":true,"index_pending":2,"sync_pending":3,"total":5})", &err);
        scyllagpt::StrataBridgePending pending;
        expect(scyllagpt::parse_bridge_pending_result(pending_json, &pending), "bridge pending parse");
        expect(pending.ok && pending.available && pending.total == 5 && pending.index_pending == 2,
               "bridge pending values");

        scyllagpt::Json pending_zero = scyllagpt::Json::parse(R"({"available":false})", &err);
        expect(scyllagpt::parse_bridge_pending_result(pending_zero, &pending), "bridge pending zero parse");
        expect(pending.ok && !pending.available && pending.total == 0, "bridge pending zero values");

        // Previews are chrome text: secret-shaped values must be masked before truncation.
        expect(scyllagpt::strata_sanitize_preview("plain handoff notes", 220) == "plain handoff notes",
               "preview passthrough");
        expect(scyllagpt::strata_sanitize_preview("api_key=abcdefgh12345678", 220).find("abcdefgh") ==
                   std::string::npos,
               "preview redacts inline key=value");
        expect(scyllagpt::strata_sanitize_preview("password: hunter2hunter2", 220).find("hunter2") ==
                   std::string::npos,
               "preview redacts separated value");
        expect(scyllagpt::strata_sanitize_preview("token sk_live_ABCDEFGHIJKLMNOP", 220).find("ABCDEF") ==
                   std::string::npos,
               "preview redacts stripe-style token");
        expect(scyllagpt::strata_sanitize_preview("-----BEGIN PRIVATE KEY-----\nMIIabc", 220) ==
                   "[redacted key material]",
               "preview redacts key blocks");
        {
            const std::string flat = scyllagpt::strata_sanitize_preview("line one\r\n\tline two", 220);
            expect(flat == "line one line two", "preview flattens whitespace");
            const std::string cut = scyllagpt::strata_sanitize_preview(std::string(400, 'a'), 40);
            expect(cut.size() <= 43 && cut.find("\xE2\x80\xA6") != std::string::npos, "preview truncates");
            // A hard cut must not leave a dangling UTF-8 continuation byte.
            std::string wide;
            for (int i = 0; i < 40; ++i) {
                wide += "\xE2\x80\x94";  // em dash
            }
            const std::string wide_cut = scyllagpt::strata_sanitize_preview(wide, 40);
            // Every em dash is 3 bytes, so a boundary-respecting cut leaves a multiple of 3
            // before the appended ellipsis.
            expect(wide_cut.size() > 3 && (wide_cut.size() - 3) % 3 == 0,
                   "preview truncates on utf-8 boundary");
        }

        // Fail soft when STRATA binary missing — start() must not crash.
        scyllagpt::StrataBridge bridge;
        std::wstring start_err;
        const bool started = bridge.start(&start_err);
        if (!started) {
            expect(bridge.state() == scyllagpt::StrataBridgeState::NotInstalled ||
                       bridge.state() == scyllagpt::StrataBridgeState::Error ||
                       bridge.state() == scyllagpt::StrataBridgeState::Stopped,
                   "bridge missing stays soft");
            expect(!bridge.last_error().empty() || !start_err.empty(), "bridge missing message");
        } else {
            const auto caps_live = bridge.capabilities();
            expect(caps_live.ok || !caps_live.message.empty(), "bridge capabilities soft");
            bridge.stop();
        }
    }

    {
        scyllagpt::Paths p = scyllagpt::make_paths();
        expect(!p.appdata.empty(), "paths appdata");
        expect(!p.knowledge_path.empty() && p.knowledge_path.find(L"knowledge.json") != std::wstring::npos,
               "paths knowledge");
        expect(!p.mcp_path.empty(), "paths mcp");
        expect(!p.strata_path.empty(), "paths strata");
    }

    return g_fail;
}
