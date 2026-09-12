#include "scyllagpt/json.h"
#include "scyllagpt/chat_mode.h"
#include "scyllagpt/session.h"
#include "scyllagpt/knowledge.h"
#include "scyllagpt/language.h"
#include "scyllagpt/layout.h"
#include "scyllagpt/lockdown.h"
#include "scyllagpt/document.h"
#include "scyllagpt/store.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

using scyllagpt::Json;
using scyllagpt::JsonlDecoder;

static int g_fail = 0;

static void expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

int main() {
    {
        // Exercise real completion handling without starting a provider or using user data.
        const auto temp = std::filesystem::temp_directory_path() / ("scylla-plan-test-" + std::to_string(GetCurrentProcessId()));
        const auto project = temp / "project";
        const auto data = temp / "appdata";
        std::filesystem::create_directories(project);
        std::filesystem::create_directories(data);
        scyllagpt::Session session;
        session.paths.appdata = data.wstring();
        session.paths.store_path = (data / "store.json").wstring();
        session.paths.knowledge_path = (data / "knowledge.json").wstring();
        session.settings.project_folder = project.wstring();
        session.project_root = project.wstring();
        const auto project_id = session.store.open_or_create(project.wstring())->id;
        session.store.upsert_thread(project_id, "test", "plan-test", "Test plan", "");
        session.active_thread_id = "plan-test";
        session.send_user("Inspect the project", "plan");
        session.handle_line(R"({"method":"item/completed","params":{"threadId":"plan-test","item":{"type":"agentMessage","phase":"final_answer","text":"# Plan\n\n1. Inspect\n2. Validate"}}})");
        session.handle_line(R"({"method":"turn/completed","params":{"threadId":"plan-test","turn":{"status":"completed"}}})");
        expect(!session.last_plan_path.empty() && std::filesystem::exists(scyllagpt::utf16(session.last_plan_path)), "completed Plan saves Knowledge artifact");
        scyllagpt::KnowledgeStore knowledge;
        knowledge.load(session.paths.knowledge_path);
        expect(knowledge.source_for_path(scyllagpt::utf16(session.last_plan_path), project_id) != nullptr, "plan registered for originating project");
        const auto saved = session.last_plan_path;
        session.send_user("What is here?", "ask");
        session.handle_line(R"({"method":"item/completed","params":{"threadId":"plan-test","item":{"type":"agentMessage","text":"Findings"}}})");
        session.handle_line(R"({"method":"turn/completed","params":{"threadId":"plan-test","turn":{"status":"completed"}}})");
        expect(session.last_plan_path == saved, "Ask does not save a plan");
        session.send_user("Plan more", "plan");
        session.handle_line(R"({"method":"item/completed","params":{"threadId":"plan-test","item":{"type":"agentMessage","text":"Partial plan"}}})");
        session.handle_line(R"({"method":"turn/completed","params":{"threadId":"plan-test","turn":{"status":"interrupted"}}})");
        expect(session.last_plan_path == saved, "interrupted Plan does not save partial artifact");
        expect(std::filesystem::is_empty(project), "Plan and Ask leave project files unchanged");
        std::filesystem::remove_all(temp);
    }
    for (const auto* mode : {"ask", "plan"}) {
        Json params = Json::parse(R"({"approvalPolicy":"on-request","sandboxPolicy":{"type":"workspaceWrite","writableRoots":["D:/project"],"networkAccess":true}})");
        scyllagpt::apply_chat_mode_policy(params, mode);
        expect(params.at("approvalPolicy").as_string() == "never", "discovery modes cannot escalate writes");
        expect(params.at("sandboxPolicy").at("type").as_string() == "readOnly"
            && !params.at("sandboxPolicy").has("writableRoots")
            && !params.at("sandboxPolicy").at("networkAccess").as_bool(), "discovery overrides remove writable roots and network");
    }
    {
        Json params = Json::parse(R"({"approvalPolicy":"on-request","sandboxPolicy":{"type":"workspaceWrite","writableRoots":["D:/project"]}})");
        const auto original = params.dump();
        scyllagpt::apply_chat_mode_policy(params, "execute");
        expect(params.dump() == original, "Execute preserves project sandbox grants");
        expect(!scyllagpt::valid_chat_mode("unknown"), "unknown modes rejected");
    }
    {
        std::string err;
        Json j = Json::parse(R"({"method":"initialize","id":0,"params":{"clientInfo":{"name":"scylla_gpt","version":"0.1.0"}}})", &err);
        expect(err.empty() && j.at("method").as_string() == "initialize", "object parse");
        expect(j.at("id").as_int() == 0, "numeric id 0");
        expect(j.dump().find("\"initialize\"") != std::string::npos, "dump");
    }
    {
        std::string err;
        Json::parse("{\"a\":1} trailing", &err);
        expect(!err.empty(), "trailing data rejected");
    }
    {
        JsonlDecoder d;
        std::vector<std::string> lines;
        std::string err;
        const std::string chunk1 = R"({"id":1,"result":)";
        expect(d.feed(chunk1.data(), chunk1.size(), lines, &err) && lines.empty(), "split: incomplete first chunk");
        const std::string chunk2 = "{}}\n{\"method\":\"turn/started\",\"params\":{}}\n{";
        expect(d.feed(chunk2.data(), chunk2.size(), lines, &err), "split: second chunk");
        expect(lines.size() == 2, "two complete lines");
        expect(lines[0].find("\"id\":1") != std::string::npos, "first line");
        expect(lines[1].find("turn/started") != std::string::npos, "second line");
    }
    {
        JsonlDecoder d;
        std::vector<std::string> lines;
        std::string err;
        const std::string two = "{\"a\":1}\n{\"b\":2}\n";
        expect(d.feed(two.data(), two.size(), lines, &err) && lines.size() == 2, "combined lines");
    }
    {
        Json j = Json::parse(R"({"delta":"cafe\u0301 \uD83D\uDE00"})");
        expect(j.at("delta").as_string().size() > 5, "unicode escape");
    }
    {
        Json a = Json::object();
        a["decision"] = Json::string("decline");
        expect(a.dump() == "{\"decision\":\"decline\"}", "approval decline payload");
    }
    {
        const char* ro = scyllagpt::isolated_codex_config_toml(false);
        const std::string t(ro);
        expect(t.find("sandbox_mode = \"read-only\"") != std::string::npos, "no-grant lockdown is read-only");
        expect(t.find("web_search = \"disabled\"") != std::string::npos, "lockdown disables web_search");
        expect(t.find("shell_tool = false") != std::string::npos, "lockdown disables shell_tool");
        expect(t.find("apps = false") != std::string::npos, "lockdown disables apps");
        expect(t.find("remote_plugin = false") != std::string::npos, "lockdown disables remote_plugin");
        expect(t.find("hooks = false") != std::string::npos, "lockdown disables hooks");
        expect(t.find("file_opener = \"none\"") != std::string::npos, "lockdown file_opener none");
        expect(t.find("project_doc_max_bytes = 0") != std::string::npos, "lockdown no AGENTS walk");
        expect(t.find("approval_policy = \"on-request\"") != std::string::npos, "lockdown on-request not never");
        expect(t.find("notify = []") != std::string::npos, "lockdown empty notify");
        expect(t.find("inherit = \"none\"") != std::string::npos, "lockdown shell env inherit none");
        expect(t.find("codex-computer-use") == std::string::npos, "lockdown has no computer-use helper");
        const char* ww = scyllagpt::isolated_codex_config_toml(true);
        const std::string w(ww);
        expect(w.find("sandbox_mode = \"workspace-write\"") != std::string::npos, "grant lockdown is workspace-write");
        expect(w.find("shell_tool = true") != std::string::npos, "grant lockdown enables shell for browse/search");
        const std::wstring flags_off = scyllagpt::app_server_disable_args(false);
        expect(flags_off.find(L"--disable apps") != std::wstring::npos, "cli disable apps");
        expect(flags_off.find(L"--disable shell_tool") != std::wstring::npos, "cli disable shell when no grant");
        const std::wstring flags_on = scyllagpt::app_server_disable_args(true);
        expect(flags_on.find(L"--disable shell_tool") == std::wstring::npos, "cli allows shell when grant");
        expect(flags_on.find(L"--disable apps") != std::wstring::npos, "cli still disables apps with grant");
    }
    {
        using scyllagpt::compute_panes;
        auto a = compute_panes(1600, 220, 400, 232, false, 0, 0, 0);
        expect(a.show_files && a.show_editor && a.show_agent && a.show_history, "1600 four panes");
        expect(a.files >= 180 && a.editor >= 400 && a.agent >= 300 && a.history >= 180, "1600 mins");
        auto b = compute_panes(1200, 220, 400, 232, false, 0, 0, 0);
        expect(b.show_files && b.show_editor && b.show_agent && !b.show_history, "1200 history collapsed");
        auto c = compute_panes(1000, 220, 400, 232, false, 0, 0, 0);
        expect(!c.show_files && c.show_editor && c.show_agent && !c.show_history, "1000 editor+agent");
        auto d = compute_panes(1440, 220, 400, 232, true, 0, 0, 0);
        expect(!d.show_files && !d.show_history && d.show_editor && d.show_agent, "focus editor hides sides");
        auto e = compute_panes(4000, 900, 650, 900, false, 0, 0, 0);
        expect(e.files == 600 && e.agent == 650 && e.history == 600, "side panes cap at 600 and chat log is uncapped");
        auto f = compute_panes(4000, 0, 0, 0, false, 0, 0, 0);
        expect(f.files == 300 && f.agent == 650 && f.history == 300, "fresh pane defaults are 300 650 300");
        auto g = compute_panes(4000, 300, 1200, 300, false, 0, 0, 0);
        expect(g.agent == 1200, "chat log has no maximum width");
    }
    {
        const auto spans = scyllagpt::lex_cpp(L"int x = 1; // c\n#include <x>\n\"hi\"");
        bool kw = false, comment = false, pre = false, str = false, num = false;
        for (const auto& s : spans) {
            kw = kw || s.kind == scyllagpt::CppKind::Keyword;
            comment = comment || s.kind == scyllagpt::CppKind::Comment;
            pre = pre || s.kind == scyllagpt::CppKind::Preproc;
            str = str || s.kind == scyllagpt::CppKind::String;
            num = num || s.kind == scyllagpt::CppKind::Number;
        }
        expect(kw && comment && pre && str && num, "cpp lexer kinds");
        expect(scyllagpt::looks_like_cpp(L"acl.cpp") && !scyllagpt::looks_like_cpp(L"readme.md"), "cpp path detect");
        expect(scyllagpt::breadcrumbs(L"D:\\src\\scylla\\acl.cpp").find(L"acl.cpp") != std::wstring::npos, "breadcrumbs");
        expect(scyllagpt::detect_language_id(L"x.ts") == "cpp", "ts via cpp lexer id");
        expect(scyllagpt::detect_language_id(L"a.py") == "python", "python lang");
        expect(scyllagpt::detect_language_id(L"Dockerfile") == "bash", "dockerfile lang");
        expect(scyllagpt::detect_language_id(L"notes.txt") == "null", "plaintext null");
        auto ind = scyllagpt::editor_detect_indent(L"  foo\n  bar\n    baz\n");
        expect(!ind.use_tabs && ind.tab_width == 2, "indent spaces 2");
        auto indt = scyllagpt::editor_detect_indent(L"\tfoo\n\tbar\n");
        expect(indt.use_tabs, "indent tabs");
    }
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        const std::filesystem::path a = std::filesystem::path(tmp) / L"scyllagpt-id-a" / L"foo";
        const std::filesystem::path b = std::filesystem::path(tmp) / L"scyllagpt-id-b" / L"foo";
        std::error_code ec;
        std::filesystem::create_directories(a, ec);
        std::filesystem::create_directories(b, ec);
        scyllagpt::WorkspaceStore s;
        auto* p1 = s.open_or_create(a.wstring());
        const std::string id1 = p1 ? p1->id : std::string();
        auto* p1b = s.open_or_create(a.wstring());
        const std::string id1b = p1b ? p1b->id : std::string();
        expect(!id1.empty() && id1 == id1b && s.projects.size() == 1, "same folder same uuid");
        auto* p2 = s.open_or_create(b.wstring());
        expect(p2 && p2->id != id1 && s.projects.size() == 2, "same leaf name different dirs");
        std::filesystem::remove_all(a.parent_path(), ec);
        std::filesystem::remove_all(b.parent_path(), ec);
    }
    {
        scyllagpt::ContextChip c;
        c.label = "demo.cpp";
        c.body = "int x;\n";
        c.unsaved = true;
        const std::string snap = scyllagpt::snapshot_context({c});
        expect(snap.find("Unsaved buffer") != std::string::npos, "snapshot unsaved label");
        expect(snap.find("int x;") != std::string::npos, "snapshot body");
        expect(scyllagpt::snapshot_context({}).empty(), "empty chips empty snapshot");
    }
    {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        const std::filesystem::path dir = std::filesystem::path(tmp) / L"scyllagpt-doc";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path f = dir / L"round.txt";
        {
            HANDLE h = CreateFileW(f.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
            expect(h != INVALID_HANDLE_VALUE, "create roundtrip file");
            const char* raw = "hello\r\nworld\r\n";
            DWORD wr = 0;
            WriteFile(h, raw, 14, &wr, nullptr);
            CloseHandle(h);
        }
        scyllagpt::LoadedText loaded = scyllagpt::load_text_file(f.wstring());
        expect(!loaded.binary && loaded.crlf && loaded.text.find(L"\n") != std::wstring::npos, "load crlf as lf");
        expect(loaded.text.find(L"\r") == std::wstring::npos, "buffer is lf");
        const auto saved = scyllagpt::save_text_file(f.wstring(), loaded.text, loaded.enc, loaded.crlf, loaded.hash);
        expect(saved.ok && !saved.conflict, "save same hash");
        HANDLE h = CreateFileW(f.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        char buf[32]{};
        DWORD rd = 0;
        ReadFile(h, buf, 31, &rd, nullptr);
        CloseHandle(h);
        expect(std::string(buf, rd).find("\r\n") != std::string::npos, "disk still crlf");
        {
            HANDLE w = CreateFileW(f.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
            const char* other = "changed\r\n";
            DWORD wr = 0;
            WriteFile(w, other, 9, &wr, nullptr);
            CloseHandle(w);
        }
        const auto conflict = scyllagpt::save_text_file(f.wstring(), loaded.text, loaded.enc, loaded.crlf, loaded.hash);
        expect(conflict.conflict && !conflict.ok, "save conflict when disk hash moved");
        std::filesystem::remove_all(dir, ec);
    }
    {
        const auto& t = scyllagpt::theme();
        expect(t.shell == RGB(0x0C, 0x0F, 0x12), "shell app_bg");
        expect(t.app_bg == t.shell, "app_bg aliases shell");
        expect(t.work == t.editor && t.work == t.agent, "editor and agent share work surface");
        expect(t.gutter == t.work, "gutter matches editor canvas");
        expect(t.amber == RGB(0xE6, 0x94, 0x05), "amber identity");
        expect(t.border_subtle == RGB(0x26, 0x2C, 0x33), "subtle border token");
    }
    extern int run_keyring_tests();
    g_fail += run_keyring_tests();
    extern int run_project_environment_tests();
    g_fail += run_project_environment_tests();
    extern int run_project_connection_tests();
    g_fail += run_project_connection_tests();
    extern int run_connection_policy_tests();
    g_fail += run_connection_policy_tests();
    extern int run_connection_broker_tests();
    g_fail += run_connection_broker_tests();
    extern int run_ssh_query_executor_tests();
    g_fail += run_ssh_query_executor_tests();
    extern int run_broker_transport_tests();
    g_fail += run_broker_transport_tests();
    extern int run_terminal_screen_tests();
    g_fail += run_terminal_screen_tests();
    extern int run_terminal_profile_tests();
    g_fail += run_terminal_profile_tests();
    extern int run_workbench_domain_tests();
    g_fail += run_workbench_domain_tests();
    extern int run_mcp_oauth_tests();
    g_fail += run_mcp_oauth_tests();
    extern int run_settings_policy_tests();
    g_fail += run_settings_policy_tests();

    if (g_fail) {
        std::cerr << g_fail << " failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
