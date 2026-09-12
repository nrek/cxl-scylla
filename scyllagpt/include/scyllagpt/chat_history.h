#pragma once
#include <ctime>
#include <string>
#include <sstream>
#include "scyllagpt/store.h"

namespace scyllagpt {
struct DisplayAttachment { std::string path, label; bool image = false; };
struct UserDisplay { std::string text; std::vector<DisplayAttachment> attachments; };
inline std::string user_display_metadata(const std::string& text, const std::vector<DisplayAttachment>& attachments = {}) {
    Json display = Json::object();
    display["text"] = Json::string(text);
    display["attachments"] = Json::array();
    for (const auto& attachment : attachments) {
        Json item = Json::object();
        item["path"] = Json::string(attachment.path);
        item["label"] = Json::string(attachment.label);
        item["image"] = Json::boolean(attachment.image);
        display["attachments"].push(std::move(item));
    }
    return "\n<scylla-display-json>" + display.dump() + "</scylla-display-json>\n";
}
inline UserDisplay parse_user_display(const std::string& text) {
    UserDisplay display{text, {}};
    const std::string prefix = "<scylla-display-json>";
    const auto start = text.find(prefix);
    if (start != std::string::npos) {
        // Parse a JSON string, so embedded closing markers in user text are harmless.
        const auto begin = start + prefix.size();
        const std::string suffix = "</scylla-display-json>";
        auto end = text.find(suffix, begin);
        while (end != std::string::npos) {
                std::string error;
                auto value = Json::parse(text.substr(begin, end - begin), &error);
                if (error.empty() && value.is_string()) { display.text = value.as_string(); return display; }
                if (error.empty() && value.is_object()) {
                    display.text = value.at("text").as_string("");
                    if (value.at("attachments").is_array()) for (const auto& item : value.at("attachments").array_items()) {
                        DisplayAttachment attachment{item.at("path").as_string(), item.at("label").as_string(), item.at("image").as_bool(false)};
                        if (!attachment.path.empty()) display.attachments.push_back(std::move(attachment));
                    }
                    return display;
                }
                // A literal closing marker can occur inside the JSON string.
                // Keep searching until the complete envelope parses.
                end = text.find(suffix, end + suffix.size());
        }
    }
    // Compatibility with Scylla's previous prompt prefix on restored threads.
    // Modern runtime transcripts contain the injected mode/grant header but may
    // predate the display envelope. Only unwrap recognized leading headers.
    if (text.starts_with("Execute mode: carry out the requested work and validate the result.") ||
        text.starts_with("Plan mode: inspect project and Knowledge files read-only.") ||
        text.starts_with("Ask mode: discover and explain information in project and Knowledge files read-only.") ||
        text.starts_with("Knowledge folders granted for this turn (absolute paths; not limited to the project cwd):")) {
        const auto boundary = text.find("\n---\n");
        const auto crlf_boundary = text.find("\r\n---\r\n");
        if (boundary != std::string::npos || crlf_boundary != std::string::npos) {
            const bool crlf = crlf_boundary < boundary;
            return parse_user_display(text.substr((crlf ? crlf_boundary : boundary) + (crlf ? 7 : 5)));
        }
    }
    const auto workflow = text.find("Scylla workflow: ");
    if (workflow != std::string::npos && (text.starts_with("Scylla workflow") || text.starts_with("Knowledge folders granted"))) {
        const std::string end = "This mode applies to this message only.\n\n";
        const auto boundary = text.find(end, workflow);
        if (boundary != std::string::npos) { display.text = text.substr(boundary + end.size()); return display; }
    }
    return display;
}
inline std::string visible_user_text(const std::string& text) {
    return parse_user_display(text).text;
}
inline std::string short_chat_title(const std::string& value) {
    std::istringstream words(value);
    std::string word, out;
    for (int i = 0; i < 4 && words >> word; ++i) {
        if (out.size() + word.size() > 100) break;
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}
inline std::string chat_review_request() {
    return "\nIn your final response, link files that need review and any handoff you created or updated, "
        "using Markdown links with absolute file paths. Do not repeat your name or introduce yourself.\n";
}
inline std::string chat_title_request(const Conversation* chat) {
    return chat && !chat->title_manual && !chat->title_generated
        ? "\nChat naming: At the end of your response, suggest a meaningful 3-4 word chat title as "
          "<!--scylla-title: Your Short Title-->. This optional metadata is used by the app; do not rename files.\n" : "";
}
inline void accept_chat_title(Conversation& chat, const std::string& response) {
    if (chat.title_manual || chat.title_generated) return;
    const std::string prefix = "<!--scylla-title:";
    const auto start = response.rfind(prefix);
    if (start == std::string::npos) return;
    const auto end = response.find("-->", start);
    if (end == std::string::npos) return;
    const auto title = short_chat_title(response.substr(start + prefix.size(), end - start - prefix.size()));
    if (!title.empty()) { chat.title = title; chat.title_generated = true; }
}
inline std::string visible_chat_text(std::string text) {
    const auto pos = text.find("<!--scylla-title:");
    if (pos != std::string::npos) text.resize(pos);
    // Hide an incomplete metadata prefix while streaming it.
    const std::string prefix = "<!--scylla-title:";
    for (std::size_t n = 1; n < prefix.size() && n <= text.size(); ++n)
        if (text.compare(text.size() - n, n, prefix, 0, n) == 0) { text.resize(text.size() - n); break; }
    return text;
}
inline std::wstring chat_date_group(std::int64_t timestamp, std::time_t now = std::time(nullptr)) {
    if (timestamp <= 0) return L"Earlier";
    const std::time_t when = static_cast<std::time_t>(timestamp);
    std::tm date{}, today{};
    if (localtime_s(&date, &when) || localtime_s(&today, &now)) return L"Earlier";
    auto same = [](const std::tm& a, const std::tm& b) { return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday; };
    if (same(date, today)) return L"Today";
    today.tm_hour = 12; today.tm_mday -= 1; today.tm_isdst = -1;
    std::mktime(&today);
    if (same(date, today)) return L"Yesterday";
    wchar_t label[32]{};
    std::wcsftime(label, 32, L"%Y-%m-%d", &date);
    return label;
}
} // namespace scyllagpt
