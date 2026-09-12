---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Editor terminal and payload panel

Fluent now nests the resizable bottom panel inside the Editor column. Files, Agent and History retain their full height. Panel height is capped to leave editor space, and hiding the Editor also hides the native terminal. Terminal and Payload use borderless buttons with an amber active label. Removed unwired Problems, Output and Ports tabs. Older saved tab names fall back to Terminal.

The existing post-activation terminal startup creates the configured default terminal profile when the panel is visible; View → Terminal opens it when hidden. Creation remains deferred until the native window exists, uses the top-level HWND, is idempotent, and now ignores disposed windows. Startup errors appear in the terminal area. A deferred startup callback checks window closure and panel visibility. Selected Payload state is restored; the terminal instance stays alive while switching tabs.

Payload displays timestamped, single-line session traffic: outgoing runtime requests, notifications and results; incoming runtime events, replies and tool calls; user submissions; and completed local/API provider replies or errors. Retains the newest 200 entries, truncates previews to 2,048 UTF-8 bytes with a marker, escapes embedded newlines, and redacts structured credential/header/environment fields. Free-text prompts and tool output remain visible. This is an in-memory session log, not a persisted audit trail or HTTP packet capture. API provider entries show user submissions and final replies rather than full HTTP envelopes. Runtime-emitted MCP/tool events expose agent requests through Scylla; traffic not reported by the runtime is not independently intercepted.

Review:

- [Panel and terminal lifecycle](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs)
- [Editor column layout and polling](D:/projects/cxl-scylla/scylla-fluent/WorkbenchWindow.cs)
- [Managed snapshot parsing and buffer](D:/projects/cxl-scylla/scylla-fluent/NativeCore.cs)
- [Session logging](D:/projects/cxl-scylla/scyllagpt/src/domain/session.cpp) and [declarations](D:/projects/cxl-scylla/scyllagpt/include/scyllagpt/session.h)
- [Native snapshot and tab persistence](D:/projects/cxl-scylla/scyllagpt/src/core/scylla_core_c.cpp) and [settings compatibility](D:/projects/cxl-scylla/scyllagpt/src/storage/settings.cpp)
- [Native regression tests](D:/projects/cxl-scylla/scyllagpt/tests/test_payload_log.cpp), [test target](D:/projects/cxl-scylla/scyllagpt/CMakeLists.txt), and [managed regression tests](D:/projects/cxl-scylla/tests/fluent-progress-tests/Program.cs)

Validation: Release native shared core, broker, full native test target and Fluent shell built successfully. Seven focused native Payload checks passed, including incoming tool-event capture, nested credential redaction, retained arguments, truncation, escaped newlines, bounded retention and oldest-entry eviction. Managed payload transport/legacy fallback and all 14 existing progress checks passed. Diff whitespace check passed. The full native test executable was built but not run. Updated app and native DLL are staged together at `.tmpcl/payload-build/`.

Live acceptance remains: launch the staged build, confirm default shell startup with Terminal visible, type commands, switch Terminal/Payload repeatedly, resize/hide the Editor and panel, and send an agent request that calls a tool. Check that Agent/History/Files extend to the status strip and that the native terminal never overlaps Payload. No running app was restarted or replaced.

Context: reviewed local Scylla MCP execution status, editor scrollbar and verbose progress handoffs and the Scout design handoff. No local blueprint was found. STRATA tools/resources were unavailable, so no STRATA searches or unfiltered workspace queries were issued. Changes are confined to cxl-scylla and preserve existing workspace edits.
