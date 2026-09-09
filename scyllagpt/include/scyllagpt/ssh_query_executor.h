#pragma once

// Trusted executor: runs one read-only query on a remote database by way of the Windows OpenSSH
// client, matching ConnectionRouteType::RemoteExecution.
//
// Secret handling rules enforced here:
//   - The database password and username are written to the child's stdin, never to argv. argv is
//     world-readable through `ps` on the bastion.
//   - The SQL also travels on stdin, so no shell quoting of user SQL is ever required.
//   - The private key is materialized to a temp file with an owner-only DACL and deleted after.
//   - StrictHostKeyChecking is on, against a Scylla-written known_hosts. A connection with no
//     pinned host key cannot execute.
//   - A passphrase-protected key is loaded into the Windows OpenSSH agent for the duration of one
//     query and removed immediately afterwards. The passphrase reaches ssh-add through an askpass
//     helper (this same executable, in askpass mode) and never touches disk or argv.

#include "scyllagpt/connection_broker.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scyllagpt {

struct ProcessRunRequest {
    std::wstring executable;
    std::vector<std::wstring> arguments;
    std::string stdin_text;
    std::uint32_t timeout_ms = 0;
    // Added to (or overriding) the inherited environment. Used to hand a passphrase to ssh-add via
    // its askpass helper, which is why it must never be logged.
    std::vector<std::pair<std::wstring, std::wstring>> environment;
};

struct ProcessRunResult {
    bool spawned = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string standard_output;
    std::string standard_error;
};

// Seam so tests exercise the whole executor without an SSH client or a network.
using ProcessRunner = std::function<ProcessRunResult(const ProcessRunRequest&)>;

// Everything needed to invoke ssh, composed without touching the filesystem so it can be asserted
// in tests. `ok == false` means the connection cannot execute and `error` says why.
struct SshCommandPlan {
    bool ok = false;
    std::string error;
    std::vector<std::wstring> arguments;
    std::string stdin_text;         // "<user>\n<password>\n<sql>\n"
    std::string known_hosts_line;   // "<host> <pinned key>" or "[host]:port <pinned key>"
    std::string private_key_pem;    // caller writes this to key_path
    // Set when the key is passphrase-protected: the caller must lease the key into the OpenSSH
    // agent before running ssh and evict it afterwards. Unset keys authenticate from the file alone.
    bool needs_agent = false;
    std::string key_passphrase;
};

// key_path is referenced by `ssh -i`; known_hosts_path by UserKnownHostsFile. Neither is read here.
SshCommandPlan plan_ssh_mysql_command(const TrustedExecutionContext& context,
                                      const std::wstring& known_hosts_path,
                                      const std::wstring& key_path);

// Parses `mysql --batch` TSV (header row, tab-separated, backslash escapes) into the JSON payloads
// carried by ConnectionResult. Returns false when the output is not parseable as a result set.
bool parse_mysql_batch_output(std::string_view text, ConnectionResult* out);

// Strips anything that looks like a credential out of an SSH/MySQL diagnostic before it becomes a
// user-visible message.
std::string sanitize_execution_error(std::string_view standard_error);

class SshQueryExecutor final : public TrustedConnectionExecutor {
public:
    // runner defaults to a real CreateProcess-based runner when left empty.
    explicit SshQueryExecutor(std::wstring work_dir, ProcessRunner runner = {});
    ~SshQueryExecutor() override;

    ConnectionResult execute(const TrustedExecutionContext& context) override;
    void close(std::string_view operation_id) override;
    void close_all() override;

    // Resolved ssh.exe, or empty when the OpenSSH client is not installed.
    static std::wstring discover_ssh_client();
    // Resolved ssh-add.exe, needed only for passphrase-protected keys.
    static std::wstring discover_ssh_add();

private:
    // Loads key_path into the OpenSSH agent, evicting any stale copy of the same public key first.
    // Returns false when the agent is unreachable or the passphrase is wrong.
    bool agent_add_key(const std::wstring& ssh_add, const std::wstring& key_path,
                       const std::string& passphrase, std::string* error);
    void agent_remove_key(const std::wstring& ssh_add, const std::wstring& key_path,
                          const std::string& passphrase);

    std::wstring work_dir_;
    ProcessRunner runner_;
    std::mutex mutex_;
    std::unordered_map<std::string, bool> cancelled_;
};

// Askpass mode. Windows OpenSSH has no scoped agent and refuses `ssh-add -t` lifetime constraints,
// so the passphrase is handed to ssh-add through SSH_ASKPASS pointing back at this executable. The
// value travels in the child's environment; nothing is written to disk.
inline constexpr const wchar_t* kAskpassModeEnvVar = L"SCYLLA_SSH_ASKPASS_MODE";
inline constexpr const wchar_t* kAskpassValueEnvVar = L"SCYLLA_SSH_PASSPHRASE";

// True when this process was spawned by ssh-add as its askpass helper.
bool ssh_askpass_mode_requested();
// Writes the passphrase from the environment to stdout and returns an exit code.
int run_ssh_askpass_helper();

}  // namespace scyllagpt
