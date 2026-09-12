#include "scyllagpt/acp_client.h"

namespace scyllagpt {

bool AcpClient::send(Json message) {
  message["jsonrpc"] = Json::string("2.0");
  if (write_(message.dump() + "\n"))
    return true;
  fail("ACP transport write failed");
  return false;
}

void AcpClient::fail(const std::string &message) {
  error = message;
  state = State::Failed;
  pending_.clear();
  activity.finish("Failed");
}

bool AcpClient::request(const std::string &method, Json params) {
  Json message = Json::object();
  const auto id = next_id_++;
  message["id"] = Json::number(id);
  message["method"] = Json::string(method);
  message["params"] = std::move(params);
  pending_[id] = method;
  return send(std::move(message));
}

bool AcpClient::initialize() {
  if (state != State::Offline)
    return false;
  state = State::Initializing;
  Json p = Json::object();
  p["protocolVersion"] = Json::number(1);
  p["clientInfo"] =
        Json::parse(R"({"name":"scylla","version":"0.1.0"})");
  p["clientCapabilities"] = Json::parse(
      R"({"fs":{"readTextFile":false,"writeTextFile":false},"terminal":false})");
  return request("initialize", std::move(p));
}

bool AcpClient::authenticate(const std::string &method_id) {
  if (state != State::Ready)
    return false;
  bool advertised = false;
  for (const auto &method : auth_methods.array_items())
    if (method.at("id").as_string() == method_id)
      advertised = true;
  if (!advertised)
    return false;
  state = State::Authenticating;
  Json p = Json::object();
  p["methodId"] = Json::string(method_id);
  return request("authenticate", std::move(p));
}

bool AcpClient::set_model(const std::string &model_id) {
  if (state != State::Idle)
    return false;
  bool advertised = false;
  for (const auto &model : models.at("availableModels").array_items())
    if (model.at("modelId").as_string() == model_id)
      advertised = true;
  if (!advertised)
    return false;
  requested_model_ = model_id;
  state = State::Configuring;
  Json p = Json::object();
  p["sessionId"] = Json::string(session_id);
  p["modelId"] = Json::string(model_id);
  return request("session/set_model", std::move(p));
}

bool AcpClient::set_mode(const std::string &mode_id) {
  if (state != State::Idle)
    return false;
  bool advertised = false;
  for (const auto &mode : modes.at("availableModes").array_items())
    if (mode.at("id").as_string() == mode_id)
      advertised = true;
  if (!advertised)
    return false;
  requested_mode_ = mode_id;
  state = State::Configuring;
  Json p = Json::object();
  p["sessionId"] = Json::string(session_id);
  p["modeId"] = Json::string(mode_id);
  return request("session/set_mode", std::move(p));
}

bool AcpClient::open(const std::string &cwd, const std::string &resume_id) {
  if (state != State::Ready && state != State::Idle)
    return false;
  if (cwd.empty())
    return false;
  if (!resume_id.empty() && !capabilities.at("loadSession").as_bool())
    return false;
  state = State::Opening;
  session_id.clear();
  text.clear();
  activity = {};
  loading_id_ = resume_id;
  Json p = Json::object();
  p["cwd"] = Json::string(cwd);
  p["mcpServers"] = Json::array();
  if (!resume_id.empty())
    p["sessionId"] = Json::string(resume_id);
  return request(resume_id.empty() ? "session/new" : "session/load",
                 std::move(p));
}

bool AcpClient::prompt(const std::string &content) {
  if (state != State::Idle || session_id.empty())
    return false;
  state = State::Prompting;
  text.clear();
  tools_.clear();
  activity.begin();
  Json p = Json::object();
  p["sessionId"] = Json::string(session_id);
  Json block = Json::object();
  block["type"] = Json::string("text");
  block["text"] = Json::string(content);
  p["prompt"] = Json::array();
  p["prompt"].push(std::move(block));
  return request("session/prompt", std::move(p));
}

bool AcpClient::cancel() {
  if (state != State::Prompting)
    return false;
  state = State::Cancelling;
  activity.phase = "Cancelling";
  Json m = Json::object();
  m["method"] = Json::string("session/cancel");
  m["params"]["sessionId"] = Json::string(session_id);
  return send(std::move(m));
}

void AcpClient::server_request(const Json &m) {
  Json reply = Json::object();
  reply["id"] = m.at("id");
  const auto method = m.at("method").as_string();
  // No blanket allow, including while cancelling or after disconnect.
  if (method == "session/request_permission") {
    reply["result"]["outcome"]["outcome"] = Json::string("cancelled");
  } else if (method == "cursor/ask_question" ||
             method == "cursor/create_plan") {
    reply["result"]["outcome"]["outcome"] = Json::string("cancelled");
  } else {
    reply["error"]["code"] = Json::number(-32601);
    reply["error"]["message"] = Json::string("Client capability unavailable");
  }
  send(std::move(reply));
}

void AcpClient::update(const Json &p) {
  if (p.at("sessionId").as_string() != session_id ||
      (state != State::Prompting && state != State::Cancelling))
    return;
  const auto &u = p.at("update");
  const auto kind = u.at("sessionUpdate").as_string();
  if (kind == "agent_message_chunk") {
    if (u.at("content").at("type").as_string() == "text")
      text += u.at("content").at("text").as_string();
  } else if (kind == "plan") {
    for (auto i = activity.work.begin(); i != activity.work.end();) {
      if (i->first.starts_with("plan:"))
        i = activity.work.erase(i);
      else
        ++i;
    }
    int n = 0;
    for (const auto &e : u.at("entries").array_items())
      if (e.at("status").as_string() == "in_progress")
        activity.work["plan:" + std::to_string(n++)] =
            AgentActivity::label(e.at("content").as_string());
  } else if (kind == "tool_call" || kind == "tool_call_update") {
    const auto id = u.at("toolCallId").as_string();
    if (id.empty())
      return;
    auto &tool = tools_[id];
    for (const auto &[k, v] : u.object_items())
      tool[k] = v;
    const auto status = tool.at("status").as_string();
    if (status == "completed" || status == "failed")
      activity.work.erase(id);
    else
      activity.work[id] =
          AgentActivity::label(tool.at("title").as_string("Working"));
    // Locations can be reads; only completed edit calls containing diff content
    // constitute provider evidence of changed files.
    if (status == "completed" && tool.at("kind").as_string() == "edit") {
      for (const auto &c : tool.at("content").array_items()) {
        if (c.at("type").as_string() == "diff" &&
            !c.at("path").as_string().empty())
          activity.changed_files.insert(c.at("path").as_string());
      }
    }
  }
}

void AcpClient::receive(const std::string &line) {
  if (state == State::Offline || state == State::Failed)
    return;
  std::string parse_error;
  const auto m = Json::parse(line, &parse_error);
  if (!parse_error.empty() || !m.is_object() ||
      m.at("jsonrpc").as_string() != "2.0") {
    fail("Invalid ACP message");
    return;
  }
  if (m.has("method")) {
    if (m.has("id"))
      server_request(m);
    else if (m.at("method").as_string() == "session/update")
      update(m.at("params"));
    return;
  }
  if (!m.at("id").is_number())
    return;
  const auto i = pending_.find(m.at("id").as_int());
  if (i == pending_.end())
    return;
  const auto method = i->second;
  pending_.erase(i);
  if (m.has("error")) {
    fail("ACP request failed: " + method);
    return;
  }
  if (!m.has("result")) {
    fail("ACP response missing result");
    return;
  }
  const auto &r = m.at("result");
  if (method == "initialize") {
    if (r.at("protocolVersion").as_int() != 1) {
      fail("Unsupported ACP protocol version");
      return;
    }
    capabilities = r.at("agentCapabilities");
    auth_methods = r.at("authMethods");
    state = State::Ready;
  } else if (method == "authenticate") {
    state = State::Ready;
  } else if (method == "session/set_model") {
    models["currentModelId"] = Json::string(requested_model_);
    state = State::Idle;
  } else if (method == "session/set_mode") {
    modes["currentModeId"] = Json::string(requested_mode_);
    state = State::Idle;
  } else if (method == "session/new" || method == "session/load") {
    session_id =
        method == "session/load" ? loading_id_ : r.at("sessionId").as_string();
    if (session_id.empty()) {
      fail("ACP response missing session identity");
      return;
    }
    models = r.at("models");
    modes = r.at("modes");
    config_options = r.at("configOptions");
    state = State::Idle;
  } else if (method == "session/prompt") {
    const auto reason = r.at("stopReason").as_string();
    activity.finish(reason == "cancelled"  ? "Interrupted"
                    : reason == "end_turn" ? "Completed"
                                           : "Stopped: " + reason);
    state = State::Idle;
  }
}

void AcpClient::disconnected() {
  pending_.clear();
  session_id.clear();
  state = State::Offline;
  activity.finish("Disconnected");
}

} // namespace scyllagpt
