---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Terminal visibility and spacing

The native terminal could remain underneath the Fluent desktop content HWND: `ShowWindow(SW_SHOWNA)` preserves sibling z-order. Showing a terminal now uses `SetWindowPos(HWND_TOP)` with `SWP_NOACTIVATE`, preserving its bounds and bringing it above the opaque content sibling. Hiding still uses `SW_HIDE`; existing session, Payload, and menu visibility handling remains in effect.

Removed the bottom panel's outer 12px gutter. Padding belongs to the header (8px horizontal, 4px vertical), with a full-width terminal body. The session selector and toolbar icons are 28px high. Plus, profile chevron, and actions use borderless Fluent glyphs with tooltips and accessible names; icon buttons are 28px square with 4px gaps.

Review:

- [Native visibility](D:/projects/cxl-scylla/scyllagpt/src/ui/terminal_host.cpp)
- [Toolbar](D:/projects/cxl-scylla/scylla-fluent/TerminalPanel.cs)
- [Panel spacing](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs)
- [Icon glyphs](D:/projects/cxl-scylla/scylla-fluent/Design.cs)
- [Native integration regression](D:/projects/cxl-scylla/tests/terminal-smoke.ps1)

The smoke test creates an opaque sibling above a terminal, then checks that showing raises the terminal and hiding clears its visibility. It also checks rendered control text and sends WM_CHAR/Return through the actual terminal control to execute a WSL command. The sibling regression failed against the previous DLL with `Terminal is obscured by its content sibling`.

Validation: Release native shared core, broker, regression executable, and Fluent builds passed. The updated native smoke test passed for the default and explicit WSL Ubuntu profiles: sibling stacking, hide/show, rendered control text, command execution through control keystrokes, and two concurrent sessions remaining alive. The test waits for printable screen text because startup may first emit only VT control sequences. The full native regression suite still reports the previously recorded unrelated failure `Plan prefers established Knowledge plans folder`; terminal checks passed. Git diff whitespace check passed.

The app, updated core DLL, and broker are staged together in [terminal-build](D:/projects/cxl-scylla/.tmpcl/terminal-build). Launch that build to review the changes; no running app has been restarted. Live Fluent visual acceptance remains necessary for spacing at different DPI/window widths, pointer focus, and menus.

Context: reviewed Scylla terminal controls and editor/terminal/payload handoffs, Fluent guidance, and local Scout blueprint alignment and design handoff. No blueprint/handoff files were found for Sanctum or Sentinel. No callable STRATA tools were exposed; no STRATA queries, including unfiltered queries, were issued. Changes are confined to cxl-scylla and preserve unrelated existing edits.
