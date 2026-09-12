#include "scyllagpt/mcp_manager.h"
#include <windows.h>
#include <filesystem>
#include <iostream>

int run_mcp_oauth_tests();

int main() {
    int failed = run_mcp_oauth_tests();
    auto expect = [&](bool value, const char* name) {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        if (!value) ++failed;
    };
    using namespace scyllagpt;
    auto templates = McpManager::known_templates();
    for (const auto& item : templates) {
        if (item.default_transport != McpTransportKind::Http) continue;
        auto connection = McpManager::from_template(item, L"Nickname");
        expect(connection.oauth_scopes.empty() && !connection.scopes_selected, "no implicit OAuth grant");
        expect(!McpManager::suggested_oauth_scopes(item.service_id).empty(), "provider offers scope choices");
        expect(item.suggested_endpoint_or_cmd.find("readonly") == std::string::npos &&
               item.suggested_endpoint_or_cmd.find("read_only") == std::string::npos, "no forced read-only endpoint");
        connection.oauth_scopes = McpManager::suggested_oauth_scopes(item.service_id);
        connection.scopes_selected = true;
        connection.project_scope = {"test-project"};
        McpManager manager;
        manager.add(connection);
        manager.mark_auth(connection.id, McpAuthState::Healthy);
        const auto path = std::filesystem::temp_directory_path() / ("scylla-mcp-" + connection.id + ".json");
        expect(manager.save(path.wstring()), "save selected scopes");
        McpManager loaded;
        expect(loaded.load(path.wstring()), "reload selected scopes");
        auto* restored = loaded.by_id(connection.id);
        expect(restored && restored->scopes_selected && restored->oauth_scopes == connection.oauth_scopes,
               "all selected scopes survive roundtrip");
        expect(restored && restored->has_authenticated, "authentication history survives roundtrip");
        expect(loaded.list_for_project("other-project").empty(), "project filtering preserves isolation");
        loaded.disconnect(connection.id);
        expect(loaded.by_id(connection.id)->auth_state == McpAuthState::NeedsReauth, "disconnect marks stale");
        std::filesystem::remove(path);
    }
    std::cout << failed << " failed\n";
    return failed ? 1 : 0;
}
