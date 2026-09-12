# MCP endpoints and scope controls

Verified 2026-09-11 against official documentation and live OAuth protected resource metadata. Scope lists in McpManager are unchecked choices, never automatic grants. “Load available scopes” refreshes choices from each server.

| Service | MCP endpoint | Scope controls and authentication |
| --- | --- | --- |
| GitHub | https://api.githubcopilot.com/mcp/ | Advertises `repo`, `read:org`, `read:user`, `user:email`, `read:packages`, `write:packages`, `read:project`, `project`, `gist`, `notifications`. Remote OAuth requires a host-registered GitHub App or OAuth App; Scylla offers a PAT field instead. Select actual PAT permissions when creating the token. `/readonly` restricts exposed tools. |
| Linear | https://mcp.linear.app/mcp | `read`, `write`; OAuth with dynamic client registration, or bearer API key/token. `/mcp/readonly` is optional. The normal endpoint does not force read-only access. |
| Notion | https://mcp.notion.com/mcp | Advertises only `default`. OAuth consent determines content access; separate read/write OAuth scopes are not advertised. |
| GitLab | https://gitlab.com/api/v4/mcp | Advertises `mcp`. Dynamic registration is supported where the instance administrator allows it. GitLab account permissions govern tool access. |
| Sentry | https://mcp.sentry.dev/mcp | `org:read`, `project:write`, `team:write`, `event:write`; OAuth with dynamic registration. Optional `/mcp/{organization}/{project}` limits the target. |
| Supabase | https://mcp.supabase.com/mcp | Advertises `organizations:read`, `projects:read`, `projects:write`, `database:write`, `database:read`, `analytics:read`, `secrets:read`, `edge_functions:read`, `edge_functions:write`, `environment:read`, `environment:write`, `storage:read`, `storage:write`. OAuth or PAT. `read_only=true`, `project_ref`, and `features` are independent endpoint controls. |
| Atlassian Rovo | https://mcp.atlassian.com/v2/mcp | Advertises 32 scopes covering identity, offline access, Jira, Confluence, Rovo search, code search, Teamwork Graph, goals, projects, Bitbucket, Loom, talent, teams, artifacts, and focus. Exact values are in McpManager and the live metadata below. OAuth consent remains authoritative. |

Supabase's guide currently says manual OAuth apps need write access to all available scopes, while its live metadata advertises granular scopes. Scylla exposes the advertised choices without claiming every subset is accepted. Provider validation and consent determine the effective grant. Tool groups default to the provider's documented set (all except storage); checkboxes can narrow it. Clearing the last group is blocked because omitting `features` would restore the provider default.

OAuth scope selections cannot change an already-created PAT's permissions. Scylla labels this distinction and links GitHub token creation. It never represents an editable nickname or agent alias as a verified provider username. No standard username field exists in the metadata checked here.

## Official documentation

- [GitHub authentication and setup](https://github.com/github/github-mcp-server#remote-github-mcp-server), [remote toolsets and read-only paths](https://github.com/github/github-mcp-server/blob/main/docs/remote-server.md)
- [Linear MCP](https://linear.app/docs/mcp)
- [Notion MCP](https://developers.notion.com/guides/mcp/mcp)
- [GitLab MCP](https://docs.gitlab.com/user/model_context_protocol/mcp_server/)
- [Sentry MCP](https://docs.sentry.io/product/sentry-mcp/)
- [Supabase MCP](https://supabase.com/docs/guides/getting-started/mcp)
- [Atlassian Rovo MCP](https://support.atlassian.com/atlassian-rovo-mcp-server/docs/getting-started-with-the-atlassian-remote-mcp-server/)

## Live scope metadata

- [GitHub](https://api.githubcopilot.com/.well-known/oauth-protected-resource/mcp/)
- [Linear](https://mcp.linear.app/.well-known/oauth-protected-resource)
- [Notion](https://mcp.notion.com/.well-known/oauth-protected-resource)
- [GitLab](https://gitlab.com/.well-known/oauth-protected-resource)
- [Sentry](https://mcp.sentry.dev/.well-known/oauth-protected-resource/mcp)
- [Supabase](https://mcp.supabase.com/.well-known/oauth-protected-resource/mcp)
- [Atlassian](https://mcp.atlassian.com/.well-known/oauth-protected-resource/v2/mcp)
