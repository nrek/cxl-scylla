---
project: cxl-scylla
date: 2026-09-12
status: implemented
---

# Terminal mouse settings and Payload colors

Settings → Terminal has a two-column top card: Default agent policy on the left and Terminal mouse behavior on the right. The choices are Highlight Copy (default: copy on mouse selection release, right-click paste), Right Click Copy (copy selection and collapse it on the first right-click, paste on the next), and Right Click Menu (Copy, Paste, Clear, New Terminal). Copy is disabled when there is no selection; Paste is disabled when no text is available. Clear clears the local terminal display/history, without sending a shell command. New Terminal uses the existing default-profile creation path. Keyboard copy/paste and bracketed paste still use the existing native input path. The saved mode applies to existing and new Fluent sessions.

Payload's first context-menu item is Color coding, followed by Copy and Copy all. The toggle colors timestamps (including the native time-only format), directions, JSON keys, strings, numbers, literals and delimiters. It preserves the exact text, including escapes and newlines, and handles incomplete/truncated JSON. Inline creation is bounded and remaining text falls back to plain rendering. Existing log redaction and retention are unchanged.

Both preferences persist in `%LOCALAPPDATA%/ScyllaGPT/panel-preferences.json`; highlighting is initially off. Ship the updated native core with Fluent because two additive terminal exports are required.

Review: `scylla-fluent/EditorPane.cs`, `PanelPreferences.cs`, `TerminalPanel.cs`, `PayloadPanel.cs`, `PayloadSyntax.cs`, `ChromeViews.cs`, `NativeCore.cs`; `scyllagpt/include/scylla_core_c.h`, `include/scyllagpt/terminal_host.h`, `src/core/scylla_core_c.cpp`, `src/ui/terminal_host.cpp`.

Validation: native shared core and broker built in `build/terminal-mouse/Release`; Fluent Release built in `.tmpcl/terminal-mouse-build/` with the matching DLL and broker. Native `scylla-terminal-mouse-tests` passed 11 checks using a hidden cmd session and intercepting copy/paste messages without changing the user's clipboard. Managed `tests/panel-behavior-tests` passed highlighting, text-preservation, truncated-string, Unicode, and bounded-output checks. Fluent build has only the existing unused `_windowInteraction` field warning. Interactive visual acceptance remains for the two-column card, popup-menu actions, and color readability. The running application has not been restarted.

Context: reviewed the scoped Scylla terminal/payload handoffs and Scout local blueprint. Each of the five scoped projects received its own exact-project STRATA search/recent request. All failed because the index lacks `published_at`/`sync_ignored_at`; no cross-project results were used and no index migration was performed. Existing packaging edits were preserved.
