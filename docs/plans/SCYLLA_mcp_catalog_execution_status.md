---
status: in_progress
project: cxl-scylla
source: D:/projects/.cursor/plans/scylla-workbench-one-click-mcp-catalog-agentic-setup-plan.md
---

# MCP catalog implementation status

This is an execution record, not a replacement for the source plan. The full eleven-phase plan is not complete.

Implemented source changes:

- Fluent composer resolves filename mentions on Tab via the native project/Knowledge catalog. Unique matches insert quoted absolute paths; ambiguous matches require a choice; stale asynchronous results are discarded.
- Exact filename matches sort ahead of substring matches. Native composer also refreshes completions on Tab.
- Fluent MCP settings use a searchable service list and collapse custom configuration under Advanced.
- Remote recipes cover GitHub, Linear, Notion, GitLab, Sentry, Supabase, and Atlassian. Read-only Linear and Supabase are explicit choices within each service.
- New connections are current-project scoped with Ask policy for reads as well as mutations.
- Fluent authorization invokes the existing native PKCE/DCR/CredMan implementation asynchronously. Completing authorization reloads connection state to preserve edits made while the browser is open.
- Removed the unapproved Figma recipe from the offered catalog. Native STRATA is not advertised in the Fluent MCP catalog.

Remaining acceptance work:

- Data-driven versioned catalog files and template-specific fields, read-only/toolset presets, setup review, and current/selected/all project management in Fluent.
- GitHub host-owned OAuth app registration (secure PAT fallback is implemented). Complete live account consent and token checks for each provider.
- Post-authentication tool discovery, schema signatures, drift review, and per-tool permissions in Fluent.
- Agent setup operations with human approval and transport routing into running sessions.
- Local package/runtime discovery, pinned versions, package approval, minimal environments, process supervision, and local recipes.
- Infrastructure provider recipes that require deployment/account metadata, Docker profiles, official registry browsing, and database template audits.
- Provider-gated integrations remain unavailable until the requisite client approval exists.

Validation: source/diff checks only; no build, app restart, credential use, or external authentication was performed. Filename regression tests were added but have not been executed. Manually rebuild both the native DLL and Fluent executable together because new ABI exports are required.

## MCP settings follow-up — 2026-09-11

Unified service cards replace the separate saved-connection section. Each card offers authentication, removal, status, saved nickname, and editable scopes; custom MCP is full width. Explicit OAuth selections persist and reach the authorization URL; changes invalidate credentials. GitHub has secure PAT input. Provider scopes and URLs were checked against official documentation and live metadata for all seven services; see [endpoint and scope research](mcp-provider-endpoints-and-scopes.md). Sentry now uses `/mcp`. Supabase exposes project, read-only, and tool-group controls. Legacy saved read-only endpoints remain restricted until edited.

The native DLL and Fluent shell build successfully. The dedicated `scylla-mcp-tests` target passes, and `tests/mcp-provider-metadata.py` confirms all seven live scope lists. Full-suite validation is incomplete: an earlier run reported an unrelated Plan/Knowledge-folder assertion, and the environment-corrected run stalled in SSH tests and was stopped. Browser consent, real account identity retrieval, and visual verification remain outstanding. Nicknames and agent aliases are not claimed as provider usernames.
