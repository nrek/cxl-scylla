---
project: cxl-scylla
date: 2026-09-11
status: implemented; source validation passed; build blocked by local toolchain
---

# Model reasoning depth selector

Added a reasoning-depth dropdown beside the composer model selector. Its choices come directly from each Codex `model/list` row's `supportedReasoningEfforts`, so unsupported values are never offered and models without declared choices hide the control. Raw values remain the runtime API values while labels are presented as Extra High, High, Medium, Low, Minimal, and None.

Follow-up: the model picker now consumes the app-server catalog's `modelSpecialty` metadata. Language-specialized options render with `(lang)`, programming-specialized options with `(code)`, and general-purpose/unspecified models (including GPT-5.6-Sol) remain unsuffixed. The catalog's `isDefault` model is selected when the user has no saved model choice; an explicit saved or newly selected choice still wins. Specialty/default metadata participates in catalog refreshes. Tests cover the visible suffixes, GPT-5.6-Sol default selection, and propagation of both `gpt-5.6-sol` and `medium` to a turn payload.

Selecting a model resets depth to `medium` when that model supports it. Otherwise Scylla uses the model's declared `defaultReasoningEffort`, then the nearest middle supported option as a fallback. After that, the value changes only through the depth selector until another model is selected. The selected effort persists in `settings.json` as `reasoning_effort`, survives restart, appears in session snapshots, and is validated against the active model before persistence. Valid depth is sent to Codex as the `effort` field on `turn/start`; stale or unsupported values are omitted.

The provider catalog and active-model snapshot now carry supported/default effort metadata. Metadata changes invalidate the model catalog signature and refresh the dropdown even when model IDs and labels stay the same. Added native tests for middle/default selection and settings round trips, plus managed parser checks for provider-order options and selected effort.

Validation: source contract checks and `git diff --check` passed. Compilation remains blocked by the local toolchain: Visual Studio 2026 initializes but its compiler fails even CMake's trivial probe with D8037 (`cannot create temporary il file`), and PowerShell initialization reports 8009001d. Managed restore/build fails before compilation with NuGet `Value cannot be null (path1)`. No live visual/provider turn test was possible, and the running app was not replaced.

Context: reviewed current Scylla handoffs and the existing Scout blueprint/design handoff. No local blueprint or handoff files were found in Sanctum or Sentinel. No STRATA connector was exposed, so no STRATA queries were issued.
