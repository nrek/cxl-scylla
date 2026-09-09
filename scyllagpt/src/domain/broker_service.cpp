#include "scyllagpt/broker_service.h"

#include "scyllagpt/json.h"
#include "scyllagpt/utf.h"

#include <vector>

#include <bcrypt.h>
#include <sddl.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace scyllagpt {
namespace {

std::string refusal_line(const char* status, const char* message) {
    Json root = Json::object();
    root["v"] = Json::number(1);
    root["status"] = Json::string(status);
    root["message"] = Json::string(message);
    root["rowCount"] = Json::number(0);
    root["elapsedMs"] = Json::number(0);
    root["truncated"] = Json::boolean(false);
    return root.dump();
}

}  // namespace

BrokerPipeService::~BrokerPipeService() { stop(); }

std::string BrokerPipeService::make_secret_token() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return {};
    }
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(sizeof(bytes) * 2);
    for (unsigned char byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

bool BrokerPipeService::start(BrokerQueryHandler handler, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (running_.load()) {
        if (error) error->clear();
        return true;
    }
    if (!handler) return fail("a broker handler is required");

    // One token per launch, used both as the pipe suffix (so a stale pipe from a previous run is
    // never reused) and as the credential the helper must present.
    token_ = make_secret_token();
    if (token_.empty()) return fail("the broker could not generate a session token");
    pipe_name_ = utf16(std::string(kBrokerPipePrefix) + token_);

    cancel_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!cancel_event_) return fail("the broker could not create its cancellation event");

    handler_ = std::move(handler);
    stopping_.store(false);
    running_.store(true);
    acceptor_ = std::thread([this] { accept_loop(); });
    if (error) error->clear();
    return true;
}

void BrokerPipeService::stop() {
    if (!running_.exchange(false)) {
        if (cancel_event_) {
            CloseHandle(cancel_event_);
            cancel_event_ = nullptr;
        }
        return;
    }
    stopping_.store(true);
    if (cancel_event_) SetEvent(cancel_event_);
    // Unblock a ConnectNamedPipe that is waiting with no client in sight.
    if (!pipe_name_.empty()) {
        HANDLE nudge = CreateFileW(pipe_name_.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (nudge != INVALID_HANDLE_VALUE) CloseHandle(nudge);
    }
    if (acceptor_.joinable()) acceptor_.join();
    if (cancel_event_) {
        CloseHandle(cancel_event_);
        cancel_event_ = nullptr;
    }
    handler_ = nullptr;
    pipe_name_.clear();
    if (!token_.empty()) {
        SecureZeroMemory(token_.data(), token_.size());
        token_.clear();
    }
}

void BrokerPipeService::accept_loop() {
    // Only the current user may open this pipe, and never from another machine.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)", SDDL_REVISION_1,
                                                             &descriptor, nullptr)) {
        return;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;

    // Blocking handle: serve_client uses ordinary synchronous ReadFile/WriteFile. Shutdown does not
    // need overlapped I/O because stop() connects to its own pipe, which releases ConnectNamedPipe.
    // One client is served at a time, so brokered queries are naturally serialized.
    while (!stopping_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 5000, &attributes);
        if (pipe == INVALID_HANDLE_VALUE) break;

        bool connected = ConnectNamedPipe(pipe, nullptr) != 0;
        if (!connected && GetLastError() == ERROR_PIPE_CONNECTED) connected = true;
        if (!connected || stopping_.load()) {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            if (stopping_.load()) break;
            continue;
        }
        serve_client(pipe);
    }
    LocalFree(descriptor);
}

void BrokerPipeService::serve_client(HANDLE pipe) {
    auto reply = [&](const std::string& line) {
        std::string body = line;
        body.push_back('\n');
        DWORD written = 0;
        WriteFile(pipe, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    };

    std::string request;
    char buffer[8192];
    DWORD read = 0;
    while (request.find('\n') == std::string::npos) {
        if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        request.append(buffer, read);
        if (request.size() > kBrokerMaxMessageBytes) {
            reply(refusal_line("invalid_request", "The broker request was too large."));
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            return;
        }
    }
    const auto newline = request.find('\n');
    if (newline != std::string::npos) request.resize(newline);

    QueryToolCall call;
    std::string token;
    std::string decode_error;
    if (!decode_broker_request(request, &call, &token, &decode_error)) {
        reply(refusal_line("invalid_request", "The broker request could not be read."));
    } else if (token.empty() || token != token_) {
        // Wrong or missing token: say nothing useful about why.
        reply(refusal_line("invalid_request", "The broker request was not authorized."));
    } else {
        std::string response;
        if (handler_) response = handler_(call);
        reply(response.empty() ? refusal_line("execution_failed", "The brokered query did not complete.")
                               : response);
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

}  // namespace scyllagpt
