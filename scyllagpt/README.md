# Scylla Workbench

Scylla Workbench is a native Windows coding workspace for Codex and Claude Code. It is written in C++20 with Win32, Scintilla, and Lexilla—there is no browser shell or WebView.

> [!IMPORTANT]
> This is an independent, experimental client. It is not an official OpenAI or Anthropic product.

## Features

- project tree and tabbed source editor;
- syntax highlighting through Scintilla and Lexilla;
- agent transcript, composer, context files, and selections;
- OpenAI model discovery through the Codex app-server;
- Claude model selection and chat through an authenticated Claude Code CLI;
- local project and conversation metadata;
- explicit project-level browse/edit grants;
- dark native Windows interface with keyboard navigation.

## How providers work

### OpenAI / Codex

Workbench launches a locally installed Codex app-server and communicates over newline-delimited JSON-RPC on standard input/output. Authentication uses the app-server's browser-based ChatGPT login flow.

Codex is not bundled. Workbench searches supported local installation locations and also allows selecting a `codex.exe` in Settings.

### Claude Code

Workbench discovers a locally installed `claude` executable, checks its authentication status, and runs chats in non-interactive print mode. Claude Code is not bundled.

## Access model

Workbench uses an isolated Codex home:

```text
%LOCALAPPDATA%\ScyllaGPT\codex-home
```

The legacy directory name is retained for compatibility. Workbench rewrites its managed `config.toml` when the application starts or the active project grant changes. It does not replace the user's normal `%USERPROFILE%\.codex` configuration.

With no active project, Codex starts read-only with shell access disabled. Selecting a project sets the thread working directory and enables workspace-scoped editing. See [docs/lockdown.md](docs/lockdown.md) for the exact controls and limitations.

Workbench itself is not an AppContainer. The separate Scylla Cage application at the repository root implements OS-level AppContainer launch.

## Requirements

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with **Desktop development with C++**
- Windows SDK
- CMake 3.20 or newer
- A local Codex installation for OpenAI features
- A local Claude Code installation for Claude features

## Install from source

Workbench does not yet ship an installer or auto-updater. Obtain the source and open Developer PowerShell for VS 2022 as described in the [root installation instructions](../README.md#install-from-source). The .NET SDK is not required for Workbench.

Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target scyllagpt scyllagpt-tests
.\build\Release\scyllagpt-tests.exe
```

Launch:

```powershell
.\build\Release\scylla-workbench.exe
```

The CMake target retains the internal name `scyllagpt`; the public executable is `scylla-workbench.exe`.

## First run

Install at least one provider before connecting it. Workbench does not install provider binaries or purchase account access for you.

| Provider | Installation and discovery | Authentication |
|---|---|---|
| OpenAI / Codex | Follow the [official Codex CLI installation guide](https://developers.openai.com/codex/cli). Workbench searches `%LOCALAPPDATA%\OpenAI\Codex\bin` subdirectories, then its direct `codex.exe`, then `PATH`. Use **Settings → Advanced → Codex executable…** to select an executable if needed. | In **Settings → AI Providers**, sign in with ChatGPT and complete the browser flow. Workbench uses its isolated Codex home, so do not assume another CLI session's login is available here. OpenAI Platform API-key setup is not exposed in the UI. |
| Claude Code | Follow the [official Claude Code setup guide](https://code.claude.com/docs/en/setup). Ensure `claude.exe` or `claude.cmd` is on `PATH`, then restart Workbench after changing `PATH`. | Run `claude auth login` in a terminal, or launch Claude Code login from **Settings → AI Providers**. Complete the console/browser flow; `claude auth status` should report a logged-in account. |

The repository does not document a verified provider-version compatibility matrix. Record `codex --version` and `claude --version` when testing or reporting an issue; if Codex is not on `PATH`, invoke the selected executable's full path with `--version`. After a provider update, verify sign-in, model selection, and sending a message; also check response cancellation for Codex. See [protocol compatibility](docs/feasibility.md).

1. Launch Workbench and open **File → Settings → AI Providers**.
2. Connect one or both providers using the steps above. Confirm that the provider shows as connected and its models appear in the header agent menu.
3. Open a disposable project folder and select it in the header project selector. This enables the Codex project browse/edit grant; check the `browse/edit` status before requesting changes.
4. Choose an agent/model from the header and start a new chat.
5. Open a file or attach a selection, then send a small request. Confirm that a response appears. For Codex, also check that **Stop** can interrupt an active response.

## Configuration and local data

Use Settings and the application menus for routine changes; settings persist immediately. These are the main controls and initial defaults:

| Setting | Default / behavior |
|---|---|
| Codex executable | Automatically discovered; select an explicit `codex.exe` in Settings if discovery fails. |
| Provider and model | Default provider is OpenAI; the chosen model is saved after selection. Connect a provider before choosing its models. |
| Project folder | No project on a fresh install; the last project folder is saved. The selected folder controls the Codex grant described in [Access model](#access-model). |
| Enter sends message | Enabled. |
| Word wrap | Disabled. |
| Show whitespace | Disabled. |

All paths below are relative to `%LOCALAPPDATA%\ScyllaGPT`:

| Path | Purpose |
|---|---|
| `settings.json` | Runtime selection, project folder, provider/model selection, editor preferences, layout, and message drafts. |
| `workspace.json` | Local project and conversation store. |
| `codex-home\` | Isolated Codex configuration, authentication, and runtime-managed session data. |
| `codex-home\config.toml` | Managed policy, overwritten on launch and project-grant changes. Manual edits do not persist across those events. |
| `workspace\` | Internal working directory used when no project is selected. |
| `runtime-stderr.log` | Codex runtime diagnostic output. |
| `recovery\`, `attachments\` | Local recovery and attachment storage. |

For a PowerShell diagnostic check:

```powershell
Get-Content "$env:LOCALAPPDATA\ScyllaGPT\codex-home\config.toml"
Get-Content "$env:LOCALAPPDATA\ScyllaGPT\runtime-stderr.log" -Tail 50
```

Use **Help → Diagnostics** to inspect runtime details. Logs may not exist before the runtime starts. When reporting an issue, redact account details, local project paths, and prompt content from diagnostics; do not share authentication files.

## Troubleshooting

| Symptom | Check |
|---|---|
| Codex executable not found | Use **Settings → Advanced → Codex executable…** to select the actual `codex.exe`; a shell wrapper alone is not sufficient for explicit selection. |
| Claude Code not found | Run `Get-Command claude` in PowerShell, ensure its directory is on `PATH`, and restart Workbench. |
| Provider installed but disconnected | Complete that provider's login flow. For Claude, check `claude auth status`; for Codex, sign in through Workbench's isolated home. |
| No provider models in the agent menu | Confirm authentication in AI Providers, then inspect **Help → Diagnostics** and the runtime log if connection fails. |
| Codex cannot edit project files | Select the project in the header and check the `browse/edit` status. Start a new chat after switching projects: an existing Codex thread retains its original project working directory. |
| Manual `config.toml` changes disappear | This file is managed. Use supported UI settings; consult [lockdown controls](docs/lockdown.md) for fixed policy. |
| Runtime fails after a provider update | Record the executable path and version, inspect diagnostics, and check [protocol compatibility](docs/feasibility.md). |

## Architecture

```text
include/scyllagpt/  Public module headers
src/app/            Win32 entry point and resources
src/domain/         Sessions, providers, and model catalog
src/platform/       Windows paths and UTF conversion
src/protocol/       JSON framing and parsing
src/runtime/        Codex child-process transport
src/storage/        Settings and local conversation store
src/ui/             Native shell, Scintilla host, layout, and theme
src/workspace/      File loading and saving
schemas/            Generated Codex app-server protocol schemas
third_party/        Scintilla and Lexilla source
```

## Current limitations

- Codex app-server behavior and schemas can change between releases.
- Workbench does not mirror ChatGPT website conversation history.
- Claude chat uses print-mode invocations rather than a persistent Claude session.
- Provider binaries and subscriptions must be installed and obtained separately.
- The application is pre-release and does not yet ship an installer or auto-updater.

See [docs/capability.md](docs/capability.md) and [docs/feasibility.md](docs/feasibility.md) for implementation status.

## Privacy

Workbench stores settings and local conversation metadata under `%LOCALAPPDATA%\ScyllaGPT`. Prompts and selected context are sent to the provider you choose. Review each provider's privacy policy and terms before using sensitive source code.

## License

Workbench is part of Scylla and is available under the repository's [MIT License](../LICENSE). Scintilla and Lexilla retain their original license; see [third-party notices](../THIRD_PARTY_NOTICES.md).
