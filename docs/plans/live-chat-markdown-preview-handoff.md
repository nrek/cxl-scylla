---
project: cxl-scylla
date: 2026-09-11
status: fixed; Fluent build passed
---

# Live Chat Log and Markdown Preview fixes

Public assistant reply deltas now render in the active Chat Log while a turn is generating or awaiting action, without requiring the user to leave and reopen the chat. The native session already exposed the active thread stream and the shell polls every 250 ms; `AgentPane` now always presents that stream. Existing transcript-prefix comparison continues to replace only the changing transient reply and prevents a duplicate when the completed response enters persisted history. The Verbose Agent Progress preference no longer gates public reply text.

Markdown Preview no longer allows the native Scintilla Source HWND to reappear over the rendered XAML preview. Both queued repositioning and direct show paths now require the editor surface to be visible and the preview surface to be hidden. This addresses the observed render blink followed by Source retaining the editor space.

Review `scylla-fluent/AgentPane.cs` and `scylla-fluent/EditorPane.cs`.

Validation: the production Fluent Release x64 build passed. Output is `.tmpcl/chat-history-build/scylla.exe`. The existing CS0649 warning for `WorkbenchWindow._windowInteraction` remains. `git diff --check` passed. No running application was replaced and no live GUI interaction was performed.

Context: reviewed the cxl-scylla verbose-agent-progress, Markdown images, editor-open-crash, and editor-tabs handoffs. No local handoff was found for cxl-sanctum or cxl-sentinel; cxl-scout's design handoff was outside these implementation paths. No STRATA connector was exposed, so no STRATA calls were issued.
