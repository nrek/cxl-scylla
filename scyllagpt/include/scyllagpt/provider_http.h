#pragma once

// WinHTTP transports for OpenAI API + Anthropic API (BYOK). Keys never logged.

#include <string>
#include <vector>

namespace scyllagpt {

struct HttpModelRow {
    std::string id;
    std::string display;
    std::string created; // Provider timestamp; empty when unavailable.
};

struct HttpChatMessage {
    bool user = true;
    std::string text;
};

// GET https://api.openai.com/v1/models — filters to gpt-/o*/chatgpt* chat-ish ids.
bool openai_api_list_models(std::vector<HttpModelRow>* out, std::wstring* error);

// POST /v1/chat/completions (non-stream) using CredMan OpenAI key.
bool openai_api_chat(const std::string& model_id, const std::vector<HttpChatMessage>& messages,
                     std::string* result_text, std::wstring* error, unsigned timeout_ms = 300000);

// GET https://api.anthropic.com/v1/models
bool claude_api_list_models(std::vector<HttpModelRow>* out, std::wstring* error);

// POST /v1/messages (non-stream) using CredMan Anthropic key.
bool claude_api_chat(const std::string& model_id, const std::vector<HttpChatMessage>& messages,
                     std::string* result_text, std::wstring* error, unsigned timeout_ms = 300000);

}  // namespace scyllagpt
