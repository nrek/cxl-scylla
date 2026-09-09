#pragma once

// Workbench side of the broker: a per-launch local named pipe that the secret-free MCP helper
// talks to. This process owns the Keyring; the helper never does.
//
// Threading contract:
//   - Each accepted client is served on its own short-lived thread.
//   - `handler` therefore runs OFF the UI thread and must not touch the Keyring directly. It is
//     expected to marshal ConnectionBroker::prepare to the UI thread and run finish() itself.

#include "scyllagpt/broker_protocol.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

// Returns a response line (see encode_broker_response). Runs on a worker thread.
using BrokerQueryHandler = std::function<std::string(const QueryToolCall&)>;

class BrokerPipeService {
public:
    BrokerPipeService() = default;
    ~BrokerPipeService();

    BrokerPipeService(const BrokerPipeService&) = delete;
    BrokerPipeService& operator=(const BrokerPipeService&) = delete;

    // Mints a fresh pipe name and token, then begins accepting. Safe to call when already running
    // (no-op). `error` is set on failure.
    bool start(BrokerQueryHandler handler, std::string* error);
    void stop();

    bool running() const { return running_.load(); }
    // Empty until start() succeeds.
    const std::wstring& pipe_name() const { return pipe_name_; }
    const std::string& token() const { return token_; }

    // Random 32-hex-character value used for both the pipe suffix and the shared token.
    static std::string make_secret_token();

private:
    void accept_loop();
    void serve_client(HANDLE pipe);

    BrokerQueryHandler handler_;
    std::wstring pipe_name_;
    std::string token_;
    std::atomic_bool running_{false};
    std::atomic_bool stopping_{false};
    std::thread acceptor_;
    HANDLE cancel_event_ = nullptr;
};

}  // namespace scyllagpt
