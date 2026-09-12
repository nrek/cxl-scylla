---
project: cxl-scylla
date: 2026-09-12
status: implemented
---

# File → New Window and multi-instance state

File → New Window is the first File-menu action. It starts a separate `scylla.exe` process with a generated, validated window identifier. Normal launches remain single-instance and continue activating the primary window.

Explicit secondary windows use `%LOCALAPPDATA%/ScyllaGPT/windows/<id>` for their settings snapshot, workspace database, browser folders, recovery files, attachments, launch log, WebView profile, runtime files, and isolated Codex home. The snapshot retains user preferences but clears the active project, active thread, drafts, and pinned tabs. Codex authentication is seeded into the isolated home. Shared knowledge, STRATA, MCP, terminal-profile, environment, panel-preference, and brokered-connection stores remain under the main ScyllaGPT directory. Native and managed atomic writes now use process-unique temporary names so shared-store saves cannot truncate one another's temporary files. A normal launch clears any inherited window id, making the command-line token the authority for state selection.

Review: `scylla-fluent/WindowInstance.cs`, `App.xaml.cs`, `ChromeViews.cs`, `WorkbenchWindow.cs`, `BrowserPreferences.cs`, `PanelPreferences.cs`, `ChatAttachments.cs`; `scyllagpt/include/scyllagpt/paths.h`, `src/platform/paths.cpp`, `src/platform/file_io.cpp`; and `tests/window-instance-tests`.

Validation: focused managed tests passed argument validation, path isolation, and child-process construction. The native shared DLL compiled successfully with the isolated-root and traversal-rejection cases present in the runtime-domain suite. The full native test target is currently blocked by an unrelated existing call to private `Session::ensure_selected_model` at `test_runtime_domain.cpp:76`. Fluent Release built successfully to `.tmpcl/new-window-build` with only the existing unused `_windowInteraction` warning. A live hidden smoke test launched two app-host processes concurrently, verified both remained alive and created distinct state roots, stopped only those processes, and removed their synthetic state folders. Direct File-menu visual acceptance remains.

Context: retrieved local scoped handoffs/blueprints and issued separate exact-project STRATA search/recent requests for cxl-scylla, cxl-sanctum, cxl-scout, cxl-sentinel, and cxl-strata. Only matching project metadata was considered; the relevant Scylla context reinforced preserving normal single-instance behavior and avoiding stale build-tree assumptions.

Manual acceptance: choose File → New Window; verify a second process/window opens, choose different project folders in each, start simultaneous chats/terminals, restart neither window, and confirm project/layout/chat changes remain independent.
