---
project: cxl-scylla
date: 2026-09-11
status: fixed
---

# Editor file-open crash

Fixed the process crash when opening a file from the Files tree. The local application log showed a `NullReferenceException` in `EditorPane.HideScintilla`, reached from `ActivateDoc` before `_active` was assigned.

The cause was the lifted nullable comparison `_active?.MinimapHandle != IntPtr.Zero`: when `_active` is null, that comparison evaluates true, after which the branch dereferenced `_active`. Replaced all five occurrences in `EditorPane.cs` with an explicit `_active is not null` guard. This also fixes the same logged crash during editor unload and application shutdown and protects minimap timer/layout paths.

Validation: `scylla-fluent/Scylla.csproj` Release x64 build passed via `.tmpcl/build-history.ps1`. Output: `.tmpcl/chat-history-build/scylla.exe`. One pre-existing CS0649 warning remains for `WorkbenchWindow._windowInteraction`. No live GUI smoke test was performed.

Context: reviewed the recent cxl-scylla editor-tabs handoff and local STRATA/agent handoff content. No STRATA connector or blueprint retrieval tool was available in this session; no unfiltered or cross-project STRATA query was issued.
