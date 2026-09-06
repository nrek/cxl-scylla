# Architecture

Scylla is a Windows-only repository with two independently runnable applications.

## Scylla Cage

The cage uses native Windows security primitives:

```text
CLI / WPF UI
    │
    ├─ application discovery
    ├─ path and reparse-point validation
    ├─ AppContainer profile and SID
    ├─ temporary filesystem ACL grants
    ├─ restricted process launch
    ├─ Job Object lifecycle
    └─ cleanup and ACL revocation
```

The C++ CLI is the authority. The WPF application starts CLI commands and consumes newline-delimited JSON events. Human-readable CLI output is for interactive use; integrations should pass `--json`.

### Source map

| Path | Responsibility |
|---|---|
| `src/scylla/appcontainer.cpp` | AppContainer profile and process launch |
| `src/scylla/acl.cpp` | Temporary grant and revocation logic |
| `src/scylla/path.cpp` | Workspace validation and path safety |
| `src/scylla/process_scan.cpp` | Related-process discovery |
| `src/scylla/session.cpp` | Strict-session lifecycle |
| `src/scylla/discover.cpp` | Installed-application discovery |
| `src/scylla/identity.cpp` | Dedicated local-user execution mode |
| `src/probe/` | Sandbox capability probe |
| `src/test_parent/` | Process-tree fixture |
| `src/ui/` | WPF management shell |

## Scylla Workbench

Workbench is a separate native Win32 process:

```text
Win32 shell
    ├─ project tree and Scintilla editor
    ├─ local settings and conversation metadata
    ├─ Codex app-server child process (stdio JSON-RPC)
    └─ Claude Code child process (print mode)
```

Workbench does not link against or redistribute provider runtimes. Provider processes are discovered locally and started as child processes. See [`../scyllagpt/README.md`](../scyllagpt/README.md) for its module map.

## Build graph

The root CMake project builds the cage CLI, probes, fixtures, and Workbench. Scintilla and Lexilla are compiled as static libraries for Workbench. The WPF shell is a separate .NET project and can also be built through the optional `scylla-ui` CMake target.
