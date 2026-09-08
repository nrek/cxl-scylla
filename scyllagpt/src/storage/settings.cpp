#include "scyllagpt/settings.h"

#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <sstream>

namespace scyllagpt {

PolicyMode parse_policy_mode(std::string_view raw, PolicyMode fallback) {
    if (raw == "allow") {
        return PolicyMode::Allow;
    }
    if (raw == "ask") {
        return PolicyMode::Ask;
    }
    if (raw == "block") {
        return PolicyMode::Block;
    }
    return fallback;
}

const char* policy_mode_string(PolicyMode mode) {
    switch (mode) {
        case PolicyMode::Allow:
            return "allow";
        case PolicyMode::Ask:
            return "ask";
        case PolicyMode::Block:
        default:
            return "block";
    }
}

const wchar_t* policy_mode_label(PolicyMode mode) {
    switch (mode) {
        case PolicyMode::Allow:
            return L"Allowed";
        case PolicyMode::Ask:
            return L"Ask each time";
        case PolicyMode::Block:
        default:
            return L"Blocked";
    }
}

ExecutionPolicy strict_execution_policy() {
    return ExecutionPolicy{};
}

bool execution_policy_is_strict(const ExecutionPolicy& policy) {
    const ExecutionPolicy strict = strict_execution_policy();
    return policy.human_terminals == strict.human_terminals &&
           policy.agent_terminals == strict.agent_terminals &&
           policy.approved_recipes == strict.approved_recipes;
}

Settings load_settings(const std::wstring& path) {
    Settings s;
    s.drafts = Json::object();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return s;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return s;
    }
    std::string raw(static_cast<std::size_t>(sz.QuadPart), 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    std::string err;
    Json j = Json::parse(raw, &err);
    if (!err.empty() || !j.is_object()) {
        return s;
    }
    s.codex_path = utf16(j.at("codex_path").as_string());
    s.enter_sends = j.at("enter_sends").is_null() ? true : j.at("enter_sends").as_bool(true);
    s.last_thread_id = j.at("last_thread_id").as_string("");
    s.restore_chat_on_start = j.at("restore_chat_on_start").as_bool(true);
    s.project_folder = utf16(j.at("project_folder").as_string());
    s.files_w = static_cast<int>(j.at("files_w").as_int(220));
    const auto knowledge_h = j.at("knowledge_h").as_int(0);
    s.knowledge_h = knowledge_h > 0 && knowledge_h <= 10000 ? static_cast<int>(knowledge_h) : 0;
    s.agent_w = static_cast<int>(j.at("agent_w").as_int(400));
    s.history_w = static_cast<int>(j.at("history_w").as_int(232));
    s.files_mode = static_cast<int>(j.at("files_mode").as_int(0));
    s.history_mode = static_cast<int>(j.at("history_mode").as_int(0));
    s.focus_editor = j.at("focus_editor").as_bool(false);
    s.default_provider = j.at("default_provider").as_string("openai");
    if (s.default_provider != "claude") {
        s.default_provider = "openai";
    }
    s.selected_model = j.at("selected_model").as_string("");
    s.word_wrap = j.at("word_wrap").as_bool(false);
    s.show_whitespace = j.at("show_whitespace").as_bool(false);
    s.terminal_h = static_cast<int>(j.at("terminal_h").as_int(220));
    if (s.terminal_h < 140) {
        s.terminal_h = 220;
    }
    s.terminal_visible = j.at("terminal_visible").as_bool(false);
    s.agent_terminal_policy = j.at("agent_terminal_policy").as_string("ask");
    if (s.agent_terminal_policy != "allow" && s.agent_terminal_policy != "block") {
        s.agent_terminal_policy = "ask";
    }
    s.default_terminal_profile_id = j.at("default_terminal_profile_id").as_string("");
    s.panel_surface = j.at("panel_surface").as_string("terminal");
    if (s.panel_surface != "problems" && s.panel_surface != "output" && s.panel_surface != "ports") {
        s.panel_surface = "terminal";
    }
    // Missing / unparsable policy falls back to Strict rather than the loosest option.
    const ExecutionPolicy strict = strict_execution_policy();
    const Json& pol = j.at("execution_policy");
    if (pol.is_object()) {
        s.execution_policy.human_terminals =
            parse_policy_mode(pol.at("human_terminals").as_string(""), strict.human_terminals);
        s.execution_policy.agent_terminals =
            parse_policy_mode(pol.at("agent_terminals").as_string(""), strict.agent_terminals);
        s.execution_policy.approved_recipes =
            parse_policy_mode(pol.at("approved_recipes").as_string(""), strict.approved_recipes);
    } else {
        s.execution_policy = strict;
    }
    // Agent terminals never receive protected values today (Strict resolve drops them), so a
    // stored "allow" is not honoured — clamp it instead of implying a capability that is absent.
    if (s.execution_policy.agent_terminals == PolicyMode::Allow) {
        s.execution_policy.agent_terminals = PolicyMode::Block;
    }
    s.terminal_profile_enabled.clear();
    const Json& en = j.at("terminal_profile_enabled");
    if (en.is_object()) {
        for (const auto& kv : en.object_items()) {
            s.terminal_profile_enabled[kv.first] = kv.second.as_bool(true);
        }
    }
    if (s.files_w < 180) {
        s.files_w = 220;
    }
    if (s.agent_w < 320) {
        s.agent_w = 400;
    }
    if (s.history_w < 180) {
        s.history_w = 232;
    }
    if (j.at("drafts").is_object()) {
        s.drafts = j.at("drafts");
    }
    const Json& w = j.at("window");
    if (w.is_object()) {
        s.window.x = static_cast<int>(w.at("x").as_int(s.window.x));
        s.window.y = static_cast<int>(w.at("y").as_int(s.window.y));
        s.window.w = static_cast<int>(w.at("w").as_int(s.window.w));
        s.window.h = static_cast<int>(w.at("h").as_int(s.window.h));
        s.window.maximized = w.at("maximized").as_bool(false);
    }
    return s;
}

bool save_settings(const std::wstring& path, const Settings& s) {
    Json j = Json::object();
    j["codex_path"] = Json::string(utf8(s.codex_path));
    j["enter_sends"] = Json::boolean(s.enter_sends);
    j["last_thread_id"] = Json::string(s.last_thread_id);
    j["restore_chat_on_start"] = Json::boolean(s.restore_chat_on_start);
    j["project_folder"] = Json::string(utf8(s.project_folder));
    j["files_w"] = Json::number(s.files_w);
    j["knowledge_h"] = Json::number(s.knowledge_h);
    j["agent_w"] = Json::number(s.agent_w);
    j["history_w"] = Json::number(s.history_w);
    j["files_mode"] = Json::number(s.files_mode);
    j["history_mode"] = Json::number(s.history_mode);
    j["focus_editor"] = Json::boolean(s.focus_editor);
    j["default_provider"] = Json::string(s.default_provider);
    j["selected_model"] = Json::string(s.selected_model);
    j["word_wrap"] = Json::boolean(s.word_wrap);
    j["show_whitespace"] = Json::boolean(s.show_whitespace);
    j["terminal_h"] = Json::number(s.terminal_h);
    j["terminal_visible"] = Json::boolean(s.terminal_visible);
    j["agent_terminal_policy"] = Json::string(s.agent_terminal_policy);
    j["default_terminal_profile_id"] = Json::string(s.default_terminal_profile_id);
    j["panel_surface"] = Json::string(s.panel_surface);
    Json pol = Json::object();
    pol["human_terminals"] = Json::string(policy_mode_string(s.execution_policy.human_terminals));
    pol["agent_terminals"] = Json::string(policy_mode_string(s.execution_policy.agent_terminals));
    pol["approved_recipes"] = Json::string(policy_mode_string(s.execution_policy.approved_recipes));
    j["execution_policy"] = std::move(pol);
    Json en = Json::object();
    for (const auto& kv : s.terminal_profile_enabled) {
        en[kv.first] = Json::boolean(kv.second);
    }
    j["terminal_profile_enabled"] = std::move(en);
    j["drafts"] = s.drafts.is_object() ? s.drafts : Json::object();
    Json w = Json::object();
    w["x"] = Json::number(s.window.x);
    w["y"] = Json::number(s.window.y);
    w["w"] = Json::number(s.window.w);
    w["h"] = Json::number(s.window.h);
    w["maximized"] = Json::boolean(s.window.maximized);
    j["window"] = std::move(w);
    const std::string body = j.dump();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

}  // namespace scyllagpt
