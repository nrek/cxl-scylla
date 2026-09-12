#include "scyllagpt/strata_bridge.h"

#include "scyllagpt/utf.h"
#include "scyllagpt/knowledge.h"
#include <filesystem>
#include <deque>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>

namespace scyllagpt {
std::vector<std::wstring> discover_strata_workspaces(const KnowledgeStore& knowledge,
                                                    const std::string& project_id) {
    namespace fs = std::filesystem;
    std::vector<std::wstring> result;
    for (const auto* source : knowledge.list_enabled_for_project(project_id)) {
        std::deque<fs::path> pending{fs::path(source->path)};
        std::size_t visited = 0;
        while (!pending.empty() && visited++ < 10000) {
            const fs::path dir = pending.front();
            pending.pop_front();
            const auto access = knowledge.resolve_effective_access(source->id, dir.wstring());
            if (!access.readable) continue;
            const DWORD attrs = GetFileAttributesW(dir.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            // Bridge v1 always uses workspace/.md/workspace_index.sqlite. A Knowledge
            // source may itself be .md, or contain any number of workspace subfolders.
            if (_wcsicmp(dir.filename().c_str(), L".md") == 0) {
                const auto db = dir / L"workspace_index.sqlite";
                const DWORD db_attrs = GetFileAttributesW(db.c_str());
                if (knowledge.resolve_effective_access(source->id, db.wstring()).readable &&
                    db_attrs != INVALID_FILE_ATTRIBUTES && !(db_attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                    const auto root = dir.parent_path().wstring();
                    if (std::none_of(result.begin(), result.end(), [&](const auto& prior) {
                        return _wcsicmp(prior.c_str(), root.c_str()) == 0;
                    })) result.push_back(root);
                }
            }
            std::error_code error;
            std::vector<fs::path> children;
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error), end;
                 !error && it != end; it.increment(error)) {
                const auto path = it->path();
                const DWORD child_attrs = GetFileAttributesW(path.c_str());
                if (child_attrs != INVALID_FILE_ATTRIBUTES && (child_attrs & FILE_ATTRIBUTE_DIRECTORY) &&
                    !(child_attrs & FILE_ATTRIBUTE_REPARSE_POINT)) children.push_back(path);
            }
            std::sort(children.begin(), children.end());
            for (const auto& child : children) pending.push_back(child);
        }
    }
    return result;
}

std::wstring resolve_strata_workspace(const KnowledgeStore& knowledge, const std::string& project_id,
                                     const std::wstring& project_root) {
    const auto candidates = discover_strata_workspaces(knowledge, project_id);
    return candidates.empty() ? project_root : candidates.front();
}

namespace {

void close_handle(HANDLE& h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        h = nullptr;
    }
}

bool file_exists_w(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring quote_arg(const std::wstring& s) {
    if (s.find_first_of(L" \t\"") == std::wstring::npos) {
        return s;
    }
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') {
            out += L"\\\"";
        } else {
            out += c;
        }
    }
    out += L'"';
    return out;
}

bool search_path_exe(const wchar_t* name, std::wstring* out) {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, name, L".exe", MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    *out = buf;
    return true;
}

bool try_python_module(const std::wstring& python, StrataBridgeLaunch* launch) {
    // Probe: python -c "import cxl_strata.bridge"
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    // A GUI parent (or a captured test process) may have no valid stdin handle.
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_EXISTING, 0, nullptr);
    si.hStdInput = null_input;

    std::wstring cmd = quote_arg(python) + L" -c \"import cxl_strata.bridge\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(python.c_str(), buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    CloseHandle(wr);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (!ok) {
        launch->discovery_note = "Python probe could not start (Windows error " + std::to_string(GetLastError()) + ")";
        CloseHandle(rd);
        return false;
    }
    if (WaitForSingleObject(pi.hProcess, 8000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    if (code != 0) {
        launch->discovery_note = "Python could not import STRATA (exit " + std::to_string(code) + ")";
        return false;
    }
    launch->found = true;
    launch->exe = python;
    launch->args = L"-m cxl_strata.bridge";
    launch->discovery_note = "python -m cxl_strata.bridge";
    return true;
}

std::string json_str(const Json& obj, const char* key) {
    return obj.has(key) ? obj.at(key).as_string("") : std::string();
}

void append_hit_array(const Json& arr, std::vector<StrataBridgeHit>* out) {
    if (!arr.is_array()) {
        return;
    }
    for (const auto& item : arr.array_items()) {
        if (!item.is_object()) {
            continue;
        }
        StrataBridgeHit h;
        h.path = json_str(item, "path");
        h.kind = json_str(item, "kind");
        h.project = json_str(item, "project");
        h.title = json_str(item, "title");
        // recent rows carry `excerpt`; search rows carry `snippet`.
        h.snippet = item.has("snippet") ? item.at("snippet").as_string("") : json_str(item, "excerpt");
        h.updated_at = json_str(item, "updated_at");
        h.origin = json_str(item, "origin");
        h.sync_status = json_str(item, "sync_status");
        out->push_back(std::move(h));
    }
}

bool ci_contains(const std::string& hay, const char* needle) {
    const std::size_t n = strlen(needle);
    if (n == 0 || hay.size() < n) {
        return false;
    }
    for (std::size_t i = 0; i + n <= hay.size(); ++i) {
        std::size_t k = 0;
        while (k < n && static_cast<char>(tolower(static_cast<unsigned char>(hay[i + k]))) == needle[k]) {
            ++k;
        }
        if (k == n) {
            return true;
        }
    }
    return false;
}

bool starts_with(const std::string& s, const char* prefix) {
    const std::size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

// Mirrors cxl_strata.content_safety.SECRET_PATTERNS closely enough for a preview line.
bool looks_like_secret_value(const std::string& token) {
    if (token.size() < 8) {
        return false;
    }
    if (starts_with(token, "sk_live_") || starts_with(token, "sk_test_") || starts_with(token, "pk_live_") ||
        starts_with(token, "pk_test_")) {
        return true;
    }
    if (token.size() >= 20 && starts_with(token, "AKIA")) {
        return true;
    }
    return false;
}

bool is_secret_key_name(const std::string& name) {
    static const char* kNames[] = {"apikey", "api_key", "api-key", "secret", "password", "passwd",
                                   "token",  "bearer",  "credential"};
    for (const char* n : kNames) {
        if (ci_contains(name, n)) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool parse_bridge_envelope(const std::string& line, bool* ok, Json* result, std::string* error, std::int64_t* id) {
    if (ok) {
        *ok = false;
    }
    if (result) {
        *result = Json::null();
    }
    if (error) {
        error->clear();
    }
    std::string err;
    Json j = Json::parse(line, &err);
    if (!err.empty() || !j.is_object()) {
        if (error) {
            *error = err.empty() ? "invalid bridge envelope" : err;
        }
        return false;
    }
    if (id && j.has("id")) {
        *id = j.at("id").as_int(0);
    }
    const bool success = j.has("ok") && j.at("ok").as_bool(false);
    if (ok) {
        *ok = success;
    }
    if (success) {
        if (result && j.has("result")) {
            *result = j.at("result");
        }
    } else if (error) {
        if (j.has("error")) {
            *error = j.at("error").as_string("bridge error");
        } else {
            *error = "bridge returned ok=false";
        }
    }
    return true;
}

bool parse_bridge_status_result(const Json& result, StrataBridgeStatus* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    out->ok = true;
    out->index_exists = result.has("index_exists") && result.at("index_exists").as_bool(false);
    out->total = result.has("total") ? result.at("total").as_int(0) : 0;
    out->bridge_version = result.has("bridge_version") ? static_cast<int>(result.at("bridge_version").as_int(0)) : 0;
    out->strata_version = result.has("strata_version") ? result.at("strata_version").as_string("") : "";
    out->workspace_root = result.has("workspace_root") ? result.at("workspace_root").as_string("") : "";
    out->index_path = result.has("index_path") ? result.at("index_path").as_string("") : "";
    out->raw = result;
    out->message = out->index_exists ? "index ready" : "index missing";
    return true;
}

bool parse_bridge_capabilities_result(const Json& result, StrataBridgeCapabilities* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    out->ok = true;
    out->bridge_version =
        result.has("bridge_version") ? static_cast<int>(result.at("bridge_version").as_int(0)) : 0;
    out->strata_version = result.has("strata_version") ? result.at("strata_version").as_string("") : "";
    out->methods.clear();
    if (result.has("methods") && result.at("methods").is_array()) {
        for (const auto& m : result.at("methods").array_items()) {
            if (m.is_string()) {
                out->methods.push_back(m.as_string());
            }
        }
    }
    out->raw = result;
    out->message = "ok";
    return true;
}

bool parse_bridge_search_result(const Json& result, StrataBridgeSearchResult* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    out->ok = true;
    out->query = result.has("query") ? result.at("query").as_string("") : "";
    out->hits.clear();
    const Json* hits = nullptr;
    if (result.has("hits") && result.at("hits").is_array()) {
        hits = &result.at("hits");
    }
    if (hits) {
        for (const auto& item : hits->array_items()) {
            if (!item.is_object()) {
                continue;
            }
            StrataBridgeHit h;
            h.path = item.has("path") ? item.at("path").as_string("") : "";
            h.kind = item.has("kind") ? item.at("kind").as_string("") : "";
            h.project = item.has("project") ? item.at("project").as_string("") : "";
            h.title = item.has("title") ? item.at("title").as_string("") : "";
            h.snippet = item.has("snippet") ? item.at("snippet").as_string("") : "";
            h.updated_at = item.has("updated_at") ? item.at("updated_at").as_string("") : "";
            h.origin = item.has("origin") ? item.at("origin").as_string("") : "";
            h.sync_status = item.has("sync_status") ? item.at("sync_status").as_string("") : "";
            out->hits.push_back(std::move(h));
        }
    }
    out->raw = result;
    out->message = "ok";
    return true;
}

bool parse_bridge_recent_result(const Json& result, StrataBridgeRecentResult* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    out->ok = true;
    out->items.clear();
    // knowledge_recent has three shapes: {items}, {documents, handoffs_available, ...},
    // or a bare handoff envelope {handoffs}. Accept all of them.
    if (result.has("items")) {
        append_hit_array(result.at("items"), &out->items);
    }
    if (result.has("documents")) {
        append_hit_array(result.at("documents"), &out->items);
    }
    if (result.has("handoffs")) {
        append_hit_array(result.at("handoffs"), &out->items);
    }
    if (result.has("handoffs_available") && result.at("handoffs_available").is_object()) {
        const Json& avail = result.at("handoffs_available");
        if (avail.has("handoffs")) {
            append_hit_array(avail.at("handoffs"), &out->items);
        }
    }
    // The same handoff can arrive via documents and handoffs_available.
    std::vector<StrataBridgeHit> deduped;
    for (auto& h : out->items) {
        const bool seen = std::any_of(deduped.begin(), deduped.end(),
                                      [&](const StrataBridgeHit& e) { return e.path == h.path; });
        if (!seen) {
            deduped.push_back(std::move(h));
        }
    }
    out->items = std::move(deduped);
    out->raw = result;
    out->message = "ok";
    return true;
}

bool parse_bridge_get_result(const Json& result, StrataBridgeDocument* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    // The bridge nests the row under `document`; tolerate a flat row too.
    const Json& doc = (result.has("document") && result.at("document").is_object()) ? result.at("document") : result;
    if (!doc.is_object()) {
        return false;
    }
    out->ok = true;
    out->path = json_str(doc, "path");
    out->kind = json_str(doc, "kind");
    out->project = json_str(doc, "project");
    out->title = json_str(doc, "title");
    out->updated_at = json_str(doc, "updated_at");
    out->origin = json_str(doc, "origin");
    out->sync_status = json_str(doc, "sync_status");
    out->storage = json_str(doc, "storage");
    out->body = json_str(doc, "body");
    out->body_truncated = doc.has("body_truncated") && doc.at("body_truncated").as_bool(false);
    out->raw = result;
    out->message = "ok";
    return true;
}

bool parse_bridge_pending_result(const Json& result, StrataBridgePending* out) {
    if (!out || !result.is_object()) {
        return false;
    }
    out->ok = true;
    out->available = result.has("available") && result.at("available").as_bool(false);
    out->index_pending = result.has("index_pending") ? result.at("index_pending").as_int(0) : 0;
    out->sync_pending = result.has("sync_pending") ? result.at("sync_pending").as_int(0) : 0;
    out->total = result.has("total") ? result.at("total").as_int(0) : out->index_pending + out->sync_pending;
    out->raw = result;
    out->message = "ok";
    return true;
}

std::string strata_sanitize_preview(const std::string& body, std::size_t max_chars) {
    if (max_chars == 0) {
        return {};
    }
    // Redaction runs before truncation on a bounded prefix, so a cut can never
    // expose the tail of a value that the scanner would have masked.
    const std::size_t scan_limit = (std::min)(body.size(), static_cast<std::size_t>(8192));
    const std::string head = body.substr(0, scan_limit);
    if (ci_contains(head, "-----begin") && ci_contains(head, "private key")) {
        return "[redacted key material]";
    }

    std::vector<std::string> tokens;
    std::string cur;
    for (char c : head) {
        if (static_cast<unsigned char>(c) <= 0x20) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }

    std::string out;
    bool redact_next = false;
    for (std::string tok : tokens) {
        if (redact_next) {
            tok = "[redacted]";
            redact_next = false;
        } else if (looks_like_secret_value(tok)) {
            tok = "[redacted]";
        } else {
            const std::size_t sep = tok.find_first_of(":=");
            if (sep != std::string::npos && is_secret_key_name(tok.substr(0, sep))) {
                const std::string value = tok.substr(sep + 1);
                if (value.empty()) {
                    // "secret:" with the value in the next token.
                    redact_next = true;
                } else {
                    tok = tok.substr(0, sep + 1) + "[redacted]";
                }
            }
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += tok;
        if (out.size() > max_chars + 32) {
            break;
        }
    }

    if (out.size() <= max_chars) {
        return out;
    }
    std::size_t cut = out.rfind(' ', max_chars);
    if (cut == std::string::npos || cut + 24 < max_chars) {
        cut = max_chars;
        // A hard cut can land mid-sequence; utf16() would then see a truncated code point.
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
    }
    out.resize(cut);
    out += "\xE2\x80\xA6";  // U+2026
    return out;
}

StrataBridgeLaunch discover_strata_bridge_launch() {
    StrataBridgeLaunch launch;

    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const auto bundled = std::filesystem::path(module).parent_path() / L"strata" / L"strata.exe";
    if (file_exists_w(bundled.wstring())) {
        launch.found = true;
        launch.exe = bundled.wstring();
        launch.args = L"bridge";
        launch.discovery_note = "bundled STRATA bridge";
        return launch;
    }
    const auto adapter = std::filesystem::path(module).parent_path() / L"strata_workbench_bridge.py";
    if (file_exists_w(adapter.wstring())) {
        std::vector<std::wstring> candidates;
        std::wstring cli;
        if (search_path_exe(L"strata", &cli)) {
            candidates.push_back((std::filesystem::path(cli).parent_path().parent_path() / L"python.exe").wstring());
        }
        for (const auto* name : {L"python", L"python3", L"py"}) {
            std::wstring python;
            if (search_path_exe(name, &python)) candidates.push_back(python);
        }
        // Store Python installations may be registered without a PATH alias.
        for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            HKEY core = nullptr;
            if (RegOpenKeyExW(hive, L"Software\\Python\\PythonCore", 0, KEY_READ, &core) != ERROR_SUCCESS) continue;
            for (DWORD index = 0; ; ++index) {
                wchar_t version[256]{};
                DWORD size = 256;
                if (RegEnumKeyExW(core, index, version, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                wchar_t python[MAX_PATH]{};
                DWORD bytes = sizeof(python);
                const auto key = std::wstring(version) + L"\\InstallPath";
                if (RegGetValueW(core, key.c_str(), L"ExecutablePath", RRF_RT_REG_SZ, nullptr, python, &bytes) == ERROR_SUCCESS)
                    candidates.emplace_back(python);
            }
            RegCloseKey(core);
        }
        for (const auto& python : candidates) {
            if (file_exists_w(python) && try_python_module(python, &launch)) {
                launch.args = L"-u " + quote_arg(adapter.wstring());
                launch.discovery_note = "STRATA Workbench library bridge";
                return launch;
            }
        }
    }

    std::wstring strata;
    if (search_path_exe(L"strata", &strata)) {
        // Prefer `strata bridge` when the CLI is on PATH.
        launch.found = true;
        launch.exe = strata;
        launch.args = L"bridge";
        launch.discovery_note = "strata bridge";
        return launch;
    }

    const wchar_t* py_names[] = {L"python", L"python3", L"py"};
    for (const wchar_t* name : py_names) {
        std::wstring python;
        if (!search_path_exe(name, &python)) {
            continue;
        }
        if (try_python_module(python, &launch)) {
            return launch;
        }
    }

    launch.found = false;
    if (launch.discovery_note.empty())
        launch.discovery_note = "STRATA not installed (strata / python -m cxl_strata.bridge not found)";
    return launch;
}

const wchar_t* strata_bridge_state_label(StrataBridgeState state) {
    switch (state) {
        case StrataBridgeState::NotInstalled:
            return L"STRATA not installed";
        case StrataBridgeState::Stopped:
            return L"Bridge stopped";
        case StrataBridgeState::Running:
            return L"Connected";
        case StrataBridgeState::Error:
            return L"Bridge error";
        default:
            return L"Unknown";
    }
}

StrataBridge::~StrataBridge() {
    stop();
}

bool StrataBridge::running() const {
    if (!process_) {
        return false;
    }
    const DWORD wait = WaitForSingleObject(process_, 0);
    return wait == WAIT_TIMEOUT;
}

bool StrataBridge::start(std::wstring* error) {
    stop();
    decoder_.clear();
    last_error_.clear();

    const StrataBridgeLaunch launch = discover_strata_bridge_launch();
    if (!launch.found) {
        state_ = StrataBridgeState::NotInstalled;
        last_error_ = launch.discovery_note;
        if (error) {
            *error = utf16(launch.discovery_note);
        }
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_rd = nullptr;
    HANDLE stdout_wr = nullptr;
    if (!CreatePipe(&stdin_rd, &stdin_wr_, &sa, 0) || !CreatePipe(&stdout_rd_, &stdout_wr, &sa, 0)) {
        state_ = StrataBridgeState::Error;
        last_error_ = "CreatePipe failed";
        if (error) {
            *error = L"CreatePipe failed";
        }
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        return false;
    }
    SetHandleInformation(stdin_wr_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd_, HANDLE_FLAG_INHERIT, 0);

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) {
        state_ = StrataBridgeState::Error;
        last_error_ = "CreateJobObject failed";
        if (error) {
            *error = L"CreateJobObject failed";
        }
        CloseHandle(stdin_rd);
        CloseHandle(stdout_wr);
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdin_rd;
    si.hStdOutput = stdout_wr;
    si.hStdError = stdout_wr;

    std::wstring cmd = quote_arg(launch.exe);
    if (!launch.args.empty()) {
        cmd += L" ";
        cmd += launch.args;
    }
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(file_exists_w(launch.exe) ? launch.exe.c_str() : nullptr, cmd_buf.data(), nullptr,
                                   nullptr, TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si,
                                   &pi);
    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);
    if (!ok) {
        state_ = StrataBridgeState::Error;
        last_error_ = "CreateProcess failed";
        if (error) {
            *error = L"CreateProcess failed for STRATA bridge";
        }
        close_handle(job_);
        close_handle(stdin_wr_);
        close_handle(stdout_rd_);
        return false;
    }
    AssignProcessToJobObject(job_, pi.hProcess);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    CloseHandle(pi.hThread);
    state_ = StrataBridgeState::Running;
    return true;
}

void StrataBridge::stop() {
    close_handle(stdin_wr_);
    if (process_) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 2000);
    }
    close_handle(process_);
    close_handle(stdout_rd_);
    close_handle(job_);
    pid_ = 0;
    decoder_.clear();
    if (state_ == StrataBridgeState::Running) {
        state_ = StrataBridgeState::Stopped;
    }
}

bool StrataBridge::ensure_started(std::wstring* error) {
    if (running()) {
        return true;
    }
    return start(error);
}

bool StrataBridge::write_line(const std::string& line) {
    if (!stdin_wr_) {
        return false;
    }
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }
    DWORD wr = 0;
    return WriteFile(stdin_wr_, payload.data(), static_cast<DWORD>(payload.size()), &wr, nullptr) != 0;
}

bool StrataBridge::read_response_for(std::int64_t id, Json* out, std::string* error, DWORD timeout_ms) {
    if (!stdout_rd_ || !out) {
        if (error) {
            *error = "bridge not running";
        }
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        if (!running()) {
            state_ = StrataBridgeState::Stopped;
            if (error) {
                *error = "bridge process exited";
            }
            return false;
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(stdout_rd_, nullptr, 0, nullptr, &avail, nullptr)) {
            if (error) {
                *error = "PeekNamedPipe failed";
            }
            return false;
        }
        if (avail == 0) {
            Sleep(20);
            continue;
        }
        const DWORD to_read = (std::min)(avail, static_cast<DWORD>(sizeof(buf)));
        DWORD rd = 0;
        if (!ReadFile(stdout_rd_, buf, to_read, &rd, nullptr) || rd == 0) {
            Sleep(20);
            continue;
        }
        std::vector<std::string> lines;
        std::string feed_err;
        if (!decoder_.feed(buf, rd, lines, &feed_err)) {
            if (error) {
                *error = feed_err.empty() ? "bridge decoder overflow" : feed_err;
            }
            return false;
        }
        for (const auto& line : lines) {
            bool ok = false;
            Json result;
            std::string err;
            std::int64_t resp_id = -1;
            if (!parse_bridge_envelope(line, &ok, &result, &err, &resp_id)) {
                continue;
            }
            if (resp_id != id) {
                continue;
            }
            if (!ok) {
                if (error) {
                    *error = err.empty() ? "bridge error" : err;
                }
                return false;
            }
            *out = result;
            return true;
        }
    }
    if (error) {
        *error = "bridge request timed out";
    }
    return false;
}

Json StrataBridge::request(const char* method, Json params, DWORD timeout_ms, std::string* error) {
    std::wstring start_err;
    if (!ensure_started(&start_err)) {
        if (error) {
            *error = utf8(start_err);
            if (error->empty()) {
                *error = last_error_.empty() ? "STRATA not installed" : last_error_;
            }
        }
        return Json::null();
    }
    const std::int64_t id = next_id_++;
    Json req = Json::object();
    req["id"] = Json::number(id);
    req["method"] = Json::string(method);
    req["params"] = params.is_null() ? Json::object() : std::move(params);
    if (!write_line(req.dump())) {
        if (error) {
            *error = "failed to write to bridge";
        }
        state_ = StrataBridgeState::Error;
        return Json::null();
    }
    Json result;
    if (!read_response_for(id, &result, error, timeout_ms)) {
        return Json::null();
    }
    return result;
}

StrataBridgeCapabilities StrataBridge::capabilities(DWORD timeout_ms) {
    StrataBridgeCapabilities out;
    std::string err;
    Json result = request("capabilities", Json::object(), timeout_ms, &err);
    if (result.is_null()) {
        out.message = err.empty() ? last_error_ : err;
        if (state_ != StrataBridgeState::NotInstalled) {
            state_ = running() ? StrataBridgeState::Running : StrataBridgeState::Error;
        }
        return out;
    }
    parse_bridge_capabilities_result(result, &out);
    return out;
}

Json StrataBridge::library(Json params, std::string* error, DWORD timeout_ms) {
    if (!params.has("workspace_root") && !default_workspace_.empty())
        params["workspace_root"] = Json::string(utf8(default_workspace_));
    return request("library", std::move(params), timeout_ms, error);
}

StrataBridgeStatus StrataBridge::status(const std::string& workspace_root, DWORD timeout_ms) {
    StrataBridgeStatus out;
    Json params = Json::object();
    std::string root = workspace_root;
    if (root.empty() && !default_workspace_.empty()) {
        root = utf8(default_workspace_);
    }
    if (!root.empty()) {
        params["workspace_root"] = Json::string(root);
    }
    std::string err;
    Json result = request("status", std::move(params), timeout_ms, &err);
    if (result.is_null()) {
        out.message = err.empty() ? last_error_ : err;
        return out;
    }
    parse_bridge_status_result(result, &out);
    return out;
}

StrataBridgeSearchResult StrataBridge::search(const std::string& query, int limit, const std::string& project,
                                              const std::string& workspace_root, DWORD timeout_ms) {
    StrataBridgeSearchResult out;
    Json params = Json::object();
    params["query"] = Json::string(query);
    params["limit"] = Json::number(limit);
    if (!project.empty()) {
        params["project"] = Json::string(project);
    }
    std::string root = workspace_root;
    if (root.empty() && !default_workspace_.empty()) {
        root = utf8(default_workspace_);
    }
    if (!root.empty()) {
        params["workspace_root"] = Json::string(root);
    }
    std::string err;
    Json result = request("search", std::move(params), timeout_ms, &err);
    if (result.is_null()) {
        out.message = err.empty() ? last_error_ : err;
        return out;
    }
    parse_bridge_search_result(result, &out);
    return out;
}

StrataBridgeRecentResult StrataBridge::recent(const std::string& project, int hours, int limit,
                                              const std::string& workspace_root, DWORD timeout_ms) {
    StrataBridgeRecentResult out;
    Json params = Json::object();
    params["hours"] = Json::number(hours);
    params["limit"] = Json::number(limit);
    if (!project.empty()) {
        params["project"] = Json::string(project);
    }
    std::string root = workspace_root;
    if (root.empty() && !default_workspace_.empty()) {
        root = utf8(default_workspace_);
    }
    if (!root.empty()) {
        params["workspace_root"] = Json::string(root);
    }
    std::string err;
    Json result = request("recent", std::move(params), timeout_ms, &err);
    if (result.is_null()) {
        out.message = err.empty() ? last_error_ : err;
        return out;
    }
    parse_bridge_recent_result(result, &out);
    return out;
}

StrataBridgeDocument StrataBridge::get(const std::string& path, const std::string& workspace_root,
                                       DWORD timeout_ms) {
    StrataBridgeDocument out;
    if (path.empty()) {
        out.message = "no document path";
        return out;
    }
    Json params = Json::object();
    params["path"] = Json::string(path);
    std::string root = workspace_root;
    if (root.empty() && !default_workspace_.empty()) {
        root = utf8(default_workspace_);
    }
    if (!root.empty()) {
        params["workspace_root"] = Json::string(root);
    }
    std::string err;
    Json result = request("get", std::move(params), timeout_ms, &err);
    if (result.is_null()) {
        out.message = err.empty() ? last_error_ : err;
        return out;
    }
    parse_bridge_get_result(result, &out);
    return out;
}

StrataBridgePending StrataBridge::pending_counts(const std::string& project, const std::string& workspace_root,
                                                 DWORD timeout_ms) {
    StrataBridgePending out;
    Json params = Json::object();
    if (!project.empty()) {
        params["project"] = Json::string(project);
    }
    std::string root = workspace_root;
    if (root.empty() && !default_workspace_.empty()) {
        root = utf8(default_workspace_);
    }
    if (!root.empty()) {
        params["workspace_root"] = Json::string(root);
    }
    std::string err;
    Json result = request("pending_counts", std::move(params), timeout_ms, &err);
    if (result.is_null()) {
        // Older bridges answer "unknown method"; that must not read as an error state.
        out.message = err.empty() ? last_error_ : err;
        return out;
    }
    parse_bridge_pending_result(result, &out);
    return out;
}

}  // namespace scyllagpt
