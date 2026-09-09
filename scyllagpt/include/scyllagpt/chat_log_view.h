#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "scyllagpt/chat_history.h"
#include "scyllagpt/utf.h"

#include <functional>
#include <string>
#include <vector>

namespace scyllagpt {

struct ChatLogMessage {
    bool user = false;
    std::wstring text;
    std::vector<DisplayAttachment> attachments;
};

using ChatLogAttachmentCallback = std::function<void(const std::vector<DisplayAttachment>&)>;
using ChatLogFileOpenCallback = std::function<void(const std::wstring& path)>;

HWND chat_log_create(HWND parent, HFONT font, ChatLogAttachmentCallback on_attachment,
                     ChatLogFileOpenCallback on_file_open = {});
void chat_log_set_font(HWND chat_log, HFONT font);
void chat_log_set_messages(HWND chat_log, std::vector<ChatLogMessage> messages);
void chat_log_clear(HWND chat_log);
bool chat_log_empty(HWND chat_log);
void chat_log_scroll_bottom(HWND chat_log);

}  // namespace scyllagpt
