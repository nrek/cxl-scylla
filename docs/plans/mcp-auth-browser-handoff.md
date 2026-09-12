# MCP authentication browser launch fix

Project: cxl-scylla

## Cause

Fresh MCP catalog cards started with no scopes selected and provider consent unchecked. `Persist()` therefore rejected Authenticate before calling the native OAuth flow, so the system browser was never reached. GitHub also defaulted to access-token mode while retaining the generic Authenticate label, which incorrectly implied that clicking it would launch a browser.

## Changes

- Fresh catalog cards now default to choosing permissions at the provider consent screen. Users may still replace that choice with explicit advertised scopes.
- Browser OAuth buttons now say **Authenticate in browser** or **Reauthenticate**.
- Token-mode buttons now say **Validate token** or **Validate new token**.
- The token field is hidden unless token mode is selected.
- GitHub remains in token mode by default because its remote MCP requires a host-registered GitHub OAuth app and does not support this client's dynamic registration path.

## Review

- `scylla-fluent/McpSettingsPane.cs`

## Verification

- Fluent Release build succeeded through `.tmpcl/build-history.ps1`.
- Existing unrelated warning remains: `WorkbenchWindow._windowInteraction` is never assigned.
- No live provider account was used. Manually verify a browser-capable provider (for example Linear) opens its consent page and GitHub clearly requests a token.

## Deployment

Local Windows desktop build only. No server deployment.
