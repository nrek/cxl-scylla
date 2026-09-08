# Contributing

Contributions are welcome through issues and pull requests.

## Development setup

Build from a Visual Studio 2022 Developer PowerShell at the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target scyllagpt scyllagpt-tests
.\build\Release\scyllagpt-tests.exe
```

Alternatively configure `scyllagpt` alone (`cmake -S scyllagpt -B scyllagpt/build …`). The product executable is `scylla-workbench.exe`.

## Pull requests

- Keep security-boundary and grant-path changes small and explain the intended invariant.
- Add or update a local test for protocol, knowledge access, keyring, or UI-domain behavior when you change those paths.
- Never include credentials, account data, absolute developer-machine paths, or generated build output.
- Preserve fail-closed behavior for provider lockdown and secret handling.
- Do not revive archived Cage / launcher / Hyper-V code as a product path (`archive/`).

By contributing, you agree that your contribution is licensed under the repository's MIT License.
