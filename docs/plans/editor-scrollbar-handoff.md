---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Source editor scrollbar

Observed the running Fluent app with README.md in source view: the vertical scrollbar had a bright system track. Capture: `.tmpcl/scrollbar-before.png`.

The Win32 shell installed the shared thin scrollbar controller in its window setup, but Fluent creates Scintilla through the native core API and bypassed that setup. `editor_create` now installs the shared controller with `theme().editor` as its track color. Both editor hosts receive the existing thin, flat thumb, hover/drag behavior and high-contrast opt-out.

Review `scyllagpt/src/ui/editor_host.cpp` and `scyllagpt/tests/test_editor_handle.cpp`. The native editor integration test checks skin ownership outside high-contrast mode and verifies page-down/top scrolling in a multiline buffer, alongside existing handle lifecycle checks.

Context: reviewed the editor-tabs handoff and scrollbar documentation in `scyllagpt/docs/markdown.md`. No STRATA connector or local blueprint was available; no unfiltered or cross-project service queries were issued.

Validation: Release native core, editor-handle tests, shared Markdown/scrollbar tests and Fluent build passed. Both native test executables exited 0; the editor test verified skin attachment and actual scrolling. `git diff --check` passed for the editor change. Updated Fluent output and native runtime are staged at `.tmpcl/chat-history-build/`. The currently running app was not restarted; visual verification of the fixed README scrollbar remains pending in the updated build.
