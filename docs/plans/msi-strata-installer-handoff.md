# MSI + STRATA installer handoff

## Implemented

- Added a WiX 5 per-user MSI project under `installer/`.
- Added a setup page for enabling STRATA, choosing Solo/Team mode, and entering the HTTPS team endpoint.
- Added a deterministic build script that publishes Scylla, builds the sibling STRATA repository, and emits `ScyllaSetup.msi`.
- Added first-launch import of installer choices into `%LOCALAPPDATA%\ScyllaGPT\strata.json` without overwriting existing settings.
- Added native discovery of bundled `strata\strata.exe bridge` before PATH/Python fallbacks.
- Kept STRATA API keys out of MSI properties and directed users to Scylla's Credential Manager-backed settings.
- Added the root `package` command, which discovers Python, uv, CMake, and .NET before running the complete STRATA and MSI pipeline.
- Fixed PowerShell's case-insensitive `StrataPayload`/`strataPayload` collision so the built STRATA directory is not replaced by its not-yet-created staging destination.
- Isolated packaging CMake output under `build/package`, selected the installed Visual Studio generator, and added explicit native-command exit checks.

## Payload dependency

The `cxl-strata` repository now contains a pinned PyInstaller specification and `scripts/build-windows-standalone.ps1`. Scylla invokes that script by default and still accepts `-StrataPayload` for a prebuilt release. The resulting directory has `strata.exe` at its root and does not require Python on the destination computer.

## Validation completed

- `Package.wxs` parses as XML.
- `build-installer.ps1` parses with the PowerShell AST parser.
- `git diff --check` reports no whitespace errors.
- Source inspection confirms the bundled STRATA discovery branch and first-launch importer are wired.

The MSI could not be compiled in this session: WiX and PyInstaller were not cached, and the managed environment could not resolve PyPI/NuGet DNS even after approved download attempts. CMake was also unavailable on PATH. The standalone build now fails fast when dependency installation or PyInstaller fails.

## Release validation still required

1. Run `package -Version <semver>` from the Scylla project root with network access; it builds and smoke-tests `strata.exe bridge` automatically.
2. For an offline release, build STRATA first and pass its output through `-StrataPayload <directory>`.
3. Resolve any WiX compiler diagnostics before release; the custom dialog navigation is the highest-value area to verify.
4. Test clean install, repair, major upgrade, and uninstall on clean Windows 10 and Windows 11 x64 VMs without Python.
5. Confirm existing `strata.json` survives upgrades and uninstall, then sign the MSI and all executable payloads.
