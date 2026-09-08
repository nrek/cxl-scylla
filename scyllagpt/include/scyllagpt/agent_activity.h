#pragma once

#include "scyllagpt/json.h"
#include <chrono>
#include <map>
#include <set>
#include <string>

namespace scyllagpt {

// A current-turn snapshot, never a transcript or an inference from assistant
// prose.
struct AgentActivity {
  bool visible = false;
  bool busy = false;
  std::string phase;
  std::map<std::string, std::string> work;
  std::map<std::string, std::string> agents;
  std::set<std::string> changed_files;
  std::chrono::steady_clock::time_point started;
  long long elapsed = 0;

  void begin() {
    *this = {};
    visible = busy = true;
    phase = "Working";
    started = std::chrono::steady_clock::now();
  }
  void finish(const std::string &status) {
    if (!visible)
      return;
    elapsed = seconds();
    busy = false;
    phase = status;
    work.clear();
    agents.clear();
  }
  long long seconds() const {
    return busy ? std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - started)
                      .count()
                : elapsed;
  }
  static std::string label(std::string s) {
    for (char &c : s)
      if (static_cast<unsigned char>(c) < 32)
        c = ' ';
    return s.size() > 180 ? s.substr(0, 180) + "..." : s;
  }
  void codex(const std::string &method, const Json &p) {
    if (!busy)
      return;
    if (method == "turn/plan/updated") {
      for (auto i = work.begin(); i != work.end();) {
        if (i->first.starts_with("plan:"))
          i = work.erase(i);
        else
          ++i;
      }
      int n = 0;
      for (const auto &step : p.at("plan").array_items()) {
        if (step.at("status").as_string() == "inProgress")
          work["plan:" + std::to_string(n++)] =
              label(step.at("step").as_string());
      }
      return;
    }
    if (method != "item/started" && method != "item/completed")
      return;
    const auto &item = p.at("item");
    const auto id = item.at("id").as_string();
    if (id.empty())
      return;
    const auto type = item.at("type").as_string();
    const bool done = method == "item/completed";
    if (done)
      work.erase(id);
    else if (type == "commandExecution")
      work[id] = "Running command";
    else if (type == "fileChange")
      work[id] = "Editing files";
    else if (type == "mcpToolCall" || type == "dynamicToolCall")
      work[id] = "Tool: " + label(item.at("tool").as_string("working"));
    else if (type == "webSearch")
      work[id] = "Searching the web";
    else if (type == "reasoning")
      work[id] = "Thinking";
    else if (type == "agentMessage")
      work[id] = "Writing response";
    if (type == "collabAgentToolCall") {
      for (const auto &[agent, state] :
           item.at("agentsStates").object_items()) {
        const auto status = state.at("status").as_string();
        if (status == "running" || status == "pendingInit")
          agents[agent] = status;
        else
          agents.erase(agent);
      }
      if (!done)
        work[id] = "Sub-agent: " + label(item.at("tool").as_string("working"));
    }
    // Approval/start only means proposed edits. Only successful completion
    // counts.
    if (done && type == "fileChange" &&
        item.at("status").as_string() == "completed") {
      for (const auto &change : item.at("changes").array_items()) {
        const auto path = change.at("path").as_string();
        if (!path.empty())
          changed_files.insert(path);
      }
    }
  }
  std::string summary() const {
    if (!visible)
      return {};
    std::string s = phase + "  |  " + std::to_string(seconds()) + "s";
    if (!agents.empty())
      s += "  |  " + std::to_string(agents.size()) + " active sub-agent(s)";
    for (const auto &[id, title] : work)
      s += "\r\n" + title;
    if (!changed_files.empty()) {
      s += "\r\nChanged files (provider reported): " +
           std::to_string(changed_files.size());
      for (const auto &file : changed_files)
        s += "\r\n  " + label(file);
    }
    return s;
  }
};

} // namespace scyllagpt
