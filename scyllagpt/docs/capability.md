# Scylla Workbench capability checklist

Implementation status for the native Workbench client. This is not a claim that Workbench is ChatGPT.com, Claude Code, or an AppContainer.

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
| Scintilla editor | Line numbers, lexers, find/replace/goto, wrap/whitespace |
| Project grant / lockdown | Unchanged; Access view documents modes |

## Deferred (wiring Phase 2+)

| Item | Why |
|------|--------|
| Command Palette / Quick Open | Next wiring slice |
| Find in Project | Deferred |
| Access Activity / multi-folder ACL | Single project grant today |
| Recent Projects submenu | Deferred |
| Toast / ModalService | Compact MessageBox/prompt only |
| Minimap / LSP | Out of scope |

## Blocked / out of scope

| Item | Why |
|------|--------|
| AppContainer around Codex | Policy + job only |
| Cage AppContainer grants from Workbench menus | Separate Scylla Cage product |

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
