---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Terminal controls and default startup

Fluent's terminal was blank because it never called the native terminal poll API. A dedicated 50ms timer now drains and renders output for every live terminal, independently of agent polling. Startup creates one configured default shell after window activation, including when the panel is hidden; it preserves the selected Terminal/Payload surface. Closing the final terminal leaves an intentional empty state with a + action.

The panel header now has a session selector, + for the configured default, a profile dropdown containing enabled shells and Select default profile, and an actions menu. Each session supports Rename (also F2 on the selector), six label colors, and Kill terminal. Right-click the selector opens its actions. Exited sessions retain their output with an Exited label. Labels and colors are session-local and are not persisted across app restarts. Split view and icon customization are deferred.

An additive native `scylla_terminal_create_profile` export accepts an explicit enabled profile id. Unknown/disabled explicit ids fail instead of falling back; an empty id uses the existing default resolution. The original creation export remains compatible. Package the updated DLL with the Fluent build.

The installed WSL launch path also rejected the unnecessarily quoted distribution argument (`-d "Ubuntu"`) with WSL_E_DISTRO_NOT_FOUND. Detected profiles now quote distribution names only when whitespace requires it. The real default Ubuntu smoke test passes with `-d Ubuntu`.

Only the selected session's native HWND is visible. Position changes invalidate the layout cache; terminal menus and rename dialogs temporarily hide the HWND so it cannot obscure XAML overlays. Shutdown destroys all sessions and stops their polling timer.

Review:

- [Terminal controls and polling](D:/projects/cxl-scylla/scylla-fluent/TerminalPanel.cs)
- [Panel lifecycle and positioning](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs)
- [Window startup](D:/projects/cxl-scylla/scylla-fluent/WorkbenchWindow.cs)
- [Managed interop](D:/projects/cxl-scylla/scylla-fluent/NativeCore.cs)
- [Native implementation](D:/projects/cxl-scylla/scyllagpt/src/core/scylla_core_c.cpp)
- [Public ABI](D:/projects/cxl-scylla/scyllagpt/include/scylla_core_c.h)
- [WSL launch arguments](D:/projects/cxl-scylla/scyllagpt/src/domain/terminal_profiles.cpp)
- [Native shell smoke test](D:/projects/cxl-scylla/tests/terminal-smoke.ps1)

Validation: Native shared core, broker, native regression executable, and Fluent Release builds passed. The native smoke test passed against the configured WSL: Ubuntu default: invalid explicit id rejected, default and explicit Ubuntu sessions produced startup output, a command returned the expected output, and both sessions remained alive. The test needed execution outside the sandbox for WSL access. The full native regression suite reports one unrelated failure: `Plan prefers established Knowledge plans folder`; terminal profile/screen checks passed. Diff whitespace check passed. App, updated core DLL, and broker are staged together in `.tmpcl/terminal-build/`. Live visual acceptance remains for rename/color, menu clipping, narrow editor widths, switching sessions/Payload, hiding/resizing the panel, and closing a terminal running a server. The running app has not been replaced or restarted.

Context: reviewed Scylla's editor/terminal/payload handoff and Fluent guidance, plus local Scout blueprint summaries and handoff guidance. No local blueprint/handoff files were found for Sanctum or Sentinel. No STRATA tools or resources were available, so no STRATA search/recent calls or unfiltered workspace queries were made. Existing unrelated edits were preserved; implementation is confined to cxl-scylla.
