# Architecture

Scylla is a Windows-only repository with two independently runnable applications.

## Scylla Cage

The cage is a CLI-only AppContainer launcher using native Windows security primitives:

```text
CLI
    → path and reparse-point validation
    → AppContainer profile and SID
    → temporary filesystem ACL grants
    → restricted process launch
    → Job Object lifecycle
    → cleanup and ACL revocation
```

The C++ CLI is the authority. Human-readable CLI output is for interactive use; integrations should pass `--json`. Workbench is the primary interactive UI in this repository and does not wrap the cage CLI.

### Source map

| Path | Responsibility |
|---|---|
| `src/scylla/appcontainer.cpp` | AppContainer profile and process launch |
| `src/scylla/acl.cpp` | Temporary grant and revocation logic |
| `src/scylla/path.cpp` | Workspace validation and path safety |
| `src/scylla/process_scan.cpp` | Related-process discovery |
| `src/scylla/session.cpp` | Strict-session lifecycle |
| `src/scylla/sandbox_probe.cpp` | Host capability and packaged self-tests |
| `src/probe/` | Sandbox capability probe |
| `src/test_parent/` | Process-tree fixture |

Historical WPF launcher sources live under `archive/scylla-ui-launcher/` and are not part of the build.

## Scylla Workbench

Workbench is a separate native Win32 process:

```text
Win32 shell
    → project tree and Scintilla editor
    → local settings and conversation metadata
    → Codex app-server child process (stdio JSON-RPC)
    → Claude Code child process (print mode)
```

Workbench does not link against or redistribute provider runtimes. Provider processes are discovered locally and started as child processes. See [`../scyllagpt/README.md`](../scyllagpt/README.md) for its module map.

## Build graph

The root CMake project builds the cage CLI, probes, fixtures, and Workbench. Scintilla and Lexilla are compiled as static libraries for Workbench.
