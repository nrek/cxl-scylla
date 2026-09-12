---
project: cxl-scylla
status: restart_required
---

# Database tools deployment repair

The running Fluent executable's directory lacked scylla-broker.exe, although
build/Release contained it. start_session_broker in scylla_core_c.cpp silently
returns when this helper is absent, leaving the agent without scylla_query or
scylla_ssh regardless of saved Security connections.

Copied the existing built helper beside the running Fluent executable. Added a
Fluent build target that deploys both native runtime binaries and rejects missing
inputs. Added a missing-helper message in Security and corrected build order in
the Fluent README. Existing connection settings and credentials were not changed.

Validation: the deployed helper completed MCP initialize and tools/list with a
dummy token and advertised scylla_query and scylla_ssh. No database operation was
performed. The deployment target passed into an isolated output directory; a
missing-native-directory case failed as expected. Hashes matched for both staged
artifacts and for the helper copied to the running app's directory. MSBuild needed
TEMP/TMP directed to the workspace because its default temporary directory resolved
under C:/WINDOWS. Git diff --check passed. The Security UI change has not been built
or visually tested.

Restart Scylla to register the helper in a fresh agent runtime, then retry
@synq-hot-rds and retrieve the latest ex_binance.prices row after inspecting its
timestamp column. The live connection, project visibility, credential unlock and
query authority still require end-to-end verification. The current session cannot
acquire newly deployed tools. No running app was terminated.

Context: reviewed local cxl-scylla MCP and Security handoffs. No STRATA/blueprint
retrieval tools were exposed; no unfiltered or cross-project query was made.
