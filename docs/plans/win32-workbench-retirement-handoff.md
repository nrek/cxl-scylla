---
project: cxl-scylla
date: 2026-09-12
status: implemented
---

# Win32 Workbench retirement

The native Win32 `scylla-workbench.exe` application is retired. The CMake executable target, entry point/resources, shell window, native settings pages, command registry, Workbench-only panel/rendering sources, orphaned Win32 UI kit, and their dedicated tests were removed. Native editor, terminal, domain, storage, broker, STRATA adapter, and test code still required by the WinUI 3 application remain. The STRATA adapter and runtime-domain test were renamed to remove obsolete Workbench branding.

Packaging now cleans the Fluent and STRATA staging directories before repopulating them and rejects a stale legacy executable before WiX runs. The Start menu continues to target only `scylla.exe`.

Documentation now identifies `scylla.exe` as the sole desktop product and describes `scyllagpt/` as its native runtime. Active build, STRATA, ACP, broker-workflow, contribution, installer, and documentation-index instructions no longer direct users to build or launch the retired application. Historical handoffs retain historical references.

System audit: no installed `scylla-workbench.exe`, legacy shortcut, uninstall entry, or running process was found. Three generated legacy executables were deleted. A final repository cleanup also removed 41 obsolete generated files (old adapters, import libraries, object files, recipes/logs, and the retired screenshot) plus five generated legacy-target directories.

Validation completed for the retirement path: a fresh CMake/Ninja Release build produced `scylla-core.dll`, `scylla-broker.exe`, and focused agent/terminal test executables. The agent-activity/ACP and terminal-mouse suites passed. Fluent Release built successfully, and all five managed regression executables passed. The retired application target is absent from Ninja's target graph; no `scylla-workbench.exe` remains in the repository or package staging; staged payloads contain `scylla.exe` and `strata.exe`; packaging scripts parse; and `git diff --check` passes.

Two broader checks are independently blocked. The Python STRATA suite cannot start because this machine's Python raises WinError 10106 while importing `_overlapped`, including outside the sandbox. WiX resolves both staged payloads after adding explicit preprocessor constants, but the existing per-user recursive harvesting design fails MSI ICE03/ICE38/ICE64 validation for hundreds of self-contained files, so it does not emit a release MSI. Resolving that installer architecture requires a separate choice between per-machine installation, registry-keyed per-user components, or deliberately suppressed ICE validation; this retirement does not silently change installation scope.

Context: local handoffs/blueprints were inspected only for cxl-scylla, cxl-sanctum, cxl-scout, cxl-sentinel, and cxl-strata. Separate exact-project STRATA search/recent requests were issued for all five projects; only matching project metadata was considered.
