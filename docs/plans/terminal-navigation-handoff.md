---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Open terminal navigation

Replaced the terminal session dropdown with a persistent, scrollable right-side list on the Terminal surface, matching the supplied Cursor reference. The list occupies 32% of the panel width, capped at 240px. The native terminal anchor occupies the remaining width, so its composition cutout cannot cover the list. Payload hides both the list and native terminal.

Rows show a numbered profile/custom label, label color, selection highlight, an Exited suffix when applicable, full-label tooltip, and a kill button. Click or keyboard selection switches sessions. Right-click a row to rename, change color, copy its output into the chat draft, or kill that specific session; F2 renames the selected session. Header +, profile selection, and actions remain available. Closing an inactive session preserves the active terminal; closing an active session selects its nearest remaining neighbor. Closing the last session leaves the existing empty state and + can create another. List containers survive selection/status refreshes to preserve keyboard focus. Splitting terminals is outside this change.

Review:

- [Session list and lifecycle](D:/projects/cxl-scylla/scylla-fluent/TerminalPanel.cs)
- [Terminal/Payload layout](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs)
- [Delete icon](D:/projects/cxl-scylla/scylla-fluent/Design.cs)
- [UI regression coverage](D:/projects/cxl-scylla/scylla-fluent/UiStressTest.cs)

Validation: Fluent Release build passed. The isolated actual WinUI terminal test passed creation, selection, inactive-session hiding, narrow-window list/native bounds, inactive close, final close, and recreation. Existing composition, shell prompt, focus/caret, command input/output, Payload switching, and editor coexistence checks also passed. The first test run used sibling order to locate the second native control; corrected the test to use its stable control ID and reran successfully. Tracked diff whitespace check passed. Rename/color and mouse/context-menu interaction retain manual acceptance coverage.

The runnable build is staged at `D:/projects/cxl-scylla/.tmpcl/terminal-navigation/`; the normal application output and existing installer staging were not replaced. Running user sessions were not stopped. Test evidence is in [ui-stress.log](D:/projects/cxl-scylla/.tmpcl/terminal-navigation/ui-stress.log).

Context was restricted to cxl-scylla, cxl-sanctum, cxl-scout, and cxl-sentinel. Read Scylla Fluent guidance and recent terminal handoffs, plus Scout's local blueprint alignment and design handoff. No blueprint/handoff files were found in Sanctum or Sentinel. No callable STRATA tools or MCP resources were available; no STRATA search/recent calls or unfiltered workspace queries were issued. Existing unrelated workspace edits were preserved.
