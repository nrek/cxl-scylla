---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Human-operated interactive terminal

The previous native stacking fix was insufficient: WinUI composition painted over the terminal even with its HWND above the content sibling. Live inspection found a visible RICHEDIT control with 38 characters while the user saw a blank panel. The running app also loaded the older normal-output DLL rather than the staged DLL. The earlier hidden-parent test could not detect WinUI composition coverage.

Added a shared native-surface region coordinator. Editor and terminal subtract their own rectangles from the Fluent composition surface; hiding either removes only its own rectangle. Terminal placement now maps island coordinates into the top-level window client area, matching the editor. Payload, session switches, terminal menus, unload, and disposal restore the terminal portion of the Fluent surface. Native HWNDs remain siblings of the private WinUI bridge.

The user operates the shell directly, including Bash and interactive SSH commands. Terminal output is not automatically forwarded to the agent. The explicit Terminal actions menu entry **Add output to chat draft** copies up to 65,536 characters of the selected terminal's rendered text into the existing composer draft. It does not send a message, execute a command, or subscribe to subsequent output. The user reviews/edits and sends the request. No agent control of the human terminal was added. Separately configured agent execution tools remain separate.

Validation:

- Fluent Release build passed.
- Isolated actual WinUI test passed: rendered shell text, composition exclusion, keyboard focus, native input caret, command input and returned output, editor and terminal visible together, Payload switching preserving the editor, and media preview preserving the terminal.
- Default and explicit WSL Ubuntu native smoke tests passed, including control keystrokes and two concurrent shells.
- Installer PowerShell syntax validation and tracked diff whitespace check passed.
- No remote server was contacted. Interactive SSH uses the shell; server-specific login was not tested.

Review [shared composition regions](D:/projects/cxl-scylla/scylla-fluent/NativeSurfaceCutouts.cs), [terminal lifecycle](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs), [editor integration](D:/projects/cxl-scylla/scylla-fluent/EditorPane.cs), [terminal review action](D:/projects/cxl-scylla/scylla-fluent/TerminalPanel.cs), [composer draft](D:/projects/cxl-scylla/scylla-fluent/AgentPane.cs), [window wiring](D:/projects/cxl-scylla/scylla-fluent/WorkbenchWindow.cs), and [actual UI regression](D:/projects/cxl-scylla/scylla-fluent/UiStressTest.cs).

The updated application is staged in `.tmpcl/terminal-build`. The [installer](D:/projects/cxl-scylla/tests/install-terminal-fix.ps1) waits for normal app/broker shutdown, backs up the four replaced runtime files, copies them into the normal launch folder, verifies SHA-256 hashes, and rolls back on copy/verification failure. It does not terminate the app or its shell sessions. Installation status is written to `.tmpcl/terminal-build/install-terminal.log`. The user must close Scylla normally and wait for installation before reopening it.

Context remains limited to cxl-scylla, cxl-sanctum, cxl-scout, and cxl-sentinel. Reused the project blueprint/handoff review from the preceding task and read the terminal visibility handoff. No callable STRATA tools were exposed, so no STRATA queries were issued. Unrelated workspace changes are preserved.
