# Codex app-server compatibility

Scylla is a native frontend for a locally installed Codex app-server. It is not a wrapper around the ChatGPT website and does not claim feature or history parity with chatgpt.com.

## Protocol snapshot

The JSON schemas under `scyllagpt/schemas/` were generated from a Codex CLI using:

```powershell
codex app-server generate-json-schema --out scyllagpt/schemas
```

The generated snapshot records the protocol expected by the current implementation. Codex is not bundled, and newer app-server versions may add or change fields.

## Implemented protocol areas

| Area | Scylla behavior |
|---|---|
| Initialization | Sends client metadata and waits for app-server readiness |
| Authentication | Starts ChatGPT browser OAuth and consumes account updates |
| Models | Requests `model/list` and presents a bounded current catalog |
| Threads | Starts, lists, reads, and resumes app-server threads |
| Turns | Starts and interrupts turns; streams assistant deltas |
| Approvals | Accepts workspace-scoped file/command requests only with an active project grant |
| Persistence | Uses the isolated Codex home managed by Scylla |

## Deliberate exclusions

- OpenAI Platform API-key configuration is not exposed in the UI.
- ChatGPT website conversation history, GPTs, projects, voice, and memory are not claimed.
- Web search, apps/connectors, hooks, remote plugins, helper agents, and computer-use helpers are disabled by managed configuration.
- Scylla does not redistribute `codex.exe`.

## Compatibility expectations

Scylla discovers versioned Codex installations before generic fallback paths. A different runtime can be selected in Settings. Because app-server is an evolving interface, users should rebuild against a current schema snapshot and run an authentication, model-list, thread, turn, and cancellation smoke test after changing Codex versions.

The isolated home remains `%LOCALAPPDATA%\ScyllaGPT\codex-home` for compatibility with existing installations. See [lockdown.md](lockdown.md) for the security boundary.
