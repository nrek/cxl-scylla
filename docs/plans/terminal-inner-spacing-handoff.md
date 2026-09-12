---
project: cxl-scylla
date: 2026-09-11
status: implemented; build validation blocked
---

# Terminal inner spacing

Added an 8 DIP margin on all four sides of the terminal anchor in [ChromeViews.cs](D:/projects/cxl-scylla/scylla-fluent/ChromeViews.cs). The native terminal HWND and surface cutout already follow this anchor's transformed origin and actual size, so the inset also reduces the terminal viewport and follows DPI scaling and resizing. The toolbar and session sidebar retain their existing layout.

Validation: inspected the anchor-to-HWND sizing and cutout path. Visual Studio MSBuild fails at startup with FileLoadException, including outside the sandbox. A dotnet build with workspace-local TEMP/TMP reaches dependency resolution but fails with NETSDK1060 reading the existing project.assets.json (null path1). Compilation and live visual verification remain outstanding. Review all four gutters after launch and while resizing or switching sessions.

Context: reviewed the previous Scylla terminal visibility/spacing handoff and local Scout blueprint alignment and design handoff. No blueprint or handoff files were found in Sanctum or Sentinel. No STRATA tools were available and no STRATA queries were issued.
