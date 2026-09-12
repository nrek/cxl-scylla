---
project: cxl-scylla
date: 2026-09-10
status: implemented
---

# Verbose Agent Progress

Settings → Providers has a right-aligned “Verbose Agent Progress” label and toggle on the Providers heading row. The switch uses Fluent accent/gray states with empty OnContent/OffContent and an accessible name. It defaults off, including for existing settings files, and persists as verbose_agent_progress in settings.json. A failed save restores the switch and reports the error.

When enabled, Fluent displays the current provider-supplied reply stream as a transient assistant row during generating/awaiting_action. Completed history remains untouched. Turning it off hides that row; completion, failure and interruption use the session's existing history without adding a duplicate. The preference takes effect through the existing settings refresh (about two seconds). The existing activity card stays in place. This shows public replies supplied by the provider, not private reasoning or a new tool-event log; providers without streaming replies retain their existing behavior.

Review: EditorPane.cs (header control), NativeCore.cs (settings parsing and transient history), AgentPane.cs and WorkbenchWindow.cs (rendering hookup), settings.h/settings.cpp and scylla_core_c.cpp (native persistence/patch API). Provider actions and HTTP model refreshes in session.cpp reload this preference before saving session settings to avoid overwriting it with a stale copy. ChatAttachment's unchanged data model is extracted to its own file so the managed regression runner can compile the production snapshot code without WinUI.

Validation: Fluent Release build passed with zero warnings/errors; native shared core and test targets also built successfully. All 14 managed progress checks passed, covering defaults, toggling, immutable history, repeated polls, completion/failure/interruption, empty streams, approval waits and thread isolation. All five new native settings assertions passed (default off, save on, save off, restart persistence and legacy defaults). The full native suite exited 1 with three failures outside the changed behavior: “Plan prefers established Knowledge plans folder”, “mcp templates count”, and “mcp template ids”. Full output is in .tmpcl/verbose-progress-native-tests.log. Visual placement and live provider interaction have not been manually tested; no running app was replaced or restarted. The validated Fluent build is in .tmpcl/progress-build; the native DLL is in build/Release.

Context: reviewed the project activity documentation and recent security-file-import handoff. No STRATA connector is exposed in this session, so no STRATA searches were issued.

Manual acceptance: open Providers, confirm the switch is gray/off and aligned to the right of the heading; turn it on and restart to confirm persistence; send a prompt and observe replies arriving in chat; turn it off during generation and confirm the transient reply hides while the finished reply still appears once.
