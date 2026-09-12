---
project: cxl-scylla
status: in_progress
---

# Fluent Security integration

Implemented in source:

- App-scoped encrypted Keyring create, unlock and lock using the existing Argon2/AES vault implementation. Unlock state is shared across Fluent agent sessions; release locks the vault.
- Human-only metadata snapshot and write-only secret input ABI. No read/export-secret endpoint. Managed UTF-8 input buffers are wiped after native calls; managed strings cannot be reliably zeroed.
- Expandable Fluent settings with named variables, descriptions, global/project scope, replace/delete and private-key/certificate file import. Deletion identifies the exact scope. Reference names cannot newly collide across scopes.
- Project-scoped saved connection forms: host/port/user literals or encrypted references, SSH private key/password/passphrase references, pinned host public key, database credentials/TLS CA, query authority and result limits.
- MySQL, PostgreSQL, SQL Server and MongoDB metadata. MongoDB remains MongoDB on serialization and legacy UI edits; it is not mapped to MySQL.
- Native helper target scylla-broker.exe; Fluent sessions register the existing query MCP with a per-session pipe. Project binding follows conversation cwd. Broker preparation shares the vault lock; execution runs outside it. Credential references are checked against project-visible items and duplicate names are refused.
- Plan/Ask block SQL mutations regardless of saved write authority. An approval-required operation is refused; this change does not auto-approve it.

Not implemented / remaining acceptance work:

- The existing trusted executor runs only MySQL remote execution over SSH private-key authentication. PostgreSQL, MongoDB, direct DB access, SSH tunnels and SSH password execution still need executors and local fixture tests.
- SSH-only aliases are now supported through scylla_ssh with the saved WSL terminal profile. Host configuration/log commands can be supplied as remote shell scripts. Identity references are bound in the saved connection; composing a separate @mysshuser at request time is not supported. Other terminal types and interactive sessions remain unsupported.
- Fluent per-operation approval UI, test-connection action, cancellation/lock revocation for already-prepared operations and human result inspection remain to implement. Lock blocks new credential authorization; an already-prepared operation may complete.
- Settings status does not yet report missing broker-helper deployment. The helper must be copied beside scylla.exe as documented in scylla-fluent/README.md.
- Review immutable managed-string lifetime for secret entry; no claim of zero plaintext lifetime is made.

Verification: Roslyn syntax parsing passed for SecuritySettingsPane.cs, NativeCore.cs and EditorPane.cs; git diff --check passed. Added scoped Keyring deletion and MongoDB metadata regression cases, not executed. No app/native rebuild or remote operation performed. Runtime and compiled validation remain pending the user's manual build.

## SSH Connections follow-up

- Separate SSH-only type and settings section, with no database configuration. Saved alias, WSL terminal profile, keyring user/private key/passphrase references, pinned host public key, command permission and output limits.
- scylla_ssh accepts connection_alias and command. Explicit type checking prevents use of the database tool against an SSH connection. The native broker supplies trusted credentials over stdin to its embedded WSL Python adapter; no credentials appear in the MCP request or process command line. SSH config, key, known_hosts and askpass script are anonymous RAM-backed files owned by the adapter, closed on exit. SSH gets a minimal environment. Private keys are not written to disk.
- Execute mode, connection Auto permission and Terminal agent policy Allow are required. Defaults block execution. Existing Ask configurations refuse execution until an approval UI exists; the agent cannot self-approve.
- WSL requires Python 3 with memfd support and /usr/bin/ssh. Only enabled WSL terminal profiles with no arguments or a distribution selector are accepted. No fallback to a different terminal/profile.
- Exit code, stdout and stderr return as bounded result rows. Known credential literals/key lines are redacted. Capped/timed-out output is withheld to avoid leaking partial credential strings. This is not a claim to detect arbitrary encoded secrets or unrelated sensitive content in remote logs.
- Fixed the shared process runner to drain stdout/stderr concurrently and monitor timeout during I/O instead of waiting until blocking reads had finished.
- Four offline Python adapter tests passed. Roslyn C# syntax and git diff --check passed. Native broker/transport tests extended, not run because no rebuild was requested. Local WSL launch failed with Wsl/Service/0x8007072c even outside the sandbox; live integration remains unverified. No SSH connection made.

## Connection layout and per-terminal policy follow-up

- Literal hostname/port/SSH username and Keyring-reference fields are paired horizontally; they stack below 560 px of form width. Credential-only fields stay reference-only.
- Terminal settings use a leftmost unlabeled enable switch with an accessible name, individual Block/Ask/Allow dropdowns, and a discovery refresh action. Policy overrides persist per profile; missing entries inherit the existing default policy.
- Discovery now includes known GitHub/Claude/Codex/Cursor CLI launchers, Nushell and PATH Bash, checking PATH, common per-user binary/npm/scoop locations and known Program Files paths. This is discovery of known commands, not enumeration of every executable as a terminal. CMD/BAT launchers use cmd.exe. WSL distribution error output is rejected, and a default WSL entry remains when the executable is installed but distro discovery fails.
- SSH selection excludes disabled/blocked profiles and still restricts selection to the currently implemented WSL transport. Ask entries can be saved but require the still-missing approval interaction to execute. The broker combines connection authority with selected terminal policy before credential preparation, and checks again at execution.
- General agent invocation of every discovered CLI, role-based authorization and non-WSL SSH execution are not implemented by this change. CLI applications are not automatically SSH transports.
- Validation: C# syntax and git diff --check passed. Added native per-profile policy regression cases; not built/run. No rebuild or remote operations performed.
