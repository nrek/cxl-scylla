---
project: cxl-scylla
date: 2026-09-11
status: implemented
---

# Project chat history drawer

The right pane now starts with a collapsed CHATS drawer. Its checkboxes come exclusively from the same open roots used by Files. All open roots initially start selected; CHATS shows a count only above one. Selection changes filter the union of matching chats without changing Files, agent grants, or the active conversation. Zero selections shows a prompt to select projects. Search and date grouping apply within the selection. Added roots start selected, removed roots disappear, and polling preserves the user's choices. Selection and drawer expansion are session-only.

Each root has a deterministic color dot with its path in a tooltip. Matching selected roots appear as right-aligned dots beside each chat title. Membership uses the stored owning project's primary root or provider cwd, plus bounded, case-insensitive root-name mentions in saved visible messages, title, and preview. Injected user context is stripped before matching. Opening several roots alone does not tag every chat with all of them. Existing account, deletion, archive, and open-project visibility rules remain enforced. Provider-only chats have cwd/title/preview matching until their messages are cached. Same-name folders are distinguished by full-path tooltips, although a bare name mention matches both.

Working chats show a compact spinner and Working label, with the active sub-agent count when reported. Approval waits, completion, failure, interruption, and disconnection use text labels; completion uses a checkmark. Finished background badges remain until the chat is visited, even when filtered out in the meantime. Activity comes from native per-thread runtime state and is in memory, not reconstructed from transcript prose or persisted across app restarts. Stable rows retain their controls between polls.

The native snapshot now carries open roots, per-chat project paths, busy state, activity phase, and active-agent count. Claude/API worker completions are tied to the originating thread so switching chats cannot redirect the final message or status into another chat. The existing limit of one simultaneous Claude/API worker remains; Codex retains its per-thread concurrency. Runtime disconnection ends background Codex activity.

Review: scylla-fluent/SidePanes.cs (drawer and row UI), ChatHistoryState.cs (selection/colors/unread completion state), NativeCore.cs (snapshot contract), scyllagpt/include/scyllagpt/chat_projects.h (project matching), session.h/session.cpp (per-thread activity and worker completion ownership), and src/core/scylla_core_c.cpp (snapshot export). Regression coverage is in tests/fluent-history-tests and scyllagpt/tests/test_runtime_domain.cpp.

Validation: Fluent Release build and native shared-core/test builds passed. All 17 managed history checks and seven new native project/background-activity assertions passed. The native suite still reports the same three failures recorded in the previous handoff: Plan prefers established Knowledge plans folder; mcp templates count; mcp template ids. Output: .tmpcl/chat-history-native-tests.log. Review build: .tmpcl/chat-history-build/scylla.exe. Live UI, provider execution, and visual/DPI acceptance have not been exercised. No running app was restarted.

Manual acceptance: add three roots in Files; open CHATS and verify only those roots appear, each with a dot. Select two, search, then clear both and verify the empty state. Open a chat mentioning two selected roots and verify two matching dots. Start work, switch chats, and verify Working changes to Completed; reopen the finished chat and verify the badge clears. Repeat with cancellation and a provider error. Remove/add a Files root and verify the drawer updates without reselecting previously unchecked roots.

Context: read the local verbose-agent-progress and markdown-images handoffs and agent activity documentation. No blueprint file or STRATA connector was available; no STRATA search/recent calls or cross-project retrievals were made.
