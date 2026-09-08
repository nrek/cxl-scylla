# Security model

## Goals

Scylla Cage is designed to:

- launch compatible desktop applications with an AppContainer token;
- expose only explicitly granted filesystem locations;
- fail closed when profile, token, ACL, or process setup fails;
- keep launched process trees tied to a Job Object;
- revoke temporary grants after the session;
- make degraded or incomplete cleanup visible to the caller.

## Boundaries

AppContainer is the primary OS boundary. Filesystem access also depends on Windows ACLs applied to the AppContainer SID. Optional network capability is separate from filesystem access.

Strict mode checks for related processes before launch because an already-running unrestricted instance is outside the cage. The session can report a degraded state if a related unrestricted process appears while the contained process is running.

## Non-goals

Scylla does not claim to:

- make arbitrary malware safe;
- contain kernel exploits or vulnerable privileged services;
- isolate resources granted by the operator;
- prevent disclosure through an explicitly enabled network;
- retrofit AppContainer compatibility onto every Win32 application;
- replace code review, endpoint protection, backups, or least-privilege account design.

## Safe evaluation

1. Build from source and inspect the command you intend to run.
2. Use a disposable local account or virtual machine for initial testing.
3. Grant a temporary directory containing no sensitive data.
4. Start with network disabled.
5. Run the included probe and verify allowed and denied paths.
6. Confirm cleanup and ACL revocation after stopping the session.
7. Do not grant a drive root, user profile, credential store, browser profile, or source tree containing secrets.

## Reporting vulnerabilities

Do not publish exploit details in a public issue. Use the repository host's private security-advisory channel when available and include:

- affected commit or release;
- Windows version and application type;
- reproduction steps;
- expected and observed access;
- relevant token, ACL, or lifecycle diagnostics with secrets removed.
