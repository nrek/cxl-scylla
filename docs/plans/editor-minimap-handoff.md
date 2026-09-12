---
project: cxl-scylla
date: 2026-09-11
status: implemented; source validation passed; build blocked by local toolchain
---

# Editor minimap

Implemented the shared plan at `D:\projects\.cursor\plans\scylla-workbench-editor-minimap-plan.md`. Settings → Editor now includes an opt-in Show minimap toggle. The application preference persists as `show_minimap`, defaults to false for fresh and existing settings, and applies live without recreating the primary editor.

Each open text document can own a secondary Scintilla view sharing the primary document pointer. The minimap is read-only, removed from tab focus, has no caret, context menu, margins, whitespace, wrapping, or scrollbars, and uses the existing syntax styles at a reduced zoom. A translucent selection shows the primary visible range. Click, drag, and wheel input navigate the primary editor and return focus without moving its caret. Tabs retain their own minimap view; minimaps are destroyed before their primary view. The right-side overview is 100 DIP wide and auto-hides below 500 DIP without changing the preference.

Validation added settings round-trip assertions for enabled, disabled, and legacy/default behavior. Source contract checks passed for settings persistence, managed/native ABI wiring, shared-document ownership, passive-view configuration, navigation handlers, fixed-width layout, and narrow-pane auto-hide. `git diff --check` passed.

Native and Fluent compilation could not be completed in this environment. The existing CMake cache references an unregistered Visual Studio 2022 instance. Visual Studio 2026 CMake and direct compiler attempts either hang during environment initialization or fail creating compiler temporary IL files, including outside the sandbox. `dotnet build` and restore fail before compilation with NETSDK1060 / NuGet `Value cannot be null (path1)` while loading the existing assets. Live UI, DPI, performance, and large-file acceptance remain pending after the local toolchain is repaired.

## Theme and pointer follow-up — 2026-09-11

The initial implementation shared Scintilla's document pointer but assumed its visual style table was shared too. It is view-local, so the minimap rendered shared lexer style numbers through Scintilla's default white theme. Minimap creation now copies every style's foreground, background, font, fractional size, weight, italic, underline, EOL-fill, and case from the primary editor before applying passive minimap behavior. The minimap subclass also handles `WM_SETCURSOR` with `IDC_HAND`, making its click-and-drag navigation affordance explicit. Existing mouse capture and navigation handling remains in place.

Context: reviewed the Scylla editor scrollbar and editor/tabs handoffs, plus Scout's local blueprint alignment and design handoff. No local blueprint/handoff files were found in Sanctum or Sentinel. STRATA tools were unavailable, so no STRATA queries were issued.
