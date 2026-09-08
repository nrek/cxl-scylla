#include "scyllagpt/terminal_profiles.h"

#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

static int g_tp_fail = 0;

static void tp_expect(bool cond, const char* name) {
    if (!cond) {
        std::cerr << "FAIL " << name << "\n";
        ++g_tp_fail;
    } else {
        std::cout << "ok   " << name << "\n";
    }
}

int run_terminal_profile_tests() {
    using scyllagpt::WorkingDirectoryMode;
    using scyllagpt::TerminalProfile;

    tp_expect(scyllagpt::normalize_agent_terminal_policy("") == "ask", "policy empty -> ask");
    tp_expect(scyllagpt::normalize_agent_terminal_policy("ALLOW") == "allow", "policy ALLOW");
    tp_expect(scyllagpt::normalize_agent_terminal_policy("block") == "block", "policy block");
    tp_expect(scyllagpt::normalize_agent_terminal_policy("nope") == "ask", "policy junk -> ask");
    tp_expect(scyllagpt::is_valid_agent_terminal_policy("ask"), "policy valid ask");
    tp_expect(!scyllagpt::is_valid_agent_terminal_policy("maybe"), "policy invalid maybe");

    tp_expect(scyllagpt::working_directory_mode_string(WorkingDirectoryMode::Project) == "project",
              "cwd mode project");
    tp_expect(scyllagpt::parse_working_directory_mode("home") == WorkingDirectoryMode::Home, "parse home");
    tp_expect(scyllagpt::make_terminal_profile_id("wsl", L"Ubuntu-22.04") == "wsl:ubuntu-22-04",
              "slug wsl id");

    {
        // UTF-16LE: U\0b\0u\0n\0t\0u\0\n\0D\0e\0b\0i\0a\0n\0\n\0
        const char raw[] = {'U', 0, 'b', 0, 'u', 0, 'n', 0, 't', 0, 'u', 0, '\n', 0,
                            'D', 0, 'e', 0, 'b', 0, 'i', 0, 'a', 0, 'n', 0, '\n', 0};
        const auto names = scyllagpt::parse_wsl_list_quiet(std::string_view(raw, sizeof(raw)));
        tp_expect(names.size() == 2 && names[0] == L"Ubuntu" && names[1] == L"Debian", "parse wsl utf16");
    }
    {
        const auto names = scyllagpt::parse_wsl_list_quiet("Alpine\nFedora\n");
        tp_expect(names.size() == 2 && names[0] == L"Alpine", "parse wsl utf8");
    }

    {
        TerminalProfile p;
        p.working_directory_mode = WorkingDirectoryMode::Project;
        tp_expect(scyllagpt::resolve_profile_cwd(p, L"D:\\proj", L"C:\\Users\\x") == L"D:\\proj",
                  "resolve project cwd");
        p.working_directory_mode = WorkingDirectoryMode::Home;
        tp_expect(scyllagpt::resolve_profile_cwd(p, L"D:\\proj", L"C:\\Users\\x") == L"C:\\Users\\x",
                  "resolve home cwd");
        p.working_directory_mode = WorkingDirectoryMode::Custom;
        p.custom_working_directory = L"E:\\work";
        tp_expect(scyllagpt::resolve_profile_cwd(p, L"D:\\proj", L"C:\\Users\\x") == L"E:\\work",
                  "resolve custom cwd");
    }

    tp_expect(scyllagpt::is_known_shell_executable(L"C:\\Windows\\System32\\cmd.exe"), "known cmd");
    tp_expect(scyllagpt::is_known_shell_executable(L"pwsh.exe"), "known pwsh");
    tp_expect(!scyllagpt::is_known_shell_executable(L"notepad.exe"), "unknown notepad");

    {
        const auto profiles = scyllagpt::discover_terminal_profiles();
        bool has_cmd = false;
        for (const auto& p : profiles) {
            if (p.id == "cmd" || scyllagpt::is_known_shell_executable(p.executable)) {
                if (p.id == "cmd") {
                    has_cmd = true;
                }
            }
        }
        for (const auto& p : profiles) {
            if (p.id == "cmd") {
                has_cmd = true;
                tp_expect(!p.executable.empty() && p.enabled, "cmd profile enabled");
                break;
            }
        }
        tp_expect(!profiles.empty(), "discover non-empty");
        tp_expect(has_cmd, "discover includes cmd.exe");
    }

    {
        std::vector<scyllagpt::TerminalProfile> discovered;
        scyllagpt::TerminalProfile a;
        a.id = "cmd";
        a.name = L"Command Prompt";
        a.enabled = true;
        discovered.push_back(a);
        scyllagpt::TerminalProfile b;
        b.id = "pwsh";
        b.name = L"PowerShell 7";
        b.enabled = true;
        discovered.push_back(b);

        std::map<std::string, bool> prefs;
        prefs["pwsh"] = false;
        auto merged = scyllagpt::merge_terminal_profiles(discovered, {}, prefs);
        tp_expect(merged.size() == 2, "merge size");
        tp_expect(merged[0].enabled, "cmd stays enabled");
        tp_expect(!merged[1].enabled, "pwsh disabled by prefs");

        const auto enabled = scyllagpt::enabled_terminal_profiles(merged);
        tp_expect(enabled.size() == 1 && enabled[0].id == "cmd", "enabled filter");

        const auto* def = scyllagpt::find_default_terminal_profile(merged, "pwsh");
        tp_expect(def && def->id == "cmd", "default skips disabled preferred");
        def = scyllagpt::find_default_terminal_profile(merged, "cmd");
        tp_expect(def && def->id == "cmd", "default uses preferred");
    }

    return g_tp_fail;
}
