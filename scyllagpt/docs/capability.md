# Scylla Workbench capability checklist

Implementation status for the native Workbench client. This is not a claim that Workbench is ChatGPT.com, Claude Code, or an OS AppContainer.

Legend: **done** (in this tree, locally tested where noted) · **deferred** · **blocked**.

## Completed

| Item | Notes |
|------|--------|
| Four-pane shell | Files \| Editor \| Agent \| Chats |
| Command registry | `commands.h` / `commands.cpp` — shared `CmdId`; `menu_apply_command_state` for enabled/checked |
| Center-pane MainContent | Settings / Access / Diagnostics / Shortcuts / Getting Started replace editor column; Scintilla docs + view preserved; Back / Escape restore editor |
| Settings (center) | Sections: AI Providers, Editor, Advanced — immediate persist; deep-link from Agent → Manage AI Providers |
| Native menu bar | File / Edit / View / Agent / Access / Help — no gray placeholder items (Minimap / Find in Project omitted) |
| Claude auth | CredMan API key + Claude Code CLI OAuth; agent menu lists Claude when connected |
| Unified model selector | Header agent menu lists every authenticated option; selection sets chat agent |
| Claude chat send | Claude Code `claude -p` print JSON on a worker thread; local `claude-*` threads |
| Agent activity box | Updating current-turn status, elapsed time, Codex work/sub-agents and successful provider-reported file changes; [details and limits](agent-activity-acp.md) |
| Scintilla editor | Line numbers, lexers, find/replace/goto, wrap/whitespace |
| Project grant / lockdown | Unchanged; Access view documents modes |
| Terminal (ConPTY) | View → Terminal / Ctrl+`; profile discovery; agent policy setting |
| Settings sections | AI Providers, Editor, Terminal, Knowledge, MCP, Strata, Advanced |
| Knowledge / MCP / Strata stores | JSON under `%LOCALAPPDATA%\ScyllaGPT\`; Settings shows live status |
| MCP OAuth (HTTP) | Discovery + DCR + PKCE loopback + CredMan; Reauthenticate / Add / Check Now / startup freshness |
| MCP agent runtime | Enabled HTTP connections in active project scope are registered with Codex; bearer tokens pass from CredMan through child-only environment variables and are never serialized to TOML |
| MCP Manage / scope / stdio / Test | Edit form; project_scope UI; Custom HTTP\|Stdio; Test Connection (HTTP probe / stdio validate) |
| Security Settings hierarchy | Overview / Keyring / Project Environments / Execution Policy |
| Project Environments UI | Entity list + detail; unified Add Variable; inherit; no AppData path; no Create Keyring on env page |
| Execution Policy | Persisted in settings.json; human Allowed/Ask/Block enforced on protected terminal launch; agent never Allowed for protected |
| Env status + terminal env chooser | Status-bar Env chip; New Terminal environment override; project-root context menu |
| Knowledge permissions / health | Folder overrides + NoAccess; enable/disable; Refresh health; explorer KNOWLEDGE/SKILLS split |
| Knowledge pane resize | Drag the divider above KNOWLEDGE; original automatic height until first drag, then persisted DIP height; collapse/expand retains size |
| Strata library | Async local/remote project browsing and search; read-only document view with return-to-results; effective remote config; explicit project Publish/Pull; see [STRATA](strata.md) |
| Terminal session polish | Tab context menu Rename/Restart/Duplicate/Close; exit codes; profile cwd edit; Problems(dirty)/Output log |
| Keyring crypto core | App-level vault; no `get_secret_value`; Access/Keyring menu status |
| Project Security | Access pane summarizes grant, terminal policy, knowledge, MCP, Strata, Keyring |

## Deferred (iteration via micro-plans)

| Item | Why |
|------|--------|
| Command Palette / Quick Open | Next wiring slice |
| Cursor ACP account/chat | Discovery status and fixture-tested protocol foundation implemented; native transport, auth, execution controls and interactive approvals still pending; [evidence](agent-activity-acp.md) |
| Find in Project | Deferred |
| Knowledge search / @alias routing / instruction registry | Permission depth done; Phase 5–6 still open |
| Strata Context Inspector / capture / wizard | Human library browsing and document transfers implemented; automatic agent context/capture still deferred |
| Keyring recipes / SecretExecutionBroker | Phase 5 |
| Agent terminal approval dialogs | Policy stored; broker UX next |
| Terminal Ports / split / scrollback search | Plan Phase 2/4 |
| Access Activity / multi-folder ACL | Single project grant today |
| Recent Projects submenu | Deferred |
| Toast / ModalService | Compact MessageBox/prompt only |
| Minimap / LSP | Out of scope |

## Blocked / out of scope

| Item | Why |
|------|--------|
| AppContainer around Codex | Out of scope — policy + Job Object only |
| Revive archived Scylla Cage CLI | Archived under `archive/scylla-cage/`; not a product path |

## Third-party

- Scintilla 5.6.6 / Lexilla 5.5.3 under `scyllagpt/third_party/`

## Operator smoke (menu wiring Phase 1)

1. Rebuild / launch `scylla-workbench.exe`.
2. Open a file → File → Settings → edit → Back — caret/dirty restored.
3. Settings → AI Providers: Claude key / ChatGPT sign-in still work.
4. Access / Diagnostics / Shortcuts / Getting Started open in center pane.
5. Escape returns to editor from utility views.
6. Menus: Save disabled with no doc; Stop disabled when idle; wrap checkmarks sync.
7. Codex Send unchanged when default provider is OpenAI.
8. Model menu: OpenAI + Claude when both connected; Claude Send uses Claude Code print.
