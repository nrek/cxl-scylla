# Agent activity and Cursor ACP

The Agent pane has one updating, scrollable activity box above the composer. It shows elapsed turn time, current work, active sub-agents when reported by Codex, and changed file paths. It does not append events to the conversation. File paths are counted only after successful provider-reported edits; this is not a Git diff or independent filesystem audit. Claude print mode provides turn-level status only.

The box keeps partial-turn file results on completion, failure, or interruption, and resets for a new turn or opened conversation. It is an in-memory snapshot, not persisted chat history. Cancellation remains busy until the provider confirms it. Turn-start errors and runtime pipe closure end the busy state. Existing transcript rendering is unchanged.

## Cursor status

**Protocol foundation only; Cursor account connection and chat are not enabled.** Cursor is hidden from Settings → AI Providers until Scylla has a working process transport, authentication flow, and agent execution path. Executable discovery alone is not presented as a provider capability.

`AcpClient` is a transport-independent JSON-RPC 2.0 client compiled into Workbench and tested with fixtures. It supports version negotiation, advertised authentication methods, session create/load, advertised model/mode selection, prompt text streaming, tool/plan activity, partial tool updates, cancellation, and disconnect/error states. Model IDs come from the server. Unsupported client requests receive errors; permissions and blocking Cursor extensions are cancelled. These replies and `clientCapabilities` are **not filesystem or shell isolation**. An empty `mcpServers` array also does not disable Cursor's native MCP configuration.

Still required before enabling Send: native process transport and bounded asynchronous auth/status checks; a versioned CLI handshake; verified configuration/environment and filesystem/shell controls; interactive permissions/questions/plans; provider/session/model persistence; model configuration updates; and end-to-end operator verification. The supplied full integration plan remains the governing reference. No account credentials, billing pools, or usage limits have been read or inferred.

## Evidence and verification

Cursor's official [ACP documentation](https://cursor.com/docs/cli/acp) and [installation documentation](https://cursor.com/docs/cli/installation), read 2026-09-07, document `agent acp`, newline-delimited JSON-RPC 2.0, `cursor_login`, session modes, and native Windows installation. A Cursor-managed launcher was later found, but it is not sufficient evidence of a usable integration and failed direct invocation in the Scylla environment. No live ACP/authentication claim is made.

Build and run the focused tests from a Windows compiler environment:

```powershell
cmake --build build --config Release --target scyllagpt scyllagpt-agent-tests
& .\build\Release\scyllagpt-agent-tests.exe
```

The tests cover proposed/failed/successful edits, deduplication, concurrent sub-agent state, interruption, cross-session events, partial ACP updates, permissions, cancellation, advertised authentication/modes, transport failure, and incompatible versions. The current Workbench build and focused tests pass. The broader existing suite is blocked by the unrelated `workflow_source_manifest` call at `tests/test_workbench_domain.cpp:113` needing updated arguments. Visual/DPI testing and live Cursor verification remain outstanding.
