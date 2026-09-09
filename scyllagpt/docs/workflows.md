# Composer workflows

The composer selector applies to the next submitted message. Execute is the default.

- **Execute** asks the agent to carry out requested project changes and verify them.
- **Plan** asks the agent to research and save a Markdown plan without implementing it.
  It prefers an existing `plans` directory in enabled, writable, agent-available
  Knowledge sources. Otherwise it uses `plans` under the first suitable Knowledge
  root, or `<project>/plans` when no suitable Knowledge source exists.
- **Ask** asks the agent to answer and inspect context without editing files or
  running commands that make changes.

These are per-message workflow instructions for both provider paths, not separate
security sandboxes. Project grants and provider approval controls still apply.

Plan drafts are saved under `plans/draft/<project-prefix>_<topic>.plan.md`.
Instructions require YAML `name`, `overview`, `status: draft`, `project`, and `todos`;
the agent follows applicable Knowledge naming rules. Lifecycle folders are
`draft`, `backlog`, `in_queue`, `in_progress`, and `done`, matching YAML `status`.
Plan mode leaves new plans in draft and returns a link to the saved file.
Writing and successful save reporting are performed by the agent; Scylla does not
pre-create an empty plan when a message is sent.

**Ctrl+Enter** submits. **Enter** and **Shift+Enter** insert newlines. Submission
is suppressed during IME composition and for held-key repeats. The old Enter-sends
preference is no longer used by the composer. Add Selection remains in the Agent
menu; it no longer occupies the composer footer.

## Rules, skills, and file mentions

Each submitted message includes selected rule/skill references from a permission-aware
metadata index of project and enabled, agent-available Knowledge sources. The agent is
instructed to read relevant AGENTS.md, CLAUDE.md, rules/*.md[c], and SKILL.md files
before working, respecting frontmatter scope and explicit user instructions. This
is a workflow context mechanism; source bodies are read by the agent, not silently
injected wholesale. If none are found, mode defaults apply.

Type @ followed by part of a filename or path to search project and Knowledge
subfolders alongside MCP aliases. Up/Down selects, Enter/Tab inserts, Escape closes;
Ctrl+Enter submits. File choices insert @"absolute path", preserving spaces and
avoiding basename ambiguity. References remain visible in the composer and message.

## `/scylla-query` (brokered SQL)

Prefix a message with `/scylla-query` to inject the brokered-SQL skill. The slash token is
stripped from the visible transcript.

Execution runs through a real agent-callable tool, not a text handoff:

```text
agent → scylla_query (MCP, stdio helper) → named pipe → Workbench
      → resolve alias → classify SQL → approve if required → authorize Keyring refs
      → ssh → mysql → bounded rows → agent
```

The helper is `scylla-workbench.exe --mcp-query-broker`, registered automatically as
`[mcp_servers.scylla-query]` in the isolated Codex home. It is **secret-free**: it receives only a
per-launch session token, derives the pipe name from it, and never holds a Keyring. Credentials are
resolved inside the Workbench process and authorized for exactly one operation.

The agent supplies only `connection_alias` and `sql`. The **saved connection** owns the credential
mapping — SSH key reference, database username and password references, pinned host key, query
policy, and result limits. The agent cannot pass a host, credential, or Keyring name, and the tool
schema exposes no field for one.

Routing detail is Keyring-held too. Every SSH and database host, port, and username accepts either a
literal value or a `*Ref` naming a Keyring secret, and the reference wins when both are present, so a
connection file can describe a route without naming a single host. References are authorized for one
operation alongside the credentials, and an unresolvable reference fails the query rather than
falling back to the literal field. Resolved hosts and database names are interpolated into a remote
shell command, so both are character-restricted and a value carrying metacharacters is refused.

### Passphrase-protected SSH keys

An encrypted key is leased into the Windows OpenSSH agent for the length of one query. The passphrase
reaches `ssh-add` through `SSH_ASKPASS` pointing back at `scylla-workbench.exe`, which prints the
value from its own environment in askpass mode — nothing is written to disk and nothing appears in
argv. `ssh` itself still runs with `BatchMode=yes`, so it can never prompt.

Two Windows constraints shape this. The agent is a single system-wide service, so there is no scoped
per-operation agent; and it refuses `ssh-add -t` lifetime constraints, so a key cannot be given an
expiry. Eviction is therefore unconditional: one teardown guard evicts the key and shreds the staged
files on every exit path, and each operation evicts any stale copy of the same public key before
adding, in case an earlier run died. `ssh-add` matches on the key rather than the filename, which is
what makes that pre-emptive purge work across differently-named temp files.

Unencrypted keys do not involve the agent at all; they authenticate from the staged file with
`IdentitiesOnly=yes`, so no unrelated agent identity is offered.

The skill text keys every claim off whether the tool is actually registered. When it is not, the
agent is told plainly that Scylla cannot execute, and is forbidden from emitting an executable-looking
block or reporting the request as submitted. Earlier revisions asked for a fenced `scylla-query`
block that nothing consumed, which produced answers presented as completed queries when no query had
run.

### Current limits

- MySQL only, over `ConnectionRouteType::RemoteExecution` (`ssh` running `mysql` on the bastion).
- SSH key authentication only; SSH password authentication is refused.
- A connection with no pinned host public key cannot execute.
- A passphrase-protected key requires the **OpenSSH Authentication Agent** service to be running.
- Connections are created by editing `%LOCALAPPDATA%\ScyllaGPT\connections.json`; there is no editor
  UI yet.

Catalog scans skip reparse points and common generated directories (.git, build,
node_modules, vendor, third_party, .venv, __pycache__), cap traversal at 20,000 entries
per source and show at most 40 file matches. Workflow selection is capped at 12
references and 6000 bytes. Metadata reads are bounded to 16 KiB and cached by path,
mtime and size. Project-family and task-intent gates precede glob matching and
description keyword scoring; narrowly scoped legacy alwaysApply flags cannot
bypass those gates. Core lifecycle references form a small baseline. This is a
deterministic heuristic, not semantic classification; ambiguous tasks can omit a
useful source, so explicitly mention it when needed. The catalog refreshes for a new suggestion session and each
send. Large catalogs can take time to scan on the UI thread. No file bodies are
read by completion. Readable source permissions and project/agent availability are
checked during discovery.

Original user text travels in a JSON display envelope alongside internal provider
context. Restored transcripts display only that original text, preserving quotes
and newlines. Mode instructions, workflow paths and title metadata are internal;
they are still available to the provider. Legacy Scylla mode prefixes are stripped
when recognizable. Agent-authored explanations of skill use are still agent text.

Chat history keeps pinned chats first and sorts by latest activity within pinned
and unpinned entries. Local calendar headings show Today, Yesterday, or YYYY-MM-DD;
legacy chats with no timestamp use Earlier until activity or provider metadata
supplies a date. The app requests an optional short title in response metadata,
limits accepted names to four words, and hides that metadata in the transcript.
Manual names and existing legacy names are preserved. No extra naming API call
is made; providers may omit the suggestion.

Chat mouse selection and context menus require an actual content-row hit. Blank
space and date headings do not select chats. Keyboard navigation and the keyboard
context-menu shortcut continue to operate on the selected chat.

The project file-tree context menu exposes Refresh at the root, on child or stale
entries, on blank space, and through keyboard invocation. Refresh re-enumerates
the project and Knowledge trees from disk while leaving open editor tabs intact.

On normal close the app saves whether Chats is visible and the active thread.
The next launch resumes that chat when its provider/account is ready, within the
current project/account filter. Claude print history is stored locally for
restoration; Codex transcripts are read from its backend. Opening another chat or
starting a new one cancels pending startup restoration.
