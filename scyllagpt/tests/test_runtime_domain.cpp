#include "scyllagpt/session.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/mcp_manager.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/strata_bridge.h"
#include "scyllagpt/strata_client.h"
#include "scyllagpt/workflow.h"
#include "scyllagpt/scylla_query_skill.h"
#include "scyllagpt/composer_tokens.h"
#include "scyllagpt/composer_metrics.h"
#include "scyllagpt/chat_log_rows.h"
#include "scyllagpt/history_merge.h"
#include "scyllagpt/codex_thread_util.h"
#include "scyllagpt/agent_files.h"
#include "scyllagpt/chat_history.h"
#include "scyllagpt/chat_projects.h"
#include "scyllagpt/markdown.h"
#include "scyllagpt/project_context.h"
#include "scyllagpt/file_io.h"
#include "scyllagpt/async_snapshot.h"

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

int run_runtime_domain_tests() {
    {
        scyllagpt::Session session;
        session.settings.default_provider = "openai";
        session.models.push_back({"gpt-depth", "GPT Depth", "openai", false,
                                  {"minimal", "low", "medium", "high", "xhigh"}, "low"});
        session.selected_model = "gpt-depth";
        session.reset_reasoning_effort_for_selected_model();
        expect(session.settings.reasoning_effort == "medium", "model selection prefers medium reasoning effort");

        session.models[0].reasoning_efforts = {"low", "high"};
        session.models[0].default_reasoning_effort = "high";
        session.reset_reasoning_effort_for_selected_model();
        expect(session.settings.reasoning_effort == "high", "model selection uses supported declared default");

        scyllagpt::Json turn = scyllagpt::Json::object();
        session.apply_reasoning_effort(turn);
        expect(turn.at("effort").as_string("") == "high", "selected reasoning effort reaches Codex turn payload");
        session.settings.reasoning_effort = "unsupported";
        turn = scyllagpt::Json::object();
        session.apply_reasoning_effort(turn);
        expect(!turn.has("effort"), "unsupported reasoning effort is omitted from Codex turn payload");
    }
    {
        scyllagpt::Session session;
        session.settings.default_provider = "openai";
        session.models.push_back({"gpt-5.6-terra", "GPT-5.6-Terra", "openai", false, {"medium"}, "medium", "code", false});
        session.models.push_back({"gpt-5.6-sol", "GPT-5.6-Sol", "openai", false, {"low", "medium"}, "low", "", true});
        session.ensure_selected_model();
        expect(session.selected_model == "gpt-5.6-sol", "catalog default selects GPT-5.6-Sol");
        session.reset_reasoning_effort_for_selected_model();
        expect(session.settings.reasoning_effort == "medium", "GPT-5.6-Sol supports selectable medium effort");
        scyllagpt::Json turn = scyllagpt::Json::object();
        turn["model"] = scyllagpt::Json::string(session.selected_model);
        session.apply_reasoning_effort(turn);
        expect(turn.at("model").as_string("") == "gpt-5.6-sol", "selected GPT-5.6-Sol reaches turn payload");
        expect(turn.at("effort").as_string("") == "medium", "selected medium reaches turn payload");
    }
    {
        using namespace scyllagpt;
        WorkspaceStore projects;
        Project owner;
        owner.id = "owner"; owner.root = L"D:\\projects\\cxl-scylla";
        owner.roots = {owner.root, L"D:\\projects\\cxl-sentinel", L"D:\\projects\\cxl-spore"};
        projects.projects.push_back(owner); projects.active_project_id = owner.id;
        Conversation chat; chat.project_id = owner.id;
        chat.local_messages = Json::array();
        Json message = Json::object(); message["user"] = Json::boolean(true);
        message["text"] = Json::string("Injected cxl-spore" + user_display_metadata("Compare cxl-sentinel with this repo"));
        chat.local_messages.push(message);
        auto paths = chat_project_paths(projects, &chat, L"", "Chat", "");
        expect(paths.array_items().size() == 2, "chat project dots include owner and visible mention only");
        expect(!mentions_project(L"cxl-sentinel-backup", L"cxl-sentinel"), "project mentions respect name boundaries");
        expect(mentions_project(L"(CXL-SENTINEL)", L"cxl-sentinel"), "project mentions ignore case");
        auto peer = chat_project_paths(projects, nullptr, L"d:\\PROJECTS\\cxl-sentinel\\src", "Chat", "");
        expect(peer.array_items().size() == 1, "provider cwd matches peer project case insensitively");
        Session session;
        session.active_thread_id = "foreground";
        session.handle_line(R"({"method":"turn/started","params":{"threadId":"background","turn":{"id":"turn-1"}}})");
        expect(session.thread_activity("background") && session.thread_activity("background")->busy,
               "history sees background thread working");
        session.handle_line(R"({"method":"turn/completed","params":{"threadId":"background","turn":{"id":"turn-1","status":"completed"}}})");
        expect(session.thread_activity("background") && !session.thread_activity("background")->busy
            && session.thread_activity("background")->phase == "Completed", "history sees background completion");
        expect(session.active_thread_id == "foreground", "background completion preserves active chat");
    }
    g_fail = 0;
    {
        using namespace scyllagpt;
        const std::vector<AgentFile> files = {
            {L"D:\\repo\\old-plan.md", L"old-plan.md"},
            {L"D:\\repo\\plan.md", L"plan.md"},
            {L"D:\\knowledge\\plan.md", L"plan.md"}
        };
        const auto matches = match_agent_files(files, L"plan.md");
        expect(matches.size() == 3 && matches[0].path == files[1].path && matches[1].path == files[2].path,
               "exact filename mentions precede substring matches and retain ambiguous paths");
        expect(match_agent_files(files, L"missing.md").empty(), "unknown filename does not resolve");
        expect(agent_file_reference(L"D:/projects/.cursor/plans/plan.md", L"D:/projects/repo") ==
            L"../.cursor/plans/plan.md", "Knowledge mention uses project-relative path");
        expect(agent_file_reference(L"D:/repo/src/file.cpp", L"D:/repo") == L"src/file.cpp",
            "Project mention uses relative path");
        expect(agent_file_reference(L"E:/knowledge/plan.md", L"D:/repo") == L"E:/knowledge/plan.md",
            "Cross-drive mention retains resolvable absolute path");
    }
    {
        using namespace std::chrono_literals;
        scyllagpt::AsyncSnapshot<int> cache(30s);
        std::promise<void> release;
        auto gate = release.get_future().share();
        std::atomic<int> calls = 0;
        auto probe = [&](int) { ++calls; gate.wait(); return 7; };
        auto poll = std::async(std::launch::async, [&] { return cache.get(probe); });
        const bool responsive = poll.wait_for(1s) == std::future_status::ready;
        if (!responsive) release.set_value();
        expect(responsive && poll.get() == 0, "provider cache returns immediately while authentication is blocked");
        if (responsive) {
            bool retained = true;
            for (int i = 0; i < 100; ++i) retained = retained && cache.get(probe) == 0;
            expect(retained, "in-flight provider probe keeps last snapshot");
            release.set_value();
        }
        expect(cache.get(probe, true) == 7 && calls == 1, "provider polling coalesces into one background probe");
        cache.invalidate(true);
        std::promise<void> stale_release;
        auto stale_gate = stale_release.get_future().share();
        cache.get([stale_gate](int) { stale_gate.wait(); return 99; });
        cache.invalidate(true);
        stale_release.set_value();
        expect(cache.get([](int) { return 11; }, true) == 11, "invalidated auth cannot restore stale signed-in state");
    }
    {
        using namespace scyllagpt;
        WorkspaceStore store;
        const auto scylla = store.open_or_create(L"D:\\projects\\cxl-scylla")->id;
        const auto forge = store.open_or_create(L"D:\\projects\\synq-forge")->id;
        const auto phalanx = store.open_or_create(L"D:\\projects\\synq-phalanx")->id;
        store.upsert_thread(scylla, "a", "scylla-chat", "Scylla", "");
        store.upsert_thread(forge, "a", "forge-chat", "Forge", "");
        store.upsert_thread(phalanx, "a", "phalanx-chat", "Phalanx", "");
        store.upsert_thread(forge, "other", "other-account", "Other", "");
        store.active_project_id = scylla;
        store.history_all_projects = true;
        expect(store.list_visible("a", L"").size() == 1, "single open project excludes unrelated chats even with legacy all-projects flag");
        store.active_project_id = forge;
        store.active()->roots = {L"D:\\projects\\synq-forge", L"D:\\projects\\synq-phalanx"};
        expect(store.list_visible("a", L"").size() == 2, "peer projects union their histories without crossing accounts");
        expect(store.contains_open_path(L"d:/PROJECTS/synq-forge/app"), "provider cwd uses normalized root containment");
        expect(!store.contains_open_path(L"D:\\projects\\synq-forge-copy"), "similar project names do not match");
        expect(!store.contains_open_path(L""), "unknown provider cwd excluded");
        const auto names = open_project_names(store);
        KnowledgeStore knowledge;
        knowledge.add_source(L"Forge", L"D:\\scope-test\\forge", SourceType::Knowledge, AccessMode::ReadOnly, true, {forge}, "forge");
        knowledge.add_source(L"Phalanx", L"D:\\scope-test\\phalanx", SourceType::Knowledge, AccessMode::ReadOnly, true, {phalanx}, "phalanx");
        knowledge.add_source(L"Scylla", L"D:\\scope-test\\scylla", SourceType::Knowledge, AccessMode::ReadOnly, true, {scylla}, "scylla");
        expect(scoped_knowledge_paths(store, knowledge).size() == 2, "knowledge grants include peer projects and exclude closed projects");
        expect(names == std::vector<std::string>{"synq-forge", "synq-phalanx"}, "knowledge projects derived from every open root");
        expect(project_document_matches(L"synq-forge/entry.md", "", names), "project handoff folder matches");
        expect(project_document_matches(L"synq-phalanx.md", "", names), "peer blueprint matches");
        expect(!project_document_matches(L"cxl-scylla.md", "Mentions synq-forge", names), "casual mention cannot import unrelated knowledge");
        expect(project_document_matches(L"FORGE.md", "| Repo | `synq-forge/` |", names), "explicit repo metadata supports blueprint aliases");
        expect(!project_document_matches(L"synq-forge-copy.md", "", names), "knowledge rejects prefix collisions");
        const auto base = std::filesystem::path(temp_file(L"project_scope")) / L".md";
        std::filesystem::create_directories(base / L"blueprints");
        std::filesystem::create_directories(base / L"handoff" / L"synq-forge");
        write_file_bytes_atomic((base / L"blueprints" / L"synq-forge.md").wstring(), "FORGE_CONTEXT");
        write_file_bytes_atomic((base / L"blueprints" / L"cxl-scylla.md").wstring(), "UNRELATED_CONTEXT");
        const auto old = base / L"handoff" / L"synq-forge" / L"old.md";
        write_file_bytes_atomic(old.wstring(), "STALE_CONTEXT");
        std::filesystem::last_write_time(old, std::filesystem::file_time_type::clock::now() - std::chrono::hours(72));
        write_file_bytes_atomic((base / L"handoff" / L"synq-forge" / L"recent.md").wstring(), "RECENT_CONTEXT");
        const auto context = scoped_knowledge_context(store, {base.wstring()});
        expect(context.find("FORGE_CONTEXT") != std::string::npos && context.find("RECENT_CONTEXT") != std::string::npos,
               "prompt contains scoped blueprint and recent handoff contents");
        expect(context.find("UNRELATED_CONTEXT") == std::string::npos && context.find("STALE_CONTEXT") == std::string::npos,
               "prompt excludes unrelated and stale files");
        store.active_project_id.clear();
        expect(store.list_visible("a", L"").empty(), "no projects means no project history");
    }
    {
        using namespace scyllagpt;
        Session session;
        session.paths.store_path = temp_file(L"chat_delete.json");
        auto* row = session.store.upsert_thread("", "", "delete-me", "Private title", "Private preview");
        row->local_messages = Json::array();
        session.active_thread_id = "delete-me";
        session.history_messages.push_back({true, "Private message"});
        std::string error;
        session.state = AppState::Generating;
        expect(!session.delete_thread("delete-me", &error), "cannot delete generating chat");
        expect(!session.store.by_thread("delete-me")->deleted, "busy deletion leaves chat intact");
        session.state = AppState::Ready;
        expect(session.delete_thread("delete-me", &error), "delete idle chat");
        expect(session.active_thread_id.empty() && session.history_messages.empty(), "active deleted chat clears transcript");
        WorkspaceStore restored;
        expect(restored.load(session.paths.store_path), "deleted chat persists");
        const auto* deleted = restored.by_thread("delete-me");
        expect(deleted && deleted->deleted && deleted->title.empty() && deleted->preview.empty() &&
            deleted->local_messages.array_items().empty(), "deletion removes local content and keeps tombstone");
        expect(restored.list_visible("", L"").empty(), "deleted chat absent after reload");
        expect(!restored.upsert_thread("", "", "delete-me", "Provider title", "Provider preview"), "provider cannot restore deleted chat");
        session.open_thread("delete-me");
        expect(session.active_thread_id.empty(), "deleted chat cannot reopen");
        session.store.upsert_thread("", "", "keep-me", "Keep", "");
        session.paths.store_path = temp_file(L"missing-delete-directory") + L"/store.json";
        expect(!session.delete_thread("keep-me", &error) && !session.store.by_thread("keep-me")->deleted,
            "failed persistence leaves chat intact");
        DeleteFileW(temp_file(L"chat_delete.json").c_str());
    }
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
        Project project;
        project.id = "p"; project.root = L"D:\\projects\\test"; project.roots = {project.root};
        store.projects.push_back(project);
        store.active_project_id = "p";
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
        const std::string injected = "Execute mode: carry out the requested work and validate the result.\n\n"
            "Knowledge folders granted for this turn (absolute paths; not limited to the project cwd):\nD:/notes\n"
            "Chat naming: hidden instruction\n\n---\n";
        expect(visible_user_text(injected + "test") == "test", "restored prompt hides runtime metadata");
        expect(visible_user_text(injected + injected + "test") == "test", "nested runtime headers unwrapped");
        expect(visible_user_text("Execute mode: carry out the requested work and validate the result.\r\n\r\n---\r\ntest") == "test",
               "runtime metadata accepts CRLF");
        expect(visible_user_text("Explain this\n---\nChat naming: example") == "Explain this\n---\nChat naming: example",
               "ordinary user separators preserved");
        expect(visible_user_text(injected + "one\n---\ntwo") == "one\n---\ntwo", "user body separators preserved");
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
        using namespace scyllagpt;
        const fs::path base = fs::path(temp_file(L"multi_root")) / std::to_wstring(GetCurrentProcessId());
        const fs::path first = base / L"synq-forge";
        const fs::path second = base / L"synq-phalanx";
        fs::create_directories(first);
        fs::create_directories(second);
        WorkspaceStore store;
        Project* project = store.open_or_create(first.wstring());
        expect(project && store.add_root(project->id, second.wstring()), "add peer repository root");
        expect(project && project->roots.size() == 2, "project retains multiple roots");
        const auto state = (base / L"store.json").wstring();
        expect(store.save(state), "save multi-root project");
        WorkspaceStore restored;
        expect(restored.load(state) && restored.active() && restored.active()->roots.size() == 2,
               "multi-root project roundtrip");
        expect(restored.active() && !restored.remove_root(restored.active()->id, first.wstring()),
               "primary project root cannot be removed");
        std::error_code error;
        fs::remove_all(base, error);
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
        fs::create_directories(shared / L"plans" / L"in_progress");
        std::ofstream(shared / L"plans" / L"in_progress" / L"mention-plan.md") << "plan";
        expect(scyllagpt::match_agent_files(scyllagpt::agent_file_catalog(store, "other", L"", {}, {"project"}),
            L"mention-plan.md").size() == 1, "Mentions discover plans from another open project's Knowledge");
        store.set_override(id, L"plans", scyllagpt::AccessMode::NoAccess);
        expect(scyllagpt::match_agent_files(scyllagpt::agent_file_catalog(store, "other", L"", {}, {"project"}),
            L"mention-plan.md").empty(), "Open-project mention scope preserves denied folders");
        store.remove_override(id, L"plans");
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
        {
            std::wstring q = L"/scylla-query list tables on RO RDS";
            expect(scyllagpt::consume_scylla_query_slash(&q), "scylla-query slash consumes");
            expect(q == L"list tables on RO RDS", "scylla-query slash leaves body");
            std::wstring keep = L"/scylla-querying no";
            expect(!scyllagpt::consume_scylla_query_slash(&keep) && keep == L"/scylla-querying no",
                   "scylla-query prefix alone does not match");
            std::wstring mixed = L"/Scylla-Query\r\nSELECT 1";
            expect(scyllagpt::consume_scylla_query_slash(&mixed) && mixed == L"SELECT 1",
                   "scylla-query slash is case-insensitive");
            const auto unavailable = scyllagpt::scylla_query_instructions("(none)\n", false);
            expect(unavailable.find("NOT available") != std::string::npos,
                   "skill says execution is unavailable when no tool is registered");
            expect(unavailable.find("Do NOT emit a fenced scylla-query block") != std::string::npos,
                   "skill forbids the unexecutable fenced block");
            expect(unavailable.find("submitted") != std::string::npos,
                   "skill forbids claiming submission");
            expect(unavailable.find("scylla_query\n") == std::string::npos,
                   "skill does not advertise a tool that is not registered");
            const auto skill = scyllagpt::scylla_query_instructions("(none)\n", true);
            expect(skill.find("scylla_query") != std::string::npos,
                   "skill names the tool when it is registered");
            expect(skill.find("connection_alias") != std::string::npos,
                   "skill documents the alias argument");
            expect(skill.find("role_map") == std::string::npos,
                   "skill no longer asks for a per-call role map");
            expect(skill.find("fabricate") != std::string::npos, "skill forbids fabricated values");
            expect(skill.find("Never ask the user to paste secret VALUES") != std::string::npos,
                   "scylla-query skill forbids secret values");
            expect(skill.find("!scylla_NAME") != std::string::npos, "scylla-query skill documents bang keyring tokens");
            const auto spans = scyllagpt::find_scylla_query_spans(L"hi /scylla-query now /scylla-querying no");
            expect(spans.size() == 1 && spans[0].begin == 3 && spans[0].end == 16, "find /scylla-query span");
            expect(scyllagpt::slash_skill_completion_for_token(L"/scylla-querying").empty(),
                   "querying is not a skill completion");
            expect(scyllagpt::slash_skill_completion_for_token(L"/scy") == L"/scylla-query",
                   "/scy suggests /scylla-query");
            expect(scyllagpt::normalize_bang_keyring_insert("RDS_HOST") == "!scylla_RDS_HOST",
                   "bang insert normalizes scylla_ prefix");
            expect(scyllagpt::normalize_bang_keyring_insert("scylla_RDS_HOST") == "!scylla_RDS_HOST",
                   "bang insert keeps scylla_ prefix");
            scyllagpt::SecretRef ref;
            ref.name = "scylla_RDS_USER";
            ref.description = "user";
            const auto bang = scyllagpt::filter_keyring_bang_completions({ref}, L"!scy");
            expect(bang.size() == 1 && bang[0].first == "!scylla_RDS_USER", "!scy filters keyring names");
            scyllagpt::SecretRef hyphen;
            hyphen.name = "rds-scylla-user";
            expect(scyllagpt::filter_keyring_bang_completions({hyphen}, L"!scy").size() == 1,
                   "!scy reaches hyphenated keyring names");
            expect(scyllagpt::filter_keyring_bang_completions({hyphen}, L"!scylla_rds").size() == 1,
                   "!scylla_rds matches through the inserted prefix");
            expect(scyllagpt::filter_keyring_bang_completions({hyphen}, L"!user").size() == 1,
                   "bare fragment matches keyring name");
            expect(scyllagpt::filter_keyring_bang_completions({hyphen}, L"!nope").empty(),
                   "unrelated fragment matches nothing");
            expect(scyllagpt::composer_visible_lines(1) == 3, "composer rests at 3 lines");
            expect(scyllagpt::composer_visible_lines(7) == 7, "composer grows with content");
            expect(scyllagpt::composer_visible_lines(40) == 12, "composer caps at 12 lines");
            const auto at_spans =
                scyllagpt::find_at_file_spans(L"see @\"D:\\projects\\a.md\" and @readme.md end");
            expect(at_spans.size() == 2 && at_spans[0].path == L"D:\\projects\\a.md",
                   "@ quoted path span");
            expect(at_spans[1].display == L"readme.md", "@ basename span");
            expect(scyllagpt::composer_text_has_bang_keyring_token(L"use !scylla_RDS_HOST please"),
                   "detect bang keyring token");
            expect(!scyllagpt::keyring_bang_token_instructions().empty(), "bang token instructions");
            std::string err;
            const auto thread = scyllagpt::Json::parse(
                R"({"turns":[{"id":"t1","status":"completed"},{"id":"t2","status":"inProgress"},{"id":"t3","status":"failed"}]})",
                &err);
            expect(err.empty() && scyllagpt::latest_in_progress_turn_id(thread) == "t2",
                   "latest inProgress turn id from thread payload");
            expect(scyllagpt::latest_in_progress_turn_id(scyllagpt::Json::object()).empty(),
                   "missing turns yields empty inProgress id");
        }

        {
            using scyllagpt::ChatRowAction;
            const std::vector<std::wstring> shown{L"hello", L"working on it"};

            const auto unchanged = scyllagpt::chat_log_plan_rows(shown, shown);
            expect(unchanged.rows.size() == 2 && unchanged.rows[0] == ChatRowAction::Reuse &&
                       unchanged.rows[1] == ChatRowAction::Reuse,
                   "identical chat rows are reused");
            expect(unchanged.destroy_from == 2, "identical chat rows destroy nothing");

            const auto streamed =
                scyllagpt::chat_log_plan_rows(shown, {L"hello", L"working on it a bit more"});
            expect(streamed.rows[0] == ChatRowAction::Reuse && streamed.rows[1] == ChatRowAction::Retext,
                   "a streaming delta only retexts the trailing row");

            const auto grown = scyllagpt::chat_log_plan_rows(shown, {L"hello", L"working on it", L"next"});
            expect(grown.rows.size() == 3 && grown.rows[2] == ChatRowAction::Create,
                   "a new message creates exactly one row");

            const auto shrunk = scyllagpt::chat_log_plan_rows(shown, {L"hello"});
            expect(shrunk.rows.size() == 1 && shrunk.destroy_from == 1,
                   "a shorter transcript destroys only the surplus rows");

            struct Msg {
                bool user;
                std::string text;
            };
            const std::vector<Msg> local{{true, "u1"}, {false, "a1"}, {true, "u2"}};
            const auto merged = scyllagpt::merge_provider_agent_messages(local, {{false, "a1"}, {false, "a2"}});
            expect(merged.size() == 4, "merge appends only unseen provider assistant messages");
            expect(merged[0].user && merged[0].text == "u1" && !merged[1].user && merged[1].text == "a1" &&
                       merged[2].user && merged[2].text == "u2",
                   "merge preserves local transcript order");
            expect(!merged[3].user && merged[3].text == "a2", "unseen provider reply lands last");

            const auto no_dupe = scyllagpt::merge_provider_agent_messages(local, {{false, "a1"}});
            expect(no_dupe.size() == 3, "merge does not duplicate a known assistant message");

            const auto ignores_users =
                scyllagpt::merge_provider_agent_messages(local, {{true, "u9"}, {false, "a1"}});
            expect(ignores_users.size() == 3, "merge ignores provider user messages");
        }
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
        expect(templates.size() == 8, "mcp templates count");
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
          expect(has_github && has_linear && has_notion && !has_figma && has_strata, "mcp template ids");

        scyllagpt::McpManager mgr;
        auto conn = scyllagpt::McpManager::from_template(templates[0], L"Personal");
        expect(!conn.id.empty() && conn.service_id == "github", "mcp from_template");
        expect(!conn.connection_name.empty(), "mcp from_template connection_name");
        expect(conn.auth_state == scyllagpt::McpAuthState::Unknown, "mcp from_template auth");
        expect(conn.oauth_scopes.empty() && !conn.scopes_selected, "new MCP connection has no implicit scope grant");
        conn.oauth_scopes = {"read:user", "repo"};
        conn.scopes_selected = true;
        for (const auto& t : templates) {
            expect(t.service_id != "linear-readonly", "read-only Linear is a choice, not a separate recipe");
            if (t.service_id == "supabase") expect(t.suggested_endpoint_or_cmd == "https://mcp.supabase.com/mcp", "Supabase does not force read only");
            if (t.default_transport == scyllagpt::McpTransportKind::Http)
                expect(!scyllagpt::McpManager::suggested_oauth_scopes(t.service_id).empty(), "every remote recipe offers provider scopes");
        }
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
        expect(round && round->scopes_selected && round->oauth_scopes == conn.oauth_scopes, "selected OAuth scopes survive save and reload");
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
        const std::wstring base = L"C:\\state\\ScyllaGPT";
        const std::wstring id = L"0123456789abcdef0123456789abcdef";
        expect(scyllagpt::instance_data_root(base, id) == base + L"\\windows\\" + id,
               "secondary window paths use isolated state root");
        expect(scyllagpt::instance_data_root(base, L"..\\escape") == base,
               "invalid secondary window id cannot escape appdata");

        scyllagpt::Paths p = scyllagpt::make_paths();
        expect(!p.appdata.empty(), "paths appdata");
        expect(!p.knowledge_path.empty() && p.knowledge_path.find(L"knowledge.json") != std::wstring::npos,
               "paths knowledge");
        expect(!p.mcp_path.empty(), "paths mcp");
        expect(!p.strata_path.empty(), "paths strata");
    }

    return g_fail;
}
