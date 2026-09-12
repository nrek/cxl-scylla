# Scylla

Scylla is a Windows coding workspace for working with AI agents while keeping projects, knowledge, terminals, external connections, and secrets under explicit user control.

The primary application is `scylla.exe`, a C# and WinUI 3 desktop shell backed by the native `scylla-core.dll` library. Scylla is under active development and is not yet a finished IDE or a hardened security boundary.

> [!WARNING]
> Scylla runs provider tools and approved commands as the signed-in Windows user. Review project grants, Knowledge access, connection policies, and terminal settings before using sensitive data. Application policy reduces accidental access; it does not make untrusted code safe.

## What Scylla includes

| Area | Current behavior |
|---|---|
| Projects and files | Multiple project folders, file and folder management, tabbed editing, syntax highlighting, search, Markdown preview, images, and Mermaid diagrams. |
| Agent chat | Local chat history, file and selection context, Execute/Plan/Ask modes, provider and model selection, streaming responses, progress, cancellation, and changed-file activity. |
| Providers | ChatGPT/Codex and Claude connections. Available models depend on the connected provider and account. |
| Terminal | Integrated Windows and WSL terminal profiles with multiple sessions and per-profile agent execution policy. |
| Knowledge | Additional local folders with project scope and agent availability. Knowledge files can be browsed and referenced from chat. |
| MCP | Saved HTTP and stdio connections, OAuth authentication, explicit scopes, project assignment, and agent aliases. Support varies by server and authentication flow. |
| Security | An encrypted application Keyring, project-scoped connection definitions, protected values, and command/query policy checks. |
| STRATA | A local workspace knowledge index for plans, blueprints, handoffs, and related project material, with optional team synchronization. |

Scylla does not bundle provider accounts. Install or connect the provider software you intend to use and follow that provider's terms and authentication flow.

## Requirements

For an installed release:

- Windows 10 version 1809 or newer, or Windows 11
- x64 processor
- A supported AI provider account for agent chat
- WebView2 Runtime for embedded web content; current Windows installations normally include it

For a source build:

- Visual Studio 2022 or newer with **Desktop development with C++** and .NET desktop tooling
- Windows 10/11 SDK
- CMake 3.20 or newer
- .NET 8 SDK

## Installation

Scylla does not currently publish a general-purpose installer from this repository. Until release packages are available, build it from source.

The planned installer will contain the complete desktop application, its native runtime and broker, and the local STRATA service. Starting Scylla will start the bundled local STRATA service for the current user; users will not need to install Python or STRATA separately. Team synchronization will remain optional and will require a remote endpoint and credential configured in Scylla.

### Build from source

Open **Developer PowerShell for Visual Studio** and run these commands from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target scylla-core-shared
dotnet build .\scylla-fluent\Scylla.csproj `
  --configuration Release `
  -p:Platform=x64
```

Building `scylla-core-shared` also builds the trusted broker. The WinUI project copies `scylla-core.dll` and `scylla-broker.exe` into its output directory and fails if either file is missing.

Run the source build:

```powershell
.\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0\scylla.exe
```

If the native output is outside `build\Release`, pass its directory when building the UI:

```powershell
dotnet build .\scylla-fluent\Scylla.csproj `
  --configuration Release `
  -p:Platform=x64 `
  -p:ScyllaNativeOutputDir="C:\path\to\native\Release"
```

## First launch

1. Open one or more project folders. These folders define the workspace available to the selected agent session.
2. Open **Settings → Providers** and connect ChatGPT/Codex, Claude, or both.
3. Select a provider, model, and mode in the Agent composer, then begin a chat.
4. Add supporting documentation through **KNOWLEDGE → Manage** and explicitly choose whether it is available to the agent.
5. Review **Settings → Terminal**, **Settings → MCP**, and **Settings → Security** before enabling command or connection access.

Provider login and project access are independent. Connecting an account does not grant an agent access to every folder, terminal, MCP server, Keyring entry, or saved connection.

## STRATA

STRATA is Scylla's project knowledge layer. It indexes human-readable project material such as plans, blueprints, handoffs, and documentation so people and agents can find the same working context.

The installer distribution is intended to run STRATA locally when Scylla launches. Local indexing works without a team service. To share or synchronize indexed knowledge, enable Team mode under **Settings → Strata**, enter the HTTPS endpoint and remote API key, then verify the connection. Remote credentials are stored through Windows Credential Manager rather than in the plain settings file.

Source builds do not yet provision or launch STRATA automatically. Their embedded STRATA view expects a compatible local service at `http://127.0.0.1:8765`. This is a development limitation that the installer packaging will remove.

## Configuration and local data

Scylla keeps per-user application data under:

```text
%LOCALAPPDATA%\ScyllaGPT\
```

The existing directory name is retained for compatibility. Configure Scylla through the application where possible instead of editing these files directly.

| Data | Storage |
|---|---|
| Application preferences and execution policy | Local JSON settings |
| Projects, conversations, and drafts | Local workspace data |
| Knowledge sources and project scope | Local Knowledge configuration |
| Terminal profiles and project environments | Local profile configuration |
| MCP and project connection metadata | Local connection configuration |
| OAuth tokens, STRATA remote credentials, and other protected credentials | Windows Credential Manager or the encrypted Scylla Keyring, depending on the feature |
| Agent runtime state | An application-managed, isolated runtime directory |

Do not commit local application data, credentials, generated build output, or diagnostic logs. Before sharing diagnostics, remove account identifiers, project contents, hostnames, and local filesystem paths.

## Access and security model

- Project folders are explicit workspace grants. Open only folders required for the task.
- Knowledge sources have their own enablement and agent-availability controls. Treat parent-folder grants carefully when a source contains sensitive child folders.
- Execute mode may permit edits and approved tools. Plan and Ask are designed for read-only work and deny write escalation.
- MCP authentication, MCP availability to an agent, and the permissions of the remote service are separate controls.
- The Scylla Keyring stores protected values. Agents receive brokered operations governed by project and execution policy rather than direct access to the vault.
- Database and SSH operations require saved project-scoped connections and are restricted by their configured route and policy. Several connection types and approval flows remain under development.

See [SECURITY.md](SECURITY.md) for vulnerability reporting and [the security implementation status](docs/plans/SCYLLA_security_fluent_status.md) for current limits.

## Development and validation

Build the native regression tests alongside the shared core:

```powershell
cmake --build build --config Release --target scyllagpt-tests scylla-mcp-tests
.\build\Release\scyllagpt-tests.exe
.\build\Release\scylla-mcp-tests.exe
```

Run the Fluent Markdown and preview checks:

```powershell
dotnet run --project .\tests\fluent-preview-tests\PreviewTests.csproj
```

Some UI and provider flows require manual verification because they depend on a real Windows session, installed provider tools, and user authentication. Tests must not use production credentials or production infrastructure.

## Current limitations

Scylla is pre-release software. Some provider capabilities, interactive approvals, connection executors, terminal behaviors, and STRATA packaging are still being completed. The editor is not a complete replacement for Visual Studio or VS Code: debugger, extension marketplace, language-server diagnostics, and several advanced terminal features are outside the current scope.

Documentation should distinguish implemented behavior from planned installer behavior. In particular, automatic STRATA provisioning and launch belong to the upcoming installer and are not performed by a source checkout today.

## Repository layout

```text
scylla-fluent/          Primary WinUI 3 application
scyllagpt/              Native core, broker, platform integrations, and tests
tests/                  Cross-layer fixtures and Fluent preview tests
docs/                   Architecture, status, and implementation handoffs
archive/                Historical prototypes retained for reference
```

The root CMake project builds the native core and its tests. The `scylla-fluent` .NET project builds the primary desktop application and stages the required native binaries beside it.

## Documentation

- [Fluent application development notes](scylla-fluent/README.md)
- [Documentation index](docs/README.md)
- [MCP implementation status](docs/plans/SCYLLA_mcp_catalog_execution_status.md)
- [Security implementation status](docs/plans/SCYLLA_security_fluent_status.md)
- [Contributing](CONTRIBUTING.md)

## Third-party software

Scylla uses the Windows App SDK, WebView2, Scintilla, Lexilla, Argon2, and other components listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). External AI providers and their command-line tools are not distributed by this repository and remain subject to their own licenses and terms.

## License

Scylla is available under the [MIT License](LICENSE). Bundled third-party components retain their original licenses.
