#pragma once

#include <string>
#include <utility>
#include <vector>

namespace scyllagpt {

enum class McpTransportKind { Stdio, Http };

enum class McpToolClass { Read, Write, Destructive, Unknown };

enum class McpApprovalMode { Auto, Ask, Block };

enum class McpAuthState { Unknown, Healthy, Expired, NeedsReauth, Offline, Disabled };

struct McpApprovalPolicy {
    McpApprovalMode read = McpApprovalMode::Auto;
    McpApprovalMode write = McpApprovalMode::Ask;
    McpApprovalMode destructive = McpApprovalMode::Ask;
    McpApprovalMode unknown = McpApprovalMode::Ask;
};

struct McpToolState {
    std::string name;
    std::string title;
    McpToolClass classification = McpToolClass::Unknown;
    bool enabled = true;
};

// Factory metadata only — no OAuth / live client yet.
struct McpServiceTemplate {
    std::string service_id;
    std::string display_name;
    McpTransportKind default_transport = McpTransportKind::Http;
    std::string suggested_endpoint_or_cmd;  // hint text for UI / custom form
    std::vector<std::wstring> arguments;
    std::vector<std::pair<std::wstring, std::wstring>> environment;
};

struct McpConnection {
    std::string id;
    std::string service_id;
    std::wstring display_name;       // service label (legacy / template name)
    std::wstring connection_name;    // user-facing account/connection title
    std::wstring account_label;
    std::string agent_alias;         // optional, without @; [a-zA-Z0-9_-]+
    McpTransportKind transport_kind = McpTransportKind::Http;
    std::wstring endpoint_or_cmd;
    std::vector<std::string> oauth_scopes;
    bool scopes_selected = false; // Explicit provider-consent choice may have an empty list.
    bool has_authenticated = false;
    std::vector<std::wstring> arguments;
    std::vector<std::pair<std::wstring, std::wstring>> environment;
    bool enabled = true;
    bool disconnected = false;
    McpAuthState auth_state = McpAuthState::Unknown;
    std::string last_checked_iso;
    std::string last_error;
    std::vector<std::string> project_scope;  // empty = all projects
    McpApprovalPolicy policy;
    std::vector<McpToolState> tools;  // stub; filled after discovery later
};

class McpManager {
public:
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    McpConnection* by_id(const std::string& id);
    const McpConnection* by_id(const std::string& id) const;

    McpConnection* add(const McpConnection& c);
    bool remove(const std::string& id);

    bool set_alias(const std::string& id, const std::string& alias, std::string* error_out);
    McpConnection* resolve_alias(const std::string& alias_with_or_without_at);
    const McpConnection* resolve_alias(const std::string& alias_with_or_without_at) const;
    std::vector<std::pair<std::string, std::wstring>> alias_completions(const std::string& prefix) const;

    void set_enabled(const std::string& id, bool enabled);
    void disconnect(const std::string& id);
    void mark_auth(const std::string& id, McpAuthState state, const std::string& err = {});

    std::vector<McpConnection*> list_for_project(const std::string& project_id);
    std::vector<const McpConnection*> list_for_project(const std::string& project_id) const;

    const std::vector<McpConnection>& connections() const { return connections_; }

    static std::vector<McpServiceTemplate> known_templates();
    static std::vector<std::string> suggested_oauth_scopes(const std::string& service);
    static McpConnection from_template(const McpServiceTemplate& t, const std::wstring& account_label);

    static bool is_valid_alias(const std::string& alias_without_at);
    static std::string normalize_alias(const std::string& alias_with_or_without_at);

private:
    std::vector<McpConnection> connections_;
};

const char* mcp_transport_name(McpTransportKind k);
const char* mcp_tool_class_name(McpToolClass c);
const char* mcp_approval_name(McpApprovalMode m);
const char* mcp_auth_state_name(McpAuthState s);
McpTransportKind mcp_transport_from_name(const std::string& s);
McpToolClass mcp_tool_class_from_name(const std::string& s);
McpApprovalMode mcp_approval_from_name(const std::string& s);
McpAuthState mcp_auth_state_from_name(const std::string& s);

}  // namespace scyllagpt
