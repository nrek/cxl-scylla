#pragma once

// Settings → Agent Providers — four providers (ChatGPT, OpenAI API, Claude Account, Claude API).

#include "scyllagpt/commands.h"
#include "scyllagpt/session.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

enum class ProvidersSettingsAction {
    None = 0,
    PersistAndSync,
    RefreshOpenAI,
    RefreshClaude,
    RefreshOpenAiApi,
    RefreshClaudeApi,
    OpenAISignIn,
    OpenAISignOut,
    OpenAiApiKey,
    OpenAiApiDisconnect,
    ClaudeApiKey,
    ClaudeApiDisconnect,
    ClaudeCodeLogin,
    ClaudeDisconnect,
    Relayout,
};

class ProvidersSettingsUi {
public:
    bool create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small);
    void destroy();
    void set_fonts(HFONT font, HFONT font_small);
    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void layout(const RECT& area);
    void refresh(Session& session);

    ProvidersSettingsAction handle_command(int id, WORD notify, Session& session, HWND owner);
    bool measure_item(MEASUREITEMSTRUCT* mi) const;
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;
    bool owns_hwnd(HWND child) const;
    bool on_card(HWND child) const;
    void paint_chrome(HDC dc) const;

private:
    enum class Expand { None, OpenAI, OpenAiApi, Claude, ClaudeApi };

    struct CardHwnds {
        HWND name = nullptr;
        HWND badge = nullptr;
        HWND meta = nullptr;
        HWND auth = nullptr;
        HWND default_lbl = nullptr;
        HWND default_sel = nullptr;
        HWND count = nullptr;
        HWND manage = nullptr;
        HWND refresh = nullptr;
        HWND primary = nullptr;
        HWND secondary = nullptr;
        HWND models_heading = nullptr;
        HWND models_hint = nullptr;
        HWND search = nullptr;
        HWND models = nullptr;
        HWND enabled_lbl = nullptr;
        HWND default_footer = nullptr;
        HWND enable_all = nullptr;
        HWND disable_all = nullptr;
        RECT frame{};
    };

    void hide_all();
    std::vector<HWND> all_controls() const;
    void fill_provider_defaults(HWND select, Session& session, const std::string& provider);
    void fill_active_models(Session& session);
    void fill_model_list(HWND list, Session& session, const std::string& provider, const std::wstring& filter,
                         std::vector<std::string>* id_store);
    void update_counts(Session& session);
    void set_expand(Expand e);
    int place_card(CardHwnds& card, int x, int y, int w, bool connected, bool expanded);
    std::wstring model_blurb(const ModelChoice& m) const;
    void paint_frame(HDC dc, const RECT& r) const;
    static int provider_index(const std::string& pid);
    static std::string provider_at_index(int idx);

    HWND parent_ = nullptr;
    HINSTANCE inst_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_small_ = nullptr;
    bool visible_ = false;
    Expand expand_ = Expand::None;
    RECT area_{};
    bool oa_connected_ = false;
    bool oa_api_connected_ = false;
    bool cl_connected_ = false;
    bool cl_api_connected_ = false;

    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;

    CardHwnds oa_;
    CardHwnds oa_api_;
    CardHwnds cl_;
    CardHwnds cl_api_;

    HWND active_heading_ = nullptr;
    HWND active_hint_ = nullptr;
    HWND active_prov_lbl_ = nullptr;
    HWND active_prov_ = nullptr;
    HWND active_model_lbl_ = nullptr;
    HWND active_model_ = nullptr;
    RECT active_frame_{};

    std::vector<std::string> oa_model_ids_;
    std::vector<std::string> oa_api_model_ids_;
    std::vector<std::string> cl_model_ids_;
    std::vector<std::string> cl_api_model_ids_;
    std::wstring oa_filter_;
    std::wstring oa_api_filter_;
    std::wstring cl_filter_;
    std::wstring cl_api_filter_;
};

}  // namespace scyllagpt
