#include "scyllagpt/ssh_query_executor.h"

#include "scyllagpt/utf.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool value, const char* name) {
    if (value) std::cout << "ok   " << name << "\n";
    else { std::cerr << "FAIL " << name << "\n"; ++failures; }
}

const char* kKeyPem = "-----BEGIN OPENSSH PRIVATE KEY-----\nfake-key-material\n-----END OPENSSH PRIVATE KEY-----\n";
const char* kDbPassword = "sup3r-s3cret-pw";

scyllagpt::TrustedExecutionContext context() {
    scyllagpt::TrustedExecutionContext ctx;
    ctx.operation_id = "op-1";
    ctx.sql = "SELECT price FROM ticker WHERE symbol = 'BTCUSDT'";
    ctx.timeout_ms = 30000;
    auto& connection = ctx.connection;
    connection.project_id = "project-a";
    connection.name = "Synq HOT reader";
    connection.alias = "synq-hot-ro";
    connection.route_type = scyllagpt::ConnectionRouteType::RemoteExecution;
    connection.ssh.host = "bastion.example";
    connection.ssh.port = 2222;
    connection.ssh.username = "ubuntu";
    connection.ssh.private_key_ref = "scylla_PROD_SSH_KEY";
    connection.ssh.host_key = "ssh-ed25519 AAAAC3NzaPinnedHostKey";
    connection.database.host = "hot.cluster-ro.rds.amazonaws.com";
    connection.database.port = 3306;
    connection.database.database = "synq";
    connection.database.username_ref = "scylla_DB_USER";
    connection.database.password_ref = "scylla_DB_PASSWORD";
    ctx.credentials.values.emplace_back("scylla_PROD_SSH_KEY", kKeyPem);
    ctx.credentials.values.emplace_back("scylla_DB_USER", "readonly");
    ctx.credentials.values.emplace_back("scylla_DB_PASSWORD", kDbPassword);
    return ctx;
}

std::string joined_arguments(const scyllagpt::SshCommandPlan& plan) {
    std::string out;
    for (const auto& argument : plan.arguments) {
        out += scyllagpt::utf8(argument);
        out.push_back('\x1f');
    }
    return out;
}
}  // namespace

int run_ssh_query_executor_tests() {
    using namespace scyllagpt;
    failures = 0;

    const auto plan = plan_ssh_mysql_command(context(), L"C:\\tmp\\known_hosts", L"C:\\tmp\\op.key");
    expect(plan.ok, "plan a remote-execution command");
    const std::string arguments = joined_arguments(plan);

    // The whole point of the design: no credential may appear in argv.
    expect(arguments.find(kDbPassword) == std::string::npos, "password never reaches argv");
    expect(arguments.find("fake-key-material") == std::string::npos, "private key never reaches argv");
    expect(arguments.find("readonly") == std::string::npos, "database username never reaches argv");
    expect(arguments.find("SELECT price") == std::string::npos, "sql never reaches argv");

    expect(plan.stdin_text == std::string("readonly\n") + kDbPassword +
                                  "\nSELECT price FROM ticker WHERE symbol = 'BTCUSDT'\n",
           "stdin carries username, password, then sql in order");
    expect(plan.private_key_pem == kKeyPem, "plan carries the key for the caller to stage");
    expect(plan.known_hosts_line == "[bastion.example]:2222 ssh-ed25519 AAAAC3NzaPinnedHostKey",
           "non-default port is bracketed in known_hosts");

    expect(arguments.find("StrictHostKeyChecking=yes") != std::string::npos, "host key checking stays on");
    expect(arguments.find("BatchMode=yes") != std::string::npos, "ssh never prompts");
    expect(arguments.find("PasswordAuthentication=no") != std::string::npos, "password auth is refused");
    expect(arguments.find("IdentitiesOnly=yes") != std::string::npos, "agent identities are ignored");
    expect(arguments.find("C:\\tmp\\known_hosts") != std::string::npos, "known_hosts path is passed");
    expect(!plan.needs_askpass && plan.key_passphrase.empty(), "an unencrypted key needs no askpass");

    // Hosts, ports, and usernames may themselves live in the Keyring, so nothing about the
    // infrastructure has to be stored in the connection file.
    auto referenced = context();
    referenced.connection.ssh.host.clear();
    referenced.connection.ssh.host_ref = "scylla_SSH_HOST";
    referenced.connection.ssh.username.clear();
    referenced.connection.ssh.username_ref = "scylla_SSH_USER";
    referenced.connection.ssh.port_ref = "scylla_SSH_PORT";
    referenced.connection.database.host.clear();
    referenced.connection.database.host_ref = "scylla_DB_HOST";
    referenced.connection.database.port_ref = "scylla_DB_PORT";
    referenced.credentials.values.emplace_back("scylla_SSH_HOST", "bastion.example");
    referenced.credentials.values.emplace_back("scylla_SSH_USER", "ubuntu");
    referenced.credentials.values.emplace_back("scylla_SSH_PORT", "2222");
    referenced.credentials.values.emplace_back("scylla_DB_HOST", "hot.internal");
    referenced.credentials.values.emplace_back("scylla_DB_PORT", "6060");
    const auto by_ref = plan_ssh_mysql_command(referenced, L"h", L"k");
    expect(by_ref.ok, "plan resolves hosts and ports from the Keyring");
    const std::string ref_arguments = joined_arguments(by_ref);
    expect(ref_arguments.find("ubuntu@bastion.example") != std::string::npos, "referenced ssh host and user resolve");
    expect(ref_arguments.find("-p\x1f" "2222") != std::string::npos, "referenced ssh port resolves");
    expect(ref_arguments.find("--host=hot.internal") != std::string::npos, "referenced database host resolves");
    expect(ref_arguments.find("--port=6060") != std::string::npos, "referenced database port resolves");
    expect(by_ref.known_hosts_line == "[bastion.example]:2222 ssh-ed25519 AAAAC3NzaPinnedHostKey",
           "known_hosts uses the resolved host");

    // A reference that resolves to nothing must fail rather than fall back to the literal field.
    auto stale_ref = context();
    stale_ref.connection.ssh.host_ref = "scylla_MISSING_HOST";
    expect(!plan_ssh_mysql_command(stale_ref, L"h", L"k").ok, "an unresolved host reference fails closed");
    auto bad_port = context();
    bad_port.connection.database.port_ref = "scylla_DB_PORT";
    bad_port.credentials.values.emplace_back("scylla_DB_PORT", "not-a-port");
    expect(!plan_ssh_mysql_command(bad_port, L"h", L"k").ok, "a non-numeric port reference fails closed");

    // A resolved host reaches a remote shell command line, so metacharacters are refused outright.
    auto injected = context();
    injected.connection.database.host_ref = "scylla_DB_HOST";
    injected.credentials.values.emplace_back("scylla_DB_HOST", "hot.internal; curl evil.example");
    expect(!plan_ssh_mysql_command(injected, L"h", L"k").ok, "refuse a host carrying shell metacharacters");

    // Passphrase-protected keys route through ssh askpass instead of a shared agent import.
    auto passphrase = context();
    passphrase.connection.ssh.key_passphrase_ref = "scylla_KEY_PASS";
    passphrase.credentials.values.emplace_back("scylla_KEY_PASS", "open-sesame");
    const auto with_passphrase = plan_ssh_mysql_command(passphrase, L"h", L"k");
    expect(with_passphrase.ok, "accept a passphrase-protected key");
    expect(with_passphrase.needs_askpass, "a passphrase-protected key requests askpass");
    expect(with_passphrase.key_passphrase == "open-sesame", "plan carries the passphrase for askpass");
    expect(joined_arguments(with_passphrase).find("open-sesame") == std::string::npos,
           "passphrase never reaches argv");
    expect(joined_arguments(with_passphrase).find("BatchMode=yes") == std::string::npos,
           "BatchMode is omitted so askpass can supply the key passphrase");
    expect(joined_arguments(with_passphrase).find("PreferredAuthentications=publickey") != std::string::npos,
           "password prompts stay disabled without BatchMode");
    expect(joined_arguments(with_passphrase).find("IdentitiesOnly=yes") != std::string::npos,
           "encrypted keys remain pinned to the staged identity");
    auto unresolved_passphrase = context();
    unresolved_passphrase.connection.ssh.key_passphrase_ref = "scylla_KEY_PASS";
    expect(!plan_ssh_mysql_command(unresolved_passphrase, L"h", L"k").ok,
           "an unresolved passphrase reference fails closed");

    auto default_port = context();
    default_port.connection.ssh.port = 22;
    expect(plan_ssh_mysql_command(default_port, L"h", L"k").known_hosts_line ==
               "bastion.example ssh-ed25519 AAAAC3NzaPinnedHostKey",
           "default port is not bracketed");

    // Fail-closed cases.
    auto no_host_key = context();
    no_host_key.connection.ssh.host_key.clear();
    expect(!plan_ssh_mysql_command(no_host_key, L"h", L"k").ok, "refuse an unpinned host");
    auto password_auth = context();
    password_auth.connection.ssh.private_key_ref.clear();
    password_auth.connection.ssh.auth_ref = "scylla_SSH_PASSWORD";
    expect(!plan_ssh_mysql_command(password_auth, L"h", L"k").ok, "refuse ssh password authentication");
    auto direct = context();
    direct.connection.route_type = ConnectionRouteType::Direct;
    expect(!plan_ssh_mysql_command(direct, L"h", L"k").ok, "refuse a direct route");
    auto postgres = context();
    postgres.connection.database.engine = DatabaseEngine::PostgreSql;
    expect(!plan_ssh_mysql_command(postgres, L"h", L"k").ok, "refuse an unimplemented engine");
    auto missing_secret = context();
    missing_secret.credentials.values.clear();
    expect(!plan_ssh_mysql_command(missing_secret, L"h", L"k").ok, "refuse when credentials are unresolved");

    // Batch output parsing.
    ConnectionResult parsed;
    expect(parse_mysql_batch_output("symbol\tprice\nBTCUSDT\t64000.00\nETHUSDT\t3200.50\n", &parsed),
           "parse batch result set");
    expect(parsed.row_count == 2, "row count excludes the header");
    expect(parsed.columns_json == R"(["symbol","price"])", "columns parsed");
    expect(parsed.rows_json == R"([["BTCUSDT","64000.00"],["ETHUSDT","3200.50"]])", "rows parsed");

    ConnectionResult nulls;
    expect(parse_mysql_batch_output("a\tb\n\\N\tvalue\n", &nulls), "parse batch nulls");
    expect(nulls.rows_json == R"([[null,"value"]])", "\\N becomes json null");

    ConnectionResult escaped;
    expect(parse_mysql_batch_output("note\nline\\nbreak\ttrailing\n", &escaped), "parse escapes");
    expect(escaped.rows_json.find("line\\nbreak") != std::string::npos, "escaped newline is unescaped into the value");

    ConnectionResult empty;
    expect(parse_mysql_batch_output("", &empty), "empty output is a success");
    expect(empty.row_count == 0 && empty.rows_json == "[]", "empty output yields no rows");

    // Error sanitizing must not forward raw stderr.
    expect(sanitize_execution_error("ERROR 1045 (28000): Access denied for user 'readonly'@'10.0.0.1'")
               .find("Access denied") != std::string::npos,
           "recognizable errors are kept");
    expect(sanitize_execution_error("Warning: MYSQL_PWD=sup3r-s3cret-pw is insecure")
               .find("sup3r-s3cret-pw") == std::string::npos,
           "stderr carrying a value is suppressed");
    expect(sanitize_execution_error("something totally unexpected") ==
               "The remote command failed. See the Scylla connection log for details.",
           "unknown errors get a generic message");
    expect(sanitize_execution_error("Host key verification failed.").find("Host key") != std::string::npos,
           "host key failure is surfaced");
    expect(sanitize_execution_error("Load key \"op.key\": incorrect passphrase\r\n"
                                    "ubuntu@bastion: Permission denied (publickey).\r\n")
               .find("Permission denied") != std::string::npos,
           "later-line OpenSSH diagnostics are surfaced");
    expect(sanitize_execution_error("Load key \"op.key\": error in libcrypto").find("Load key") !=
               std::string::npos,
           "key load failures are surfaced");

    // Whole-executor path with a fake runner: no ssh, no network.
    ProcessRunRequest captured;
    auto runner = [&captured](const ProcessRunRequest& request) {
        captured = request;
        ProcessRunResult result;
        result.spawned = true;
        result.exit_code = 0;
        result.standard_output = "price\n64000.00\n";
        return result;
    };
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    SshQueryExecutor executor(std::wstring(temp) + L"scylla-broker-test", runner);
    const ConnectionResult executed = executor.execute(context());
    if (SshQueryExecutor::discover_ssh_client().empty()) {
        expect(!executed.ok && !executed.warning.empty(), "missing OpenSSH client fails closed");
    } else {
        expect(executed.ok, "fake-backed execution succeeds");
        expect(executed.row_count == 1, "fake-backed execution returns rows");
        expect(captured.stdin_text.find(kDbPassword) != std::string::npos,
               "runner receives the password on stdin");
        std::string spawn_arguments;
        for (const auto& argument : captured.arguments) spawn_arguments += utf8(argument);
        expect(spawn_arguments.find(kDbPassword) == std::string::npos,
               "spawned argv holds no password");
    }

    // Encrypted keys are passed directly to ssh. This avoids legacy PEM compatibility problems in
    // ssh-add while preserving the same noninteractive, secret-free argv contract.
    if (!SshQueryExecutor::discover_ssh_client().empty()) {
        std::vector<ProcessRunRequest> spawns;
        auto recording_runner = [&spawns](const ProcessRunRequest& request) {
            spawns.push_back(request);
            ProcessRunResult result;
            result.spawned = true;
            result.exit_code = 0;
            result.standard_output = "price\n64000.00\n";
            return result;
        };
        auto leased = context();
        leased.connection.ssh.key_passphrase_ref = "scylla_KEY_PASS";
        leased.credentials.values.emplace_back("scylla_KEY_PASS", "open-sesame");
        SshQueryExecutor askpass_executor(std::wstring(temp) + L"scylla-broker-test", recording_runner);
        const ConnectionResult askpass_result = askpass_executor.execute(leased);
        expect(askpass_result.ok, "askpass execution succeeds");

        auto describe = [](const ProcessRunRequest& request) {
            std::string out = utf8(request.executable);
            for (const auto& argument : request.arguments) out += " " + utf8(argument);
            return out;
        };
        expect(spawns.size() == 1, "encrypted key requires only the ssh query process");
        if (spawns.size() == 1) {
            expect(describe(spawns[0]).find("ssh.exe") != std::string::npos, "askpass is attached to ssh");
            expect(describe(spawns[0]).find("open-sesame") == std::string::npos,
                   "passphrase stays out of ssh argv");
            expect(describe(spawns[0]).find("BatchMode=yes") == std::string::npos,
                   "askpass ssh spawn omits BatchMode");
            bool askpass_points_at_us = false, forced = false, carries_passphrase = false;
            for (const auto& [key, value] : spawns[0].environment) {
                if (key == L"SSH_ASKPASS") askpass_points_at_us = utf8(value).find(".exe") != std::string::npos;
                if (key == L"SSH_ASKPASS_REQUIRE") forced = value == L"force";
                if (key == kAskpassValueEnvVar) carries_passphrase = utf8(value) == "open-sesame";
            }
            expect(askpass_points_at_us, "ssh is pointed at an askpass program");
            expect(forced, "askpass is forced so no console prompt can appear");
            expect(carries_passphrase, "the passphrase travels only in the ssh process environment");
        }
    }
    return failures;
}
