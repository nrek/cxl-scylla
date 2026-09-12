#include "scyllagpt/ssh_query_executor.h"
#include "scyllagpt/ssh_terminal_script.h"
#include "scyllagpt/terminal_profiles.h"
#include "scyllagpt/settings.h"
#include <shellapi.h>
#include <thread>

#include "scyllagpt/json.h"
#include "scyllagpt/paths.h"
#include "scyllagpt/utf.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace scyllagpt {
namespace {

const char* engine_client(DatabaseEngine engine) {
    switch (engine) {
        case DatabaseEngine::MySql: return "mysql";
        default: return nullptr;  // PostgreSQL / SQL Server remote execution not implemented
    }
}

// The remote side reads username, password, then SQL from stdin. Nothing sensitive is interpolated
// into this script, so it needs no escaping of user data.
std::string remote_mysql_script(const std::string& host, std::uint16_t port,
                                const std::string& database, std::uint32_t timeout_seconds) {
    std::string script;
    script += "IFS= read -r SCYLLA_DB_USER\n";
    script += "IFS= read -r SCYLLA_DB_PASS\n";
    script += "MYSQL_PWD=\"$SCYLLA_DB_PASS\" mysql";
    script += " --host=" + host;
    script += " --port=" + std::to_string(port);
    script += " --user=\"$SCYLLA_DB_USER\"";
    if (!database.empty()) script += " --database=" + database;
    script += " --batch --raw";
    script += " --connect-timeout=" + std::to_string(timeout_seconds == 0 ? 30u : timeout_seconds);
    script += "\n";
    return script;
}

// A hostname bound for a remote shell command line. Rejecting anything outside this set keeps a
// Keyring value from smuggling shell metacharacters into remote_mysql_script.
bool is_safe_host(std::string_view host) {
    if (host.empty() || host.size() > 253) return false;
    for (char c : host) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                             c == '.' || c == '-' || c == '_' || c == ':';
        if (!allowed) return false;
    }
    return true;
}

// Same reasoning for the database name, which is also interpolated into the remote command.
bool is_safe_identifier(std::string_view value) {
    for (char c : value) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                             c == '_' || c == '-' || c == '$';
        if (!allowed) return false;
    }
    return true;
}

bool parse_port(std::string_view text, std::uint16_t* out) {
    if (text.empty() || text.size() > 5 || !out) return false;
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535) return false;
    *out = static_cast<std::uint16_t>(value);
    return true;
}

// mysql --batch escapes tab, newline, carriage return and backslash inside field values.
std::string unescape_batch_field(std::string_view field) {
    std::string out;
    out.reserve(field.size());
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\' || i + 1 >= field.size()) {
            out.push_back(field[i]);
            continue;
        }
        switch (field[++i]) {
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case '0': out.push_back('\0'); break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back(field[i]); break;
        }
    }
    return out;
}

// Fields stay raw here. SQL NULL is the literal two characters \N, which is indistinguishable
// from an escaped value once unescaping has run, so the NULL test has to happen first.
std::vector<std::string> split_batch_row(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            fields.emplace_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

Json json_string_array(const std::vector<std::string>& raw_fields) {
    Json array = Json::array();
    for (const auto& raw : raw_fields) {
        if (raw == "\\N") array.push(Json::null());
        else array.push(Json::string(unescape_batch_field(raw)));
    }
    return array;
}

bool write_private_file(const std::wstring& path, std::string_view body) {
    // Windows OpenSSH rejects the generic OWNER RIGHTS SID (S-1-3-4) even when it resolves to the
    // file owner. Build the protected DACL with the current user's concrete SID instead, matching
    // the ACL shape accepted for keys under %USERPROFILE%\.ssh.
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    std::vector<BYTE> token_info(token_bytes);
    if (token_bytes == 0 ||
        !GetTokenInformation(token, TokenUser, token_info.data(), token_bytes, &token_bytes)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_info.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_text)) return false;
    const std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        return false;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok && written == body.size();
}

void shred_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 1024 * 1024) {
            std::string zeros(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD written = 0;
            WriteFile(file, zeros.data(), static_cast<DWORD>(zeros.size()), &written, nullptr);
            FlushFileBuffers(file);
        }
        CloseHandle(file);
    }
    DeleteFileW(path.c_str());
}

std::wstring quote_argument(const std::wstring& value) {
    const bool needs_quotes = value.empty() || value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needs_quotes) return value;
    std::wstring out;
    out.push_back(L'"');
    for (std::size_t i = 0; i < value.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < value.size() && value[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == value.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (value[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(value[i]);
    }
    out.push_back(L'"');
    return out;
}

// Copies the current environment, replacing any entry the request overrides. The result may hold a
// passphrase, so the caller zeroes it as soon as CreateProcessW returns.
std::wstring build_environment_block(const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
    std::wstring block;
    auto overridden = [&](std::wstring_view name) {
        for (const auto& [key, value] : overrides) {
            if (key.size() == name.size() &&
                CompareStringOrdinal(key.c_str(), static_cast<int>(key.size()), name.data(),
                                     static_cast<int>(name.size()), TRUE) == CSTR_EQUAL) {
                return true;
            }
        }
        return false;
    };
    if (LPWCH inherited = GetEnvironmentStringsW()) {
        for (LPWCH cursor = inherited; *cursor;) {
            const std::wstring_view entry(cursor);
            const std::size_t equals = entry.find(L'=');
            // Skip the "=X:" drive-relative entries and anything we are replacing.
            if (equals != std::wstring_view::npos && equals > 0 && !overridden(entry.substr(0, equals))) {
                block.append(entry);
                block.push_back(L'\0');
            }
            cursor += entry.size() + 1;
        }
        FreeEnvironmentStringsW(inherited);
    }
    for (const auto& [key, value] : overrides) {
        block += key;
        block.push_back(L'=');
        block += value;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

// Environment handed to ssh so it can obtain the private-key passphrase without a console prompt.
// SSH_ASKPASS_REQUIRE=force is what makes this work even though a console may be attached; DISPLAY
// is set as well because older OpenSSH builds refuse askpass without it.
std::vector<std::pair<std::wstring, std::wstring>> askpass_environment(const std::string& passphrase) {
    std::wstring self(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    self.resize(length);
    // Fluent has no askpass process mode. The native helper is deployed alongside it.
    const auto slash = self.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const auto helper = self.substr(0, slash + 1) + L"scylla-broker.exe";
        if (file_exists(helper)) self = helper;
    }
    return {
        {L"SSH_ASKPASS", self},
        {L"SSH_ASKPASS_REQUIRE", L"force"},
        {L"DISPLAY", L"localhost:0"},
        {kAskpassModeEnvVar, L"1"},
        {kAskpassValueEnvVar, utf16(passphrase)},
    };
}

void wipe_environment(std::vector<std::pair<std::wstring, std::wstring>>& environment) {
    for (auto& [key, value] : environment) {
        if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    }
    environment.clear();
}

ProcessRunResult run_process(const ProcessRunRequest& request) {
    ProcessRunResult result;
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
    HANDLE err_read = nullptr, err_write = nullptr;
    auto close_handle = [](HANDLE& handle) {
        if (handle) {
            CloseHandle(handle);
            handle = nullptr;
        }
    };
    auto cleanup = [&]() {
        close_handle(in_read);
        close_handle(in_write);
        close_handle(out_read);
        close_handle(out_write);
        close_handle(err_read);
        close_handle(err_write);
    };
    if (!CreatePipe(&in_read, &in_write, &inheritable, 0) ||
        !CreatePipe(&out_read, &out_write, &inheritable, 0) ||
        !CreatePipe(&err_read, &err_write, &inheritable, 0)) {
        cleanup();
        return result;
    }
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quote_argument(request.executable);
    for (const auto& argument : request.arguments) {
        command.push_back(L' ');
        command += quote_argument(argument);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in_read;
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    std::wstring environment;
    if (!request.environment.empty()) environment = build_environment_block(request.environment);
    const BOOL started = CreateProcessW(request.executable.c_str(), mutable_command.data(), nullptr,
                                        nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        environment.empty() ? nullptr : environment.data(), nullptr,
                                        &startup, &process);
    if (!environment.empty()) SecureZeroMemory(environment.data(), environment.size() * sizeof(wchar_t));
    close_handle(in_read);
    close_handle(out_write);
    close_handle(err_write);
    if (!started) {
        cleanup();
        return result;
    }
    result.spawned = true;

    auto drain = [](HANDLE pipe, std::string& sink) {
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            if (sink.size() < 16u * 1024u * 1024u) sink.append(buffer, read);
        }
    };
    // Drain both streams concurrently and apply the timeout while I/O is in progress.
    std::thread stdout_reader([&] { drain(out_read, result.standard_output); });
    std::thread stderr_reader([&] { drain(err_read, result.standard_error); });
    std::thread input_writer([&] {
        size_t offset = 0;
        while (offset < request.stdin_text.size()) {
            DWORD written = 0;
            if (!WriteFile(in_write, request.stdin_text.data() + offset,
                static_cast<DWORD>((std::min)(request.stdin_text.size() - offset, size_t{4096})), &written, nullptr) || !written) break;
            offset += written;
        }
        close_handle(in_write);
    });

    const DWORD timeout = request.timeout_ms == 0 ? INFINITE : request.timeout_ms;
    if (WaitForSingleObject(process.hProcess, timeout) == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    if (result.timed_out) {
        CancelSynchronousIo(input_writer.native_handle());
        CancelSynchronousIo(stdout_reader.native_handle());
        CancelSynchronousIo(stderr_reader.native_handle());
    }
    input_writer.join();
    stdout_reader.join();
    stderr_reader.join();
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) result.exit_code = static_cast<int>(exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    cleanup();
    return result;
}

}  // namespace

SshCommandPlan plan_ssh_mysql_command(const TrustedExecutionContext& context,
                                     const std::wstring& known_hosts_path,
                                     const std::wstring& key_path) {
    SshCommandPlan plan;
    const ProjectConnection& connection = context.connection;
    auto fail = [&](std::string message) {
        plan.ok = false;
        plan.error = std::move(message);
        return plan;
    };

    if (connection.route_type == ConnectionRouteType::Direct) {
        return fail("This connection is configured for direct access, which the trusted executor "
                    "does not provide. Use an SSH remote-execution connection.");
    }
    if (engine_client(connection.database.engine) == nullptr) {
        return fail("Only MySQL remote execution is implemented.");
    }
    if (connection.ssh.host_key.empty()) {
        return fail("The connection has no pinned SSH host key, so the host cannot be verified.");
    }
    if (connection.ssh.private_key_ref.empty()) {
        return fail("SSH password authentication is not supported. Configure a private key "
                    "reference on this connection.");
    }

    // A *_ref resolves through this operation's one-shot credentials; a plain field is the fallback.
    // An unresolvable reference is fatal rather than silently falling back to the literal.
    bool resolution_failed = false;
    auto resolve = [&](const std::string& literal, const std::string& ref) -> std::string {
        if (ref.empty()) return literal;
        const std::string value = context.credentials.lookup(ref);
        if (value.empty()) resolution_failed = true;
        return value;
    };
    auto resolve_port = [&](std::uint16_t literal, const std::string& ref) -> std::uint16_t {
        if (ref.empty()) return literal;
        std::uint16_t parsed = 0;
        if (!parse_port(context.credentials.lookup(ref), &parsed)) {
            resolution_failed = true;
            return literal;
        }
        return parsed;
    };

    const std::string ssh_host = resolve(connection.ssh.host, connection.ssh.host_ref);
    const std::string ssh_user = resolve(connection.ssh.username, connection.ssh.username_ref);
    const std::uint16_t ssh_port = resolve_port(connection.ssh.port, connection.ssh.port_ref);
    const std::string db_host = resolve(connection.database.host, connection.database.host_ref);
    const std::uint16_t db_port = resolve_port(connection.database.port, connection.database.port_ref);
    const std::string key_pem = context.credentials.lookup(connection.ssh.private_key_ref);
    const std::string db_user = context.credentials.lookup(connection.database.username_ref);
    const std::string db_password = context.credentials.lookup(connection.database.password_ref);
    const std::string passphrase = connection.ssh.key_passphrase_ref.empty()
                                       ? std::string()
                                       : context.credentials.lookup(connection.ssh.key_passphrase_ref);

    if (resolution_failed) return fail("A host or port held in the Keyring could not be resolved.");
    if (ssh_host.empty() || ssh_user.empty()) return fail("The connection is missing its SSH host or username.");
    if (db_host.empty()) return fail("The connection is missing its database host.");
    if (key_pem.empty()) return fail("The SSH private key could not be resolved from the Keyring.");
    if (db_user.empty()) return fail("The database username could not be resolved from the Keyring.");
    if (db_password.empty()) return fail("The database password could not be resolved from the Keyring.");
    if (!connection.ssh.key_passphrase_ref.empty() && passphrase.empty())
        return fail("The SSH key passphrase could not be resolved from the Keyring.");
    if (!is_safe_host(ssh_host) || !is_safe_host(db_host))
        return fail("A resolved host contains characters that are not valid in a hostname.");
    if (!is_safe_identifier(connection.database.database))
        return fail("The database name contains characters that are not allowed.");

    const std::uint32_t timeout_seconds = connection.result_policy.timeout_seconds;
    plan.private_key_pem = key_pem;
    plan.needs_askpass = !passphrase.empty();
    plan.key_passphrase = passphrase;
    plan.known_hosts_line =
        (ssh_port == 22 ? ssh_host : "[" + ssh_host + "]:" + std::to_string(ssh_port)) + " " +
        connection.ssh.host_key;

    plan.arguments = {
        L"-p", std::to_wstring(ssh_port),
        L"-T",                              // no pty: stdin stays a clean pipe
        L"-o", L"StrictHostKeyChecking=yes",
        L"-o", L"PasswordAuthentication=no",
        L"-o", L"KbdInteractiveAuthentication=no",
        L"-o", L"PubkeyAuthentication=yes",
        L"-o", L"PreferredAuthentications=publickey",
        L"-o", L"UserKnownHostsFile=" + known_hosts_path,
        L"-o", L"ConnectTimeout=" + std::to_wstring(timeout_seconds == 0 ? 30u : timeout_seconds),
        L"-i", key_path,
    };
    // BatchMode disables passphrase querying. That is correct for unencrypted keys (never hang on a
    // prompt), but it also blocks SSH_ASKPASS on some OpenSSH builds — including Windows — so an
    // encrypted key must omit it and rely on askpass + the auth options above instead.
    if (!plan.needs_askpass) {
        plan.arguments.push_back(L"-o");
        plan.arguments.push_back(L"BatchMode=yes");
    }
    // Always pin authentication to the staged identity. Encrypted keys are decrypted by this ssh
    // process through askpass, so no shared-agent identity is needed or accepted.
    plan.arguments.push_back(L"-o");
    plan.arguments.push_back(L"IdentitiesOnly=yes");
    plan.arguments.push_back(utf16(ssh_user + "@" + ssh_host));
    plan.arguments.push_back(utf16(remote_mysql_script(db_host, db_port, connection.database.database,
                                                      timeout_seconds)));
    // Order matters: the remote script reads username, then password, then SQL to EOF.
    plan.stdin_text = db_user + "\n" + db_password + "\n" + context.sql + "\n";
    plan.ok = true;
    return plan;
}

bool parse_mysql_batch_output(std::string_view text, ConnectionResult* out) {
    if (!out) return false;
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (i > start) {
                std::string_view line = text.substr(start, i - start);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                lines.emplace_back(line);
            }
            start = i + 1;
        }
    }
    if (lines.empty()) {
        // A statement with no result set (or an empty one) is still a success.
        out->columns_json = "[]";
        out->rows_json = "[]";
        out->row_count = 0;
        return true;
    }
    const auto columns = split_batch_row(lines.front());
    Json rows = Json::array();
    for (std::size_t i = 1; i < lines.size(); ++i) {
        rows.push(json_string_array(split_batch_row(lines[i])));
    }
    out->columns_json = json_string_array(columns).dump();
    out->rows_json = rows.dump();
    out->row_count = lines.size() - 1;
    return true;
}

std::string sanitize_execution_error(std::string_view standard_error) {
    // mysql and ssh both echo option text on failure. Keep only the recognizable, credential-free
    // diagnostics rather than forwarding raw stderr to the agent.
    static constexpr std::string_view kSafe[] = {
        "Access denied",
        "Unknown database",
        "Unknown column",
        "Table",
        "syntax error",
        "Host key verification failed",
        "Permission denied",
        "Connection timed out",
        "Could not resolve hostname",
        "Connection refused",
        "command not found",
        "Load key",
        "incorrect passphrase",
        "Bad permissions",
        "UNPROTECTED PRIVATE KEY",
        "No route to host",
        "Network is unreachable",
        "Name or service not known",
        "mysql:",
        "ERROR ",
    };
    auto looks_credentialed = [](std::string_view line) {
        return line.find("PWD=") != std::string_view::npos ||
               line.find("password=") != std::string_view::npos ||
               line.find("SCYLLA_SSH_PASSPHRASE=") != std::string_view::npos;
    };
    auto score_line = [](std::string_view line) -> int {
        // Prefer the actionable OpenSSH/MySQL diagnosis over an earlier, weaker "Load key ..." line.
        if (line.find("Permission denied") != std::string_view::npos ||
            line.find("Host key verification failed") != std::string_view::npos ||
            line.find("Access denied") != std::string_view::npos ||
            line.find("incorrect passphrase") != std::string_view::npos ||
            line.find("Connection timed out") != std::string_view::npos ||
            line.find("Could not resolve hostname") != std::string_view::npos) {
            return 3;
        }
        if (line.find("Load key") != std::string_view::npos) return 1;
        return 2;
    };
    std::string best;
    int best_score = -1;
    std::size_t start = 0;
    while (start <= standard_error.size()) {
        const std::size_t end = standard_error.find('\n', start);
        std::string_view line = standard_error.substr(
            start, end == std::string_view::npos ? standard_error.size() - start : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && !looks_credentialed(line)) {
            for (const auto& safe : kSafe) {
                if (line.find(safe) != std::string_view::npos) {
                    const int score = score_line(line);
                    if (score >= best_score) {
                        best.assign(line);
                        best_score = score;
                    }
                    break;
                }
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!best.empty()) return best;
    return "The remote command failed. See the Scylla connection log for details.";
}

// Appends a credential-free diagnostic to broker-run/connection.log so the generic sanitize message
// is not a dead end. Never writes stdin, environment values, or key material.
void append_connection_log(const std::wstring& work_dir, std::string_view heading,
                           std::string_view standard_error, int exit_code) {
    if (work_dir.empty() || !ensure_dir(work_dir)) return;
    const std::wstring path = join_path(work_dir, L"connection.log");
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    char stamp[64]{};
    sprintf_s(stamp, "%04u-%02u-%02uT%02u:%02u:%02uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour,
              utc.wMinute, utc.wSecond);

    std::string body;
    body.reserve(standard_error.size() + 128);
    body += "---- ";
    body += stamp;
    body += " ";
    body += heading;
    body += " exit=";
    body += std::to_string(exit_code);
    body += " ----\n";
    if (standard_error.empty()) {
        body += "(no stderr captured)\n";
    } else {
        // Drop lines that look like they carry a value; everything else is useful for diagnosis.
        std::size_t start = 0;
        while (start <= standard_error.size()) {
            const std::size_t end = standard_error.find('\n', start);
            std::string_view line = standard_error.substr(
                start, end == std::string_view::npos ? standard_error.size() - start : end - start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const bool secretish = line.find("PWD=") != std::string_view::npos ||
                                   line.find("password=") != std::string_view::npos ||
                                   line.find("SCYLLA_SSH_PASSPHRASE=") != std::string_view::npos;
            if (!secretish) {
                body.append(line);
                body.push_back('\n');
            }
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
    }
    body.push_back('\n');
    DWORD written = 0;
    WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(file);
}

SshQueryExecutor::SshQueryExecutor(std::wstring work_dir, ProcessRunner runner)
    : work_dir_(std::move(work_dir)), runner_(runner ? std::move(runner) : ProcessRunner(run_process)) {}

SshQueryExecutor::~SshQueryExecutor() { close_all(); }

std::wstring SshQueryExecutor::discover_ssh_client() {
    wchar_t system_dir[MAX_PATH]{};
    if (GetSystemDirectoryW(system_dir, MAX_PATH) == 0) return {};
    const std::wstring candidate = join_path(system_dir, L"OpenSSH\\ssh.exe");
    if (file_exists(candidate)) return candidate;
    return {};
}

bool ssh_askpass_mode_requested() {
    wchar_t marker[8]{};
    return GetEnvironmentVariableW(kAskpassModeEnvVar, marker, 8) > 0 && marker[0] == L'1';
}

int run_ssh_askpass_helper() {
    std::wstring value(4096, L'\0');
    const DWORD length = GetEnvironmentVariableW(kAskpassValueEnvVar, value.data(),
                                                 static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return 1;
    value.resize(length);
    // OpenSSH expects the passphrase on stdout, newline-terminated.
    std::string out = utf8(value);
    SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    out.push_back('\n');
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    const BOOL ok = stdout_handle && stdout_handle != INVALID_HANDLE_VALUE &&
                    WriteFile(stdout_handle, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    SecureZeroMemory(out.data(), out.size());
    return ok ? 0 : 1;
}

ConnectionResult SshQueryExecutor::execute(const TrustedExecutionContext& context) {
    ConnectionResult result;
    if (context.connection.ssh_only) {
        const auto paths = make_paths();
        const auto settings = load_settings(paths.settings_path);
        if (terminal_agent_policy(settings.terminal_profile_policy, context.connection.terminal_profile_id,
            settings.agent_terminal_policy) != "allow") {
            result.warning = "Agent terminal execution is not allowed by Terminal settings.";
            return result;
        }
        const auto profiles = merge_terminal_profiles(discover_terminal_profiles(),
            load_custom_terminal_profiles(paths.terminals_path), settings.terminal_profile_enabled);
        const TerminalProfile* profile = nullptr;
        for (const auto& candidate : profiles)
            if (candidate.id == context.connection.terminal_profile_id && candidate.enabled) profile = &candidate;
        if (!profile) { result.warning = "The selected terminal profile is unavailable or disabled."; return result; }
        const auto slash = profile->executable.find_last_of(L"\\/");
        if (_wcsicmp(profile->executable.substr(slash == std::wstring::npos ? 0 : slash + 1).c_str(), L"wsl.exe") != 0) {
            result.warning = "SSH command connections currently require a WSL terminal profile."; return result;
        }
        ProcessRunRequest request;
        request.executable = profile->executable;
        int argc = 0;
        auto argv = CommandLineToArgvW((L"wsl.exe " + profile->args).c_str(), &argc);
        if (!argv) { result.warning = "Invalid WSL profile arguments."; return result; }
        for (int i = 1; i < argc; ++i) request.arguments.push_back(argv[i]);
        LocalFree(argv);
        // Only a distribution selector is accepted; custom command/profile switches cannot
        // redirect the secret-bearing stdin to a different program.
        if (!request.arguments.empty() && !(request.arguments.size() == 2 &&
            (request.arguments[0] == L"-d" || request.arguments[0] == L"--distribution"))) {
            result.warning = "Use a WSL profile with only a distribution selector."; return result;
        }
        request.arguments.insert(request.arguments.end(), {L"--exec", L"python3", L"-c", utf16(kSshTerminalScript)});
        const auto& ssh = context.connection.ssh;
        auto value = [&](const std::string& ref, const std::string& literal) {
            return ref.empty() ? literal : context.credentials.lookup(ref);
        };
        std::uint16_t port = ssh.port;
        if (!ssh.port_ref.empty() && !parse_port(context.credentials.lookup(ssh.port_ref), &port)) {
            result.warning = "Invalid SSH port reference."; return result;
        }
        auto payload = Json::object();
        payload["host"] = Json::string(value(ssh.host_ref, ssh.host));
        payload["user"] = Json::string(value(ssh.username_ref, ssh.username));
        payload["port"] = Json::number(port);
        payload["hostKey"] = Json::string(ssh.host_key);
        payload["key"] = Json::string(context.credentials.lookup(ssh.private_key_ref));
        payload["passphrase"] = Json::string(context.credentials.lookup(ssh.key_passphrase_ref));
        payload["command"] = Json::string(context.sql);
        payload["timeout"] = Json::number(context.timeout_ms / 1000);
        payload["maxBytes"] = Json::number((std::min)(context.connection.result_policy.max_bytes, 1024u * 1024u));
        request.stdin_text = payload.dump();
        request.timeout_ms = context.timeout_ms + 10000;
        auto output = runner_(request);
        SecureZeroMemory(request.stdin_text.data(), request.stdin_text.size());
        std::string error;
        const auto response = Json::parse(output.standard_output, &error);
        if (!output.spawned || output.timed_out || !error.empty() || !response.is_object() || !response.at("error").is_null()) {
            result.warning = "SSH terminal execution failed. The selected WSL distribution needs Python 3 and OpenSSH.";
            return result;
        }
        if (response.at("timedOut").as_bool(false)) {
            result.warning = "SSH command timed out."; return result;
        }
        auto columns = Json::array();
        for (const auto* name : {"exitCode", "stdout", "stderr"}) columns.push(Json::string(name));
        auto row = Json::array();
        row.push(response.at("exitCode")); row.push(response.at("stdout")); row.push(response.at("stderr"));
        auto rows = Json::array(); rows.push(std::move(row));
        result.columns_json = columns.dump(); result.rows_json = rows.dump(); result.row_count = 1;
        result.truncated = response.at("truncated").as_bool(false);
        result.ok = true; // The command's exit status is returned separately, including nonzero.
        return result;
    }
    {
        std::lock_guard lock(mutex_);
        cancelled_[context.operation_id] = false;
    }
    auto forget = [&]() {
        std::lock_guard lock(mutex_);
        cancelled_.erase(context.operation_id);
    };

    const std::wstring client = discover_ssh_client();
    if (client.empty()) {
        result.warning = "The Windows OpenSSH client was not found; install it to use brokered connections.";
        forget();
        return result;
    }
    if (!ensure_dir(work_dir_)) {
        result.warning = "Scylla could not prepare its private execution directory.";
        forget();
        return result;
    }

    const std::wstring key_path = join_path(work_dir_, utf16(context.operation_id) + L".key");
    const std::wstring hosts_path = join_path(work_dir_, utf16(context.operation_id) + L".known_hosts");
    const SshCommandPlan plan = plan_ssh_mysql_command(context, hosts_path, key_path);
    if (!plan.ok) {
        result.warning = plan.error;
        forget();
        return result;
    }

    // Both files hold material the agent must never read, so both are owner-only and shredded.
    bool wrote = write_private_file(key_path, plan.private_key_pem);
    wrote = wrote && write_private_file(hosts_path, plan.known_hosts_line + "\n");
    if (!wrote) {
        shred_file(key_path);
        shred_file(hosts_path);
        result.warning = "Scylla could not stage the SSH credentials for this operation.";
        forget();
        return result;
    }

    // One teardown for everything this operation stages, so no early return, exception, or failure
    // path can leave key material on disk or a passphrase in memory.
    struct OperationCleanup {
        std::wstring key_path;
        std::wstring hosts_path;
        std::string passphrase;
        ~OperationCleanup() {
            shred_file(key_path);
            shred_file(hosts_path);
            if (!passphrase.empty()) SecureZeroMemory(passphrase.data(), passphrase.size());
        }
    } cleanup{key_path, hosts_path, plan.key_passphrase};

    ProcessRunRequest request;
    request.executable = client;
    request.arguments = plan.arguments;
    request.stdin_text = plan.stdin_text;
    request.timeout_ms = context.timeout_ms;
    if (plan.needs_askpass) request.environment = askpass_environment(plan.key_passphrase);

    const auto started = std::chrono::steady_clock::now();
    ProcessRunResult run = runner_(request);
    wipe_environment(request.environment);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (!request.stdin_text.empty()) {
        SecureZeroMemory(request.stdin_text.data(), request.stdin_text.size());
    }

    result.elapsed_ms = static_cast<std::uint64_t>(elapsed.count());
    if (!run.spawned) {
        result.warning = "Scylla could not start the SSH client.";
        forget();
        return result;
    }
    if (run.timed_out) {
        result.warning = "The query exceeded the connection timeout and was cancelled.";
        forget();
        return result;
    }
    if (run.exit_code != 0) {
        append_connection_log(work_dir_, "ssh/mysql failed", run.standard_error, run.exit_code);
        result.warning = sanitize_execution_error(run.standard_error);
        if (result.warning.find("connection log") != std::string::npos) {
            result.warning += " (" + utf8(join_path(work_dir_, L"connection.log")) + ")";
        }
        forget();
        return result;
    }
    result.ok = parse_mysql_batch_output(run.standard_output, &result);
    if (!result.ok) result.warning = "The query result could not be read.";
    forget();
    return result;
}

void SshQueryExecutor::close(std::string_view operation_id) {
    std::lock_guard lock(mutex_);
    const auto found = cancelled_.find(std::string(operation_id));
    if (found != cancelled_.end()) found->second = true;
}

void SshQueryExecutor::close_all() {
    std::lock_guard lock(mutex_);
    for (auto& [id, flag] : cancelled_) flag = true;
}

}  // namespace scyllagpt
