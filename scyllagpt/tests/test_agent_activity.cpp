#include "scyllagpt/acp_client.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace scyllagpt;
static void check(bool ok, const char *why) {
  if (!ok) {
    std::cerr << why << '\n';
    std::exit(1);
  }
}
int main() {
  AgentActivity a;
  a.begin();
  auto edit = Json::parse(
      R"({"item":{"id":"e","type":"fileChange","status":"inProgress","changes":[{"path":"src/a.cpp"}]}})");
  a.codex("item/started", edit);
  check(a.changed_files.empty(), "Proposed edits are not changed files");
  edit["item"]["status"] = Json::string("failed");
  a.codex("item/completed", edit);
  check(a.changed_files.empty() && a.work.empty(),
        "Failed edits must clear work without claiming changes");
  edit["item"]["status"] = Json::string("completed");
  a.codex("item/completed", edit);
  a.codex("item/completed", edit);
  check(a.changed_files.size() == 1, "Completed file paths deduplicate");
  auto collab = Json::parse(
      R"({"item":{"id":"c","type":"collabAgentToolCall","tool":"spawnAgent","agentsStates":{"a":{"status":"running"},"b":{"status":"pendingInit"}}}})");
  a.codex("item/started", collab);
  check(a.agents.size() == 2, "Concurrent subagents tracked independently");
  collab["item"]["agentsStates"]["a"]["status"] = Json::string("completed");
  a.codex("item/completed", collab);
  check(a.agents.size() == 1, "Finishing one subagent preserves the other");
  a.finish("Interrupted");
  check(!a.busy && a.work.empty() && a.agents.empty() &&
            a.changed_files.size() == 1,
        "Interrupt clears live status and retains changed files");
  a.codex("item/started", edit);
  check(a.work.empty(), "Late events cannot revive a finished activity");
  a.begin();
  check(a.changed_files.empty(), "A new turn clears the previous file summary");

  std::vector<Json> sent;
  AcpClient c([&](const std::string &line) {
    sent.push_back(Json::parse(line));
    return true;
  });
  auto reply = [&](Json result) {
    Json m = Json::object();
    m["jsonrpc"] = Json::string("2.0");
    m["id"] = sent.back().at("id");
    m["result"] = std::move(result);
    c.receive(m.dump());
  };
  check(c.initialize(), "Initialize writes a request");
  check(!sent.back()
             .at("params")
             .at("clientCapabilities")
             .at("terminal")
             .as_bool(),
        "No terminal capability advertised");
  reply(Json::parse(
      R"({"protocolVersion":1,"agentCapabilities":{"loadSession":true},"authMethods":[{"id":"cursor_login"}]})"));
  check(c.state == AcpClient::State::Ready, "Initialize negotiates version");
  check(!c.authenticate("invented"), "Authentication must be advertised");
  check(c.authenticate("cursor_login"),
        "Advertised authentication can be requested");
  reply(Json::object());
  check(c.open("C:/fixture"), "Open session");
  reply(Json::parse(
      R"({"sessionId":"s1","models":{"currentModelId":"advertised-model"},"modes":{"currentModeId":"ask"}})"));
  check(c.models.at("currentModelId").as_string() == "advertised-model",
        "Models come from server");
  check(!c.set_model("invented") && !c.set_mode("agent"),
        "Unadvertised models and modes cannot be selected");
  c.modes = Json::parse(
      R"({"currentModeId":"ask","availableModes":[{"id":"ask"},{"id":"plan"}]})");
  check(c.set_mode("plan"), "Advertised mode can be selected");
  check(c.modes.at("currentModeId").as_string() == "ask",
        "Mode does not change before acknowledgement");
  reply(Json::object());
  check(c.modes.at("currentModeId").as_string() == "plan",
        "Acknowledgement applies mode");
  check(c.prompt("hello"), "Prompt accepted once idle");
  const auto prompt_id = sent.back().at("id");
  check(!c.prompt("overlap"), "Overlapping prompt rejected");
  c.receive(
      R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"other","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"wrong"}}}})");
  check(c.text.empty(), "Other session does not leak into transcript");
  c.receive(
      R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"hello"}}}})");
  c.receive(
      R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call","toolCallId":"t","title":"Edit","kind":"edit","status":"in_progress","content":[{"type":"diff","path":"a.md"}]}}})");
  check(c.activity.changed_files.empty(), "ACP proposed diff is not applied");
  c.receive(
      R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":{"sessionUpdate":"tool_call_update","toolCallId":"t","status":"completed"}}})");
  check(c.activity.changed_files.count("a.md") == 1,
        "Partial ACP update preserves original diff");
  c.receive(
      R"({"jsonrpc":"2.0","id":"permission","method":"session/request_permission","params":{"sessionId":"s1","options":[{"optionId":"yes","kind":"allow_always"}]}})");
  check(sent.back().at("result").at("outcome").at("outcome").as_string() ==
            "cancelled",
        "Permissions fail closed");
  check(c.cancel() && c.state == AcpClient::State::Cancelling,
        "Cancellation waits for confirmation");
  check(!sent.back().has("id"), "Cancel is a notification");
  Json done = Json::object();
  done["jsonrpc"] = Json::string("2.0");
  done["id"] = prompt_id;
  done["result"]["stopReason"] = Json::string("cancelled");
  c.receive(done.dump());
  check(c.state == AcpClient::State::Idle && !c.activity.busy &&
            c.text == "hello",
        "Cancel keeps partial output and clears busy");
  c.disconnected();
  check(c.session_id.empty() && c.state == AcpClient::State::Offline,
        "Disconnect invalidates session");
  AcpClient broken([](const std::string &) { return false; });
  check(!broken.initialize() && broken.state == AcpClient::State::Failed,
        "Write failure cannot leave client initializing");
  AcpClient incompatible([](const std::string &) { return true; });
  incompatible.initialize();
  incompatible.receive(
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":99}})");
  check(incompatible.state == AcpClient::State::Failed,
        "Unsupported protocol version rejected");
  std::cout << "Agent activity and ACP protocol checks passed\n";
}
