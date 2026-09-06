#pragma once

#include "scyllagpt/json.h"

#include <string>

namespace scyllagpt {

struct WindowPlacement {
    int x = 80;
    int y = 80;
    int w = 1440;
    int h = 860;
    bool maximized = false;
};

struct Settings {
    std::wstring codex_path;
    WindowPlacement window;
    bool enter_sends = true;
    std::string last_thread_id;
    Json drafts;  // object: threadId -> draft text
    std::wstring project_folder;
    int files_w = 220;
    int agent_w = 400;
    int history_w = 232;
    int files_mode = 0;    // 0 auto, 1 on, 2 off
    int history_mode = 0;
    bool focus_editor = false;
    std::string default_provider = "openai";  // "openai" | "claude"
    std::string selected_model;               // last model id (OpenAI or Claude)
    bool word_wrap = false;
    bool show_whitespace = false;
};

Settings load_settings(const std::wstring& path);
bool save_settings(const std::wstring& path, const Settings& s);

}  // namespace scyllagpt
