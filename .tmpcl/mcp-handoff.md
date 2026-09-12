# MCP connection cards and explicit scope selection

Project: cxl-scylla

## Changes

- Replaced EditorPane's separate catalog and saved-connection section with McpSettingsPane. Cards have top-row Authenticate/Reauthenticate and Remove, nickname, endpoint, agent alias, gray/green/red state, vertical provider scope checklists, collapsed Edit Scopes after authentication, and Save only when fields change. Custom MCP is full width. Multiple accounts remain supported.
- Persisted explicit OAuth scopes, consent choice, and authentication history in the native connection model. OAuth requests use the selected scopes instead of a challenge hint or first advertised scope. Changing the endpoint or scope selection clears credentials and marks reauthentication necessary. Browser completion rejects changed/removed/disabled connections.
- Added live scope discovery through the native C ABI and verified all seven providers. Fixed the Sentry endpoint to /mcp. Consolidated Linear read-only into a choice and removed Supabase's forced read-only default. Legacy read-only URLs are retained until the user edits them.
- Supabase supports project_ref, read_only, and feature-group selection. GitHub and Linear have optional read-only endpoints. Native metadata offers every advertised OAuth scope, initially unchecked.
- Added a PasswordBox/token authentication path, validation via an MCP initialize request, and Windows Credential Manager storage. GitHub defaults to this path because remote OAuth requires a host-registered GitHub app. Token permissions must be selected at the provider; local OAuth checkboxes cannot rewrite a PAT.
- MCP snapshots are filtered to the active project, expired/missing credentials are rendered stale, and nickname/alias text is not represented as verified account identity.

## Review files

- D:/projects/cxl-scylla/scylla-fluent/McpSettingsPane.cs
- D:/projects/cxl-scylla/scylla-fluent/EditorPane.cs
- D:/projects/cxl-scylla/scylla-fluent/NativeCore.cs
- D:/projects/cxl-scylla/scyllagpt/src/core/scylla_core_c.cpp
- D:/projects/cxl-scylla/scyllagpt/src/domain/mcp_manager.cpp
- D:/projects/cxl-scylla/scyllagpt/src/domain/mcp_oauth.cpp
- D:/projects/cxl-scylla/scyllagpt/tests/test_mcp_settings.cpp
- D:/projects/cxl-scylla/tests/mcp-provider-metadata.py
- D:/projects/cxl-scylla/docs/plans/mcp-provider-endpoints-and-scopes.md

## Verification

- Native Release DLL and broker build succeeded.
- Fluent Release build succeeded in the isolated .tmpcl/chat-history-build output folder, with matching native runtime copied alongside it.
- Dedicated scylla-mcp-tests passed: OAuth/PKCE helpers, fixture-only Credential Manager roundtrip/cleanup, scope persistence for all providers, no implicit read-only grants, authentication history, and project isolation.
- Live, unauthenticated native metadata contract test passed for GitHub (10 scopes), Linear (2), Notion (1), GitLab (1), Sentry (4), Supabase (13), and Atlassian (32).
- git diff --check passed. Existing unrelated local edits were retained.
- Full-suite validation remains incomplete: an initial run reported Plan/Knowledge-folder and outdated catalog assertions; the catalog assertions were updated. The environment-corrected full run stalled in SSH tests and was stopped. MCP tests have a separate executable to avoid this unrelated stall.

## Limits and acceptance checks

- No user account sign-in or existing user token was used. Complete browser consent or token authentication for real accounts, including cancellation, token expiry, Remove, and saving a narrower scope selection.
- No visual smoke test was performed. Review settings at wide and narrow pane widths, including authenticated/stale cards and custom MCP.
- Verified provider username display is not implemented; the UI shows the separately labeled agent alias where available. OAuth metadata does not provide a standard username field. Provider-specific identity retrieval is still required for the requested @username display.
- Notion advertises only default and GitLab only mcp; finer access is determined by provider authorization. Supabase's manual-app documentation currently requires all scopes even though its metadata advertises granular scopes; subsets remain subject to provider acceptance.
- GitHub browser OAuth still needs an app registration owned by the host. PAT authentication is the implemented fallback. GitLab instances with DCR disabled need a separately registered client, which is not configured by this UI.
- Service scope choices are requests, not a claim that all combinations are accepted or that local selection changes an existing PAT's permissions.

## Context

Loaded the four requested blueprints. STRATA recent and search were called separately with exact project filters for cxl-scylla, cxl-sanctum, cxl-scout, and cxl-sentinel. Relevant prior art was confined to Scylla's MCP execution record and source plan. The other three projects were not changed.

## Deployment

Local Windows desktop build; no server deployment. Close Scylla before copying the runtime into the normal application output. This session built isolated Fluent output and did not restart the running app.

```powershell
cd D:\projects\cxl-scylla
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config Release --target scylla-core-shared scylla-mcp-tests
& .\build\Release\scylla-mcp-tests.exe
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' scylla-fluent\Scylla.csproj /p:Configuration=Release /p:Platform=x64 /restore
& .\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0\scylla.exe
```

The agent shell initially lacked Windows environment variables. Existing .tmpcl/build-native.ps1 and .tmpcl/build-history.ps1 demonstrate the required SystemRoot, WINDIR, ProgramData, and TEMP setup. Restoring SystemRoot/WINDIR also resolved documentation download failures.
