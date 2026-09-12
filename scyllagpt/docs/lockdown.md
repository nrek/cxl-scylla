# Scylla security controls

Scylla is a native Codex and Claude Code client. These controls limit unrequested folder traversal and disable helper features that are outside Scylla's intended scope.

Do not treat this file as an OS AppContainer or isolated-user sandbox. The former Scylla Cage CLI is archived and is not part of this product.

## Project dropdown = agent browse/edit grant

The header project selector is the **single folder** the agent may work in:

| State | Agent sandbox |
|-------|----------------|
| Project selected (e.g. `example-project`) | `workspace-write` + `writableRoots = [that folder]` + **`shell_tool = true`** (sandboxed read/search/edit). Thread `cwd` is that folder |
| No project | `read-only`, `shell_tool = false` on the isolated Scylla workspace |

Managed `config.toml` is rewritten on launch **and** when the grant changes. If shell enablement flips while the app-server is running, Scylla **restarts** Codex so CreateProcess `--disable` flags match (shell is not hot-reloaded).

CreateProcess cwd stays `%LOCALAPPDATA%\ScyllaGPT\workspace` (process hygiene). Apps, hooks, web_search, notify helpers stay off.

### Approvals under grant

| Request | With project grant | No grant |
|---------|-------------------|----------|
| `item/fileChange/requestApproval`, `applyPatchApproval` | **accept** | decline |
| `item/commandExecution/requestApproval`, `execCommandApproval` | **accept** (sandbox still applies) | decline |
| permissions / MCP elicitation | deny / decline | deny / decline |

## What we pin

| Control | Behavior |
|---------|----------|
| Isolated `CODEX_HOME` | `%LOCALAPPDATA%\ScyllaGPT\codex-home` — does not mutate `%USERPROFILE%\.codex` |
| `config.toml` | **Overwritten every launch / grant change.** `auth.json` is never rewritten by Scylla |
| Process cwd | `%LOCALAPPDATA%\ScyllaGPT\workspace` |
| Thread cwd + sandbox | Follow the project grant; existing thread keeps its project cwd if the dropdown switches mid-session |
| `sandbox_mode` | `workspace-write` with grant; `read-only` without |
| `shell_tool` | **true** with grant (browse/search); **false** without |
| CLI `--disable` | Always disables apps/hooks/plugins/…; disables `shell_tool` only when there is **no** grant |
| `project_doc_max_bytes = 0` | Do not walk parent trees for `AGENTS.md` |
| Features off (always) | apps/connectors, remote plugins, hooks, memories, multi-agent, skill MCP install, goals, shell snapshot |
| `web_search = "disabled"` | No cached/live search spider |
| `notify = []` | No `codex-computer-use.exe` |
| `file_opener = "none"` | Do not open citations in Cursor/VS Code |
| Shell env inherit | `none` |
| Job Object | `KILL_ON_JOB_CLOSE`. Assign failure **fails closed** |
| Handle inheritance | stdin/stdout/stderr only |

## Residual risk (honest)

- Codex still runs as the **signed-in Windows user**. Policy + sandbox, not an OS AppContainer. Shell under grant can run commands; the Codex sandbox is the primary filesystem bound.
- ChatGPT OAuth opens the system browser as the signed-in Windows user.
- Writable/browse roots are the selected project folder plus Knowledge sources granted to the agent. Do not treat these as OS-level isolation.

## Operator check after launch

1. With project open: `type %LOCALAPPDATA%\ScyllaGPT\codex-home\config.toml` → `sandbox_mode = "workspace-write"`, `shell_tool = true`.
2. Agent hint: `Grant: <path> (browse / edit)`. Status: `browse/edit <name>`.
3. New chat: ask the agent to find a file by name under the grant — it should search/read without asking you to paste sources.
4. No project: `shell_tool = false`, `read-only`; shell approvals declined.
5. No `codex-computer-use.exe`.
