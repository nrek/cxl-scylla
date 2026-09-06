#include "scyllagpt/settings.h"

#include "scyllagpt/utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <sstream>

namespace scyllagpt {

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
    s.project_folder = utf16(j.at("project_folder").as_string());
    s.files_w = static_cast<int>(j.at("files_w").as_int(220));
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
    j["project_folder"] = Json::string(utf8(s.project_folder));
    j["files_w"] = Json::number(s.files_w);
    j["agent_w"] = Json::number(s.agent_w);
    j["history_w"] = Json::number(s.history_w);
    j["files_mode"] = Json::number(s.files_mode);
    j["history_mode"] = Json::number(s.history_mode);
    j["focus_editor"] = Json::boolean(s.focus_editor);
    j["default_provider"] = Json::string(s.default_provider);
    j["selected_model"] = Json::string(s.selected_model);
    j["word_wrap"] = Json::boolean(s.word_wrap);
    j["show_whitespace"] = Json::boolean(s.show_whitespace);
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
