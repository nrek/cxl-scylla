---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Editor tabs and Files menu

Removed the normal full-path label above Source/Preview. Its header row collapses when empty; error and save/status messages still appear when needed. Opening or renaming an ordinary file clears the label.

With five or more open documents, filenames longer than 18 text elements display the first 12, an ellipsis, and the final five. This preserves the filename ending and avoids splitting Unicode text elements. Closing back below five restores full labels. Tabs and their activation labels show the full filename in a tooltip; close buttons keep their existing full-filename tooltip and accessible label.

Manage Files is now the first context-menu option on Files tree entries and empty tree space, invoking the existing folder-management dialog. Knowledge menus retain their existing actions.

Review: scylla-fluent/EditorPane.cs and scylla-fluent/SidePanes.cs. Validation: Fluent Release build passed; output is .tmpcl/chat-history-build/scylla.exe, including the prior chat drawer changes. No app restart or live visual test was performed. Manual checks: open four long filenames, add a fifth, hover a shortened tab, close back to four, and right-click a Files entry and empty tree space.

Context: reviewed the cxl-scylla chat-history-drawer handoff. No STRATA connector or local blueprint was available; no cross-project retrievals were made.

Follow-up: editor tabs now have `Design.GapSm` (8 logical pixels) of top padding, matching their existing bottom padding. Review `scylla-fluent/EditorPane.cs` at the `_tabsHost` initialization. Release build passed using `.tmpcl/build-history.ps1`; output remains `.tmpcl/chat-history-build/scylla.exe`. Live visual verification was not performed.
