#pragma once

#include "scyllagpt/agent_activity.h"
#include <functional>

namespace scyllagpt {

// Transport-independent ACP state machine. Runtime activation is gated
// separately; client capability declarations and permission replies are NOT an
// OS sandbox.
class AcpClient {
public:
  enum class State {
    Offline,
    Initializing,
    Ready,
    Authenticating,
    Opening,
    Idle,
    Configuring,
    Prompting,
    Cancelling,
    Failed
  };
  explicit AcpClient(std::function<bool(const std::string &)> write)
      : write_(std::move(write)) {}
  State state = State::Offline;
  Json capabilities;
  Json auth_methods;
  Json models;
  Json modes;
  Json config_options;
  std::string session_id, text, error;
  AgentActivity activity;

  bool initialize();
  bool authenticate(const std::string &method_id);
  bool open(const std::string &cwd, const std::string &resume_id = {});
  bool set_model(const std::string &model_id);
  bool set_mode(const std::string &mode_id);
  bool prompt(const std::string &content);
  bool cancel();
  void receive(const std::string &line);
  void disconnected();

private:
  std::function<bool(const std::string &)> write_;
  std::int64_t next_id_ = 1;
  std::map<std::int64_t, std::string> pending_;
  std::map<std::string, Json> tools_;
  std::string loading_id_;
  std::string requested_model_, requested_mode_;
  bool send(Json message);
  bool request(const std::string &method, Json params);
  void fail(const std::string &message);
  void server_request(const Json &message);
  void update(const Json &params);
};

} // namespace scyllagpt
