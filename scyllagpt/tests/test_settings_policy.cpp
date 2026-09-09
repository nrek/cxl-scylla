#include "scyllagpt/settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <iostream>
#include <string>

static int g_pol_fail = 0;

static void pol_expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_pol_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

static std::wstring pol_temp_path() {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    wchar_t file[MAX_PATH]{};
    GetTempFileNameW(tmp, L"scyp", 0, file);
    DeleteFileW(file);
    std::wstring path(file);
    path += L".settings.json";
    return path;
}

static void write_raw(const std::wstring& path, const std::string& body) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD wr = 0;
    WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wr, nullptr);
    CloseHandle(h);
}

int run_settings_policy_tests() {
    using scyllagpt::ExecutionPolicy;
    using scyllagpt::PolicyMode;
    using scyllagpt::Settings;

    g_pol_fail = 0;

    pol_expect(scyllagpt::parse_policy_mode("allow", PolicyMode::Block) == PolicyMode::Allow,
               "parse allow");
    pol_expect(scyllagpt::parse_policy_mode("ask", PolicyMode::Block) == PolicyMode::Ask, "parse ask");
    pol_expect(scyllagpt::parse_policy_mode("block", PolicyMode::Allow) == PolicyMode::Block,
               "parse block");
    pol_expect(scyllagpt::parse_policy_mode("Allow", PolicyMode::Block) == PolicyMode::Block,
               "parse is case sensitive -> fallback");
    pol_expect(scyllagpt::parse_policy_mode("", PolicyMode::Ask) == PolicyMode::Ask,
               "parse empty -> fallback");
    pol_expect(std::string(scyllagpt::policy_mode_string(PolicyMode::Ask)) == "ask", "mode to string");

    const ExecutionPolicy strict = scyllagpt::strict_execution_policy();
    pol_expect(strict.human_terminals == PolicyMode::Allow, "strict allows human terminals");
    pol_expect(strict.agent_terminals == PolicyMode::Block, "strict blocks agent terminals");
    pol_expect(strict.approved_recipes == PolicyMode::Ask, "strict asks for approved recipes");
    pol_expect(scyllagpt::execution_policy_is_strict(strict), "strict recognises itself");

    ExecutionPolicy loose = strict;
    loose.human_terminals = PolicyMode::Block;
    pol_expect(!scyllagpt::execution_policy_is_strict(loose), "modified policy is not strict");

    // Defaults on a fresh Settings must already be Strict — a missing file must never be looser
    // than the shipped rule.
    const Settings fresh;
    pol_expect(scyllagpt::execution_policy_is_strict(fresh.execution_policy),
               "default Settings is strict");

    // Round-trip through settings.json. save_settings rebuilds the whole object, so a key that is
    // written but not read (or vice versa) silently disappears on restart.
    const std::wstring path = pol_temp_path();
    DeleteFileW(path.c_str());
    Settings out;
    out.execution_policy.human_terminals = PolicyMode::Ask;
    out.execution_policy.agent_terminals = PolicyMode::Ask;
    out.execution_policy.approved_recipes = PolicyMode::Block;
    out.codex_path = L"C:\\codex\\codex.exe";
    pol_expect(scyllagpt::save_settings(path, out), "save settings with policy");

    const Settings back = scyllagpt::load_settings(path);
    pol_expect(back.execution_policy.human_terminals == PolicyMode::Ask, "round-trip human ask");
    pol_expect(back.execution_policy.agent_terminals == PolicyMode::Ask, "round-trip agent ask");
    pol_expect(back.execution_policy.approved_recipes == PolicyMode::Block, "round-trip recipes block");
    pol_expect(back.codex_path == out.codex_path, "round-trip leaves existing keys intact");

    // A settings.json written before Execution Policy existed must come back Strict.
    write_raw(path, "{\"enter_sends\":true,\"files_w\":220}");
    const Settings legacy = scyllagpt::load_settings(path);
    pol_expect(scyllagpt::execution_policy_is_strict(legacy.execution_policy),
               "legacy settings default to strict");

    // Garbage values fall back per-field rather than to the loosest option.
    write_raw(path,
              "{\"execution_policy\":{\"human_terminals\":\"nonsense\",\"agent_terminals\":\"ask\","
              "\"approved_recipes\":\"\"}}");
    const Settings partial = scyllagpt::load_settings(path);
    pol_expect(partial.execution_policy.human_terminals == PolicyMode::Allow,
               "unparsable human falls back to strict default");
    pol_expect(partial.execution_policy.agent_terminals == PolicyMode::Ask, "valid agent survives");
    pol_expect(partial.execution_policy.approved_recipes == PolicyMode::Ask,
               "empty recipes falls back to strict default");

    // Agent terminals can never be "allow": Strict resolve drops protected values for agents, so
    // a stored allow would advertise a capability that does not exist.
    write_raw(path, "{\"execution_policy\":{\"agent_terminals\":\"allow\"}}");
    const Settings clamped = scyllagpt::load_settings(path);
    pol_expect(clamped.execution_policy.agent_terminals == PolicyMode::Block,
               "agent allow is clamped to block");

    // Four-provider BYOK: coerce + API models default disabled.
    pol_expect(scyllagpt::coerce_default_provider("openai-api") == "openai-api", "coerce openai-api");
    pol_expect(scyllagpt::coerce_default_provider("claude-api") == "claude-api", "coerce claude-api");
    pol_expect(scyllagpt::coerce_default_provider("nope") == "openai", "coerce unknown -> openai");
    pol_expect(scyllagpt::provider_id_from_string("openai-api") == scyllagpt::ProviderId::OpenAiApi,
               "provider id openai-api");
    pol_expect(scyllagpt::provider_id_from_string("claude-api") == scyllagpt::ProviderId::ClaudeApi,
               "provider id claude-api");
    Settings enable_s;
    pol_expect(scyllagpt::settings_model_enabled(enable_s, "openai", "gpt-5"),
               "account missing enable => on");
    pol_expect(!scyllagpt::settings_model_enabled(enable_s, "openai-api", "gpt-5"),
               "api missing enable => off");
    pol_expect(!scyllagpt::settings_model_enabled(enable_s, "claude-api", "claude-opus-4-6"),
               "claude-api missing enable => off");
    enable_s.model_enabled[scyllagpt::model_enable_key("openai-api", "gpt-5")] = true;
    pol_expect(scyllagpt::settings_model_enabled(enable_s, "openai-api", "gpt-5"),
               "api explicit enable => on");

    write_raw(path, "{\"default_provider\":\"openai-api\"}");
    const Settings api_def = scyllagpt::load_settings(path);
    pol_expect(api_def.default_provider == "openai-api", "load default_provider openai-api");

    DeleteFileW(path.c_str());
    return g_pol_fail;
}
