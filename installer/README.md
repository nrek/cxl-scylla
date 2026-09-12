# Scylla MSI

This WiX project creates a self-contained, per-user MSI containing Scylla and a standalone STRATA distribution. It installs beneath `%LOCALAPPDATA%\CXL\Scylla`, creates a Start menu shortcut, and passes safe base STRATA settings to Scylla's first launch. Existing `%LOCALAPPDATA%\ScyllaGPT\strata.json` settings are never overwritten.

## STRATA payload contract

By default the build locates the sibling `cxl-strata` repository and runs its pinned PyInstaller build. You may instead supply `-StrataPayload` with a prebuilt release directory whose root contains `strata.exe`. Python is not required on the destination computer.

Do not put an API key in the payload or MSI properties. Team credentials must be added after installation in **Settings → Strata** so Scylla can store them in Windows Credential Manager.

## Build

From Developer PowerShell for Visual Studio:

```powershell
.\package -Version 1.0.0
```

`package` performs tool discovery, builds and smoke-tests standalone STRATA, builds Scylla, and produces the MSI. Pass `-StrataPayload C:\releases\strata-win-x64` to reuse a prebuilt STRATA payload, or `-SkipApplicationBuild` to reuse staged Scylla output.
Use `package -PreflightOnly` to verify tool and repository discovery without building anything.

The result is `installer\artifacts\ScyllaSetup.msi`. The script builds the native core, publishes the self-contained WinUI application, validates the STRATA payload, and builds the MSI.

For a packaging-only rebuild, retain `installer\payload\scylla` and use `-SkipApplicationBuild`.

## Base configuration

Interactive setup includes a STRATA configuration page for enablement, local/team mode, and the team endpoint. Local STRATA is the default. Deployment tools may also set these public MSI properties:

```powershell
msiexec /i ScyllaSetup.msi STRATAENABLED=1 STRATAMODE=team STRATAENDPOINT=https://strata.example.com
```

`STRATAMODE` is `solo` or `team`; team endpoints must use HTTPS. Invalid team endpoints fall back to local-only configuration on first launch. The pending installer values are consumed once and do not replace an existing user configuration.

## Release checks

1. Build and smoke-test `strata.exe bridge` before packaging.
2. Install on a clean Windows 10 1809+ x64 VM with no Python installation.
3. Launch Scylla, open **Settings → Strata**, and verify local search.
4. Repair, upgrade, and uninstall; confirm user data under `%LOCALAPPDATA%\ScyllaGPT` remains intact.
5. Sign the MSI and bundled executables in the release pipeline.
