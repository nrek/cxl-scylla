# Scylla native runtime

This directory contains the C++20 runtime used by the WinUI 3 `scylla.exe` application. It is not a separate desktop product.

The supported native deliverables are:

- `scylla-core.dll`, the C ABI consumed by `scylla-fluent`;
- `scylla-broker.exe`, the trusted helper for policy-governed database and SSH operations;
- native regression-test executables.

The retired Win32 Workbench and its `scylla-workbench.exe` binary are no longer built, packaged, installed, or supported.

## Build

From a Visual Studio 2022 Developer PowerShell at the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target scylla-core-shared scyllagpt-tests
dotnet build .\scylla-fluent\Scylla.csproj --configuration Release -p:Platform=x64
```

Building `scylla-core-shared` also builds `scylla-broker.exe`. The Fluent project stages both native files beside `scylla.exe` and fails if either is missing.

Run native tests with:

```powershell
.\build\Release\scyllagpt-tests.exe
```

Additional focused targets cover MCP, editor handles, terminal behavior, and payload formatting. The STRATA adapter has a Python regression suite. See the target declarations in `CMakeLists.txt`.

## Architecture

```text
include/scyllagpt/  Native runtime interfaces
src/core/           C ABI exported to the WinUI application
src/domain/         Sessions, providers, policy, Keyring, connections, and STRATA
src/platform/       Windows paths, process coordination, UTF, and file I/O
src/protocol/       JSON framing and parsing
src/runtime/        Agent child-process transport
src/storage/        Settings and local conversation storage
src/ui/             Native editor/terminal hosts and shared theme helpers
src/workspace/      File loading and saving
tests/              Native regression tests
third_party/        Scintilla, Lexilla, Argon2, and supporting source
```

Native editor and terminal host code remains because WinUI embeds those controls through `scylla-core.dll`; its presence does not represent a second application shell.

## Security and data

Runtime state is stored under `%LOCALAPPDATA%\ScyllaGPT`. Project folders remain explicit workspace grants. Provider processes run as the signed-in Windows user under application policy and job controls, not an OS AppContainer.

See [lockdown controls](docs/lockdown.md) and the [root README](../README.md) for user-facing requirements and limitations.
