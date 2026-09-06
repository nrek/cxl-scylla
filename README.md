# Scylla

![Scylla Workbench screenshot](docs/images/scylla-workbench-screen.png)

Scylla is experimental Windows-native tooling for running desktop applications with explicit filesystem access. The repository contains two related applications:

- **Scylla Cage** — a C++20 CLI and WPF shell that launch Win32 applications inside a Windows AppContainer.
- **[Scylla Workbench](scyllagpt/README.md)** — a native C++ desktop workspace for Codex and Claude Code.

> [!WARNING]
> This is pre-release security software. Review the threat model, test with non-sensitive data, and verify the resulting token and ACL behavior before relying on it. A sandbox reduces access; it does not make untrusted software safe.

## Scylla Cage

`scylla.exe launch` creates an AppContainer profile, grants the selected application read/execute access to its install tree, grants only the requested workspace access, and launches the process with the restricted token. If sandbox setup fails, Scylla fails closed instead of launching the target normally.

Strict sessions add:

- multiple read-only and read/write grants;
- optional network removal with `--no-internet`;
- related-process preflight checks;
- a Job Object for process-tree lifecycle control;
- machine-readable lifecycle events with `--json`;
- cleanup and grant revocation when the session ends.

The WPF shell (`Scylla.UI.exe`) calls the CLI through its JSON interface. The CLI remains usable independently.

## Scylla Workbench

Scylla Workbench is a separate native Win32 application. It embeds the Codex app-server over stdio and can invoke an authenticated Claude Code CLI in print mode. It provides a project tree, Scintilla editor, agent pane, local conversation list, explicit project grants, and isolated Codex configuration.

Workbench is **not** launched inside the AppContainer cage. Its policy and Codex sandbox controls are documented in [scyllagpt/docs/lockdown.md](scyllagpt/docs/lockdown.md).

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with **Desktop development with C++**
- Windows 10/11 SDK
- CMake 3.20 or newer
- .NET 9 SDK (only for the WPF cage UI)
- Optional: ChatGPT desktop/Codex CLI and Claude Code for Workbench providers

## Install from source

The instructions below build and run the applications from a local source checkout. Workbench does not yet ship an installer or auto-updater.

1. Clone this repository using its **Code → HTTPS** URL, or download and extract its ZIP archive. Git is needed only for cloning.
2. Open **Developer PowerShell for VS 2022** from the Start menu.
3. Change into the extracted or cloned repository directory containing `CMakeLists.txt` and this README. Run all commands below from that directory.

Build the native CLI, test tools, and Workbench:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Optional, for the WPF Cage UI only (requires the .NET 9 SDK):

```powershell
dotnet build .\src\ui\Scylla.UI.csproj -c Release
```

Build the native tools first: the WPF build copies `scylla.exe`, `scylla-probe.exe`, and `scylla-test-parent.exe` into its output directory. Keep these files alongside `Scylla.UI.exe` when copying the UI elsewhere. The WPF build is framework-dependent; another machine also needs the .NET 9 Desktop Runtime. Native builds use the MSVC runtime; another machine may need the Visual C++ v14 Redistributable (x64).

Primary outputs:

```text
build/Release/scylla.exe
build/Release/scylla-probe.exe
build/Release/scylla-test-parent.exe
build/Release/scylla-workbench.exe
src/ui/bin/Release/net9.0-windows/Scylla.UI.exe
```

## Quick start

Use disposable directories while evaluating the sandbox. Start with the bundled process-tree fixture so no third-party application is required:

```powershell
New-Item -ItemType Directory -Force .\sandbox-workspace | Out-Null

.\build\Release\scylla.exe capabilities
.\build\Release\scylla.exe selftest-path
.\build\Release\scylla.exe discover --json

.\build\Release\scylla.exe strict launch `
  --profile test-parent `
  --allow-rw "$PWD\sandbox-workspace" `
  --args "--seconds 60" `
  --no-internet `
  --json
```

The path check should print `SCYLLA: PATH TESTS OK`. A successful strict launch emits a JSON event with `"event":"started"` and `"state":"RUNNING"`. The fixture runs for about 60 seconds; session cleanup emits a `closed` event. Check its `ok` field and cleanup details rather than assuming that process exit means cleanup succeeded.

While the foreground session is active, open a second terminal in the repository root to inspect it and request shutdown:

```powershell
.\build\Release\scylla.exe strict status --json
.\build\Release\scylla.exe strict stop --json
```

To launch your own application, replace the example executable path with an existing Win32 executable:

```powershell
.\build\Release\scylla.exe strict launch `
  --app "C:\Path\To\Application.exe" `
  --allow-rw "$PWD\sandbox-workspace" `
  --no-internet `
  --json
```

For a basic launch with one writable workspace and internet access enabled:

```powershell
.\build\Release\scylla.exe launch `
  --app "C:\Path\To\Application.exe" `
  --workspace "$PWD\sandbox-workspace"
```

Launch the optional WPF Cage UI:

```powershell
.\src\ui\bin\Release\net9.0-windows\Scylla.UI.exe
```

Launch Workbench:

```powershell
.\build\Release\scylla-workbench.exe
```

Follow the [Workbench provider setup and first-run instructions](scyllagpt/README.md#first-run) before sending a message.

## Cage configuration

CLI launches take their configuration from command-line arguments:

| Option | Behavior / default |
|---|---|
| `launch --workspace <dir>` | Grants read/write access to one workspace. Required for basic launch. |
| `strict launch --allow-rw <dir>` | Grants read/write access. Repeat for multiple directories; at least one is required. The first is the process working directory. |
| `strict launch --allow-ro <dir>` | Adds a read-only grant. Repeat as needed; none by default. |
| `strict launch --no-internet` | Omits the internet capability. Both basic and strict launches enable internet access by default; basic launch has no network-disable flag. |
| `strict launch --args <str>` | Passes an argument string to the target application. |
| `strict launch --seconds N` | Limits session duration; otherwise Scylla waits for exit or a stop request. |
| `strict launch --kill-related` | Terminates related applications already running outside the session. Without this flag, the CLI refuses to start when related processes are present. Save their work and close them first. |
| `--json` | Emits structured output on commands that support it, including strict lifecycle commands. |

The WPF UI saves application, filesystem, network, and strict-session profiles under `%LOCALAPPDATA%\Scylla\profiles`. Configure these through the UI and review the selected profile before launching. Workbench has [separate settings and storage](scyllagpt/README.md#configuration-and-local-data).

## Troubleshooting

| Symptom | Check |
|---|---|
| `cmake` or the Visual Studio generator is unavailable | Install the C++ workload, Windows SDK, and CMake; reopen Developer PowerShell for VS 2022. |
| `dotnet` is unavailable | Install the .NET 9 SDK, or skip the optional WPF build. |
| WPF reports `scylla.exe` missing | Build the native Release targets, then rebuild the WPF project so the native tools are copied alongside the UI. |
| Strict launch reports related processes already running | Save work and close those processes, then retry. |
| Launch refuses a path or ACL grant | Read the reported reason; use an existing executable and a disposable local workspace whose permissions you can modify. Avoid drive roots, user-profile roots, and redirected workspace paths. |
| Workbench cannot find or connect a provider | Follow [Workbench troubleshooting](scyllagpt/README.md#troubleshooting). |

## Repository layout

```text
src/scylla/       AppContainer launch, identity, ACL, discovery, and sessions
src/probe/        Sandbox capability probe
src/test_parent/  Process-tree test fixture
src/ui/           WPF management shell
scyllagpt/        Scylla Workbench native Win32 source
docs/             Architecture, threat-model, and implementation notes
```

## Security notes

- Grant the smallest possible directories; do not grant an entire user profile.
- Reparse points and unsafe workspace roots are rejected.
- AppContainer filesystem access is implemented with explicit ACL grants.
- Basic and strict launches enable internet access by default. Use `strict launch --no-internet` to omit the internet capability.
- `--json` is the supported automation contract; do not parse human-readable output.
- Report security-sensitive findings privately before publishing details.

## Documentation

See [docs/README.md](docs/README.md) for the technical documentation index and [scyllagpt/docs/capability.md](scyllagpt/docs/capability.md) for Workbench capability status.

## Third-party software

Scylla Workbench includes Scintilla and Lexilla source code under their permissive license. External Codex, ChatGPT, OpenAI, Anthropic, and Claude products are not distributed by this repository and remain subject to their respective terms.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Scylla is available under the [MIT License](LICENSE). Bundled third-party components retain their original licenses.
