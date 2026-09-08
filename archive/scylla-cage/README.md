# Archived — Scylla Cage

Historical AppContainer CLI (`scylla.exe`), capability probe (`scylla-probe.exe`), and process-tree fixture (`scylla-test-parent.exe`).

**Not built.** The product path is Scylla Workbench under `scyllagpt/`. Do not revive these targets in the root CMake project.

| Path | Former role |
|------|-------------|
| `src/scylla/` | AppContainer launch, ACL grants, strict sessions |
| `src/probe/` | Sandbox capability probe |
| `src/test_parent/` | Process-tree test fixture |
| `docs/` | Former cage architecture and security-model notes |

Prior launcher decommission pattern: `archive/scylla-ui-launcher/`, `archive/hyperv-poc/`.
