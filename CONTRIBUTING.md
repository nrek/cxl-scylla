# Contributing

Contributions are welcome through issues and pull requests.

## Development setup

Build from a Visual Studio 2022 Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run the native test targets:

```powershell
.\build\Release\scylla.exe selftest-path
.\build\Release\scyllagpt-tests.exe
```

The optional WPF shell requires the .NET 9 SDK:

```powershell
dotnet build .\src\ui\Scylla.UI.csproj -c Release
```

## Pull requests

- Keep security-boundary changes small and explain the intended invariant.
- Add or update a local test for path, token, ACL, process, or protocol behavior.
- Never include credentials, account data, absolute developer-machine paths, or generated build output.
- Preserve fail-closed behavior: setup failures must not fall back to unrestricted launch.
- Keep human-readable output separate from the stable `--json` contract.

By contributing, you agree that your contribution is licensed under the repository's MIT License.
