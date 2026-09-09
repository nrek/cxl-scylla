#include "scyllagpt/providers_settings_ui.h"

#include "scyllagpt/provider.h"
#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"
#include "scyllagpt/utf.h"

#include <algorithm>

namespace scyllagpt {
namespace {

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd ? hwnd : GetDesktopWindow())), 96);
}

void show_many(const std::vector<HWND>& hs, int cmd) {
    for (HWND h : hs) {
        if (h) {
            ShowWindow(h, cmd);
        }
    }
}

void move_show(HWND h, int x, int y, int w, int hh) {
    if (!h) {
        return;
    }
    MoveWindow(h, x, y, w, hh, TRUE);
    ShowWindow(h, SW_SHOW);
    InvalidateRect(h, nullptr, TRUE);
}

void hide(HWND h) {
    if (h) {
        ShowWindow(h, SW_HIDE);
    }
}

std::wstring get_text(HWND h) {
    if (!h) {
        return {};
    }
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(n));
    return s;
}

bool contains_ci(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c; };
    if (needle.size() > hay.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(hay[i + j]) != lower(needle[j])) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

void fill_solid(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

void frame_rect_color(HDC dc, const RECT& r, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    HGDIOBJ old_br = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(dc, old_br);
    SelectObject(dc, old);
    DeleteObject(pen);
}

}  // namespace

int ProvidersSettingsUi::provider_index(const std::string& pid) {
    if (pid == "openai-api") {
        return 1;
    }
    if (pid == "claude") {
        return 2;
    }
    if (pid == "claude-api") {
        return 3;
    }
    return 0;
}

std::string ProvidersSettingsUi::provider_at_index(int idx) {
    switch (idx) {
        case 1:
            return "openai-api";
        case 2:
            return "claude";
        case 3:
            return "claude-api";
        default:
            return "openai";
    }
}

bool ProvidersSettingsUi::create(HWND parent, HINSTANCE inst, HFONT font, HFONT font_small) {
    if (!parent) {
        return false;
    }
    destroy();
    parent_ = parent;
    inst_ = inst ? inst : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    font_ = font;
    font_small_ = font_small ? font_small : font;

    title_ = ui_kit::create_static(parent_, inst_, 0, L"Settings / Agent Providers", font_, false);
    subtitle_ = ui_kit::create_static(parent_, inst_, 0,
                                      L"Manage ChatGPT, OpenAI API, Claude Account, and Claude API separately.",
                                      font_small_, true);

    auto make_card = [&](CardHwnds& c, const wchar_t* name, UINT id_manage, UINT id_refresh, UINT id_default,
                         UINT id_search, UINT id_models, UINT id_enable_all, UINT id_disable_all,
                         UINT id_default_footer, UINT id_primary, UINT id_secondary) {
        c.name = ui_kit::create_static(parent_, inst_, 0, name, font_, false);
        c.badge = ui_kit::create_static(parent_, inst_, 0, L"Not Connected", font_small_, true);
        c.meta = ui_kit::create_static(parent_, inst_, 0, L"", font_small_, true);
        c.auth = ui_kit::create_static(parent_, inst_, 0, L"", font_small_, true);
        c.default_lbl = ui_kit::create_static(parent_, inst_, 0, L"Default model", font_small_, true);
        c.default_sel = ui_kit::create_select(parent_, inst_, id_default, font_);
        c.count = ui_kit::create_static(parent_, inst_, 0, L"", font_small_, true);
        c.manage = ui_kit::create_button(parent_, inst_, id_manage, L"Manage Models",
                                         ui_kit::ButtonKind::Secondary, font_);
        c.refresh = ui_kit::create_button(parent_, inst_, id_refresh, L"Refresh Models",
                                          ui_kit::ButtonKind::Secondary, font_);
        c.primary = ui_kit::create_button(parent_, inst_, id_primary, L"Sign in", ui_kit::ButtonKind::Primary,
                                          font_);
        c.secondary = ui_kit::create_button(parent_, inst_, id_secondary, L"Sign Out",
                                            ui_kit::ButtonKind::Secondary, font_);
        c.models_heading = ui_kit::create_static(parent_, inst_, 0, L"Models", font_, false);
        c.models_hint =
            ui_kit::create_static(parent_, inst_, 0, L"Choose which models appear in Scylla.", font_small_, true);
        c.search = ui_kit::create_text_field(parent_, inst_, id_search, font_);
        ui_kit::set_placeholder(c.search, L"Search models…");
        c.models = ui_kit::create_entity_list(parent_, inst_, id_models, font_);
        c.enabled_lbl = ui_kit::create_static(parent_, inst_, 0, L"", font_small_, true);
        c.default_footer = ui_kit::create_select(parent_, inst_, id_default_footer, font_);
        c.enable_all = ui_kit::create_button(parent_, inst_, id_enable_all, L"Enable All",
                                             ui_kit::ButtonKind::Ghost, font_);
        c.disable_all = ui_kit::create_button(parent_, inst_, id_disable_all, L"Disable All",
                                              ui_kit::ButtonKind::Ghost, font_);
    };

    make_card(oa_, L"OpenAI ChatGPT", Cmd_ProvOaManage, Cmd_ProvOaRefresh, Cmd_ProvOaDefault, Cmd_ProvOaSearch,
              Cmd_ProvOaModels, Cmd_ProvOaEnableAll, Cmd_ProvOaDisableAll, Cmd_ProvOaDefaultFooter,
              Cmd_SetOaSignIn, Cmd_SetOaSignOut);
    SetWindowTextW(oa_.primary, L"Sign in with ChatGPT");
    SetWindowTextW(oa_.models_heading, L"ChatGPT Models");

    make_card(oa_api_, L"OpenAI API", Cmd_ProvOaApiManage, Cmd_ProvOaApiRefresh, Cmd_ProvOaApiDefault,
              Cmd_ProvOaApiSearch, Cmd_ProvOaApiModels, Cmd_ProvOaApiEnableAll, Cmd_ProvOaApiDisableAll,
              Cmd_ProvOaApiDefaultFooter, Cmd_ProvOaApiConnect, Cmd_ProvOaApiDisc);
    SetWindowTextW(oa_api_.primary, L"Connect API key…");
    SetWindowTextW(oa_api_.secondary, L"Disconnect");
    SetWindowTextW(oa_api_.models_heading, L"OpenAI API Models");
    SetWindowTextW(oa_api_.models_hint, L"Queried from the API. None enabled until you select them.");

    make_card(cl_, L"Claude Account", Cmd_ProvClManage, Cmd_ProvClRefresh, Cmd_ProvClDefault, Cmd_ProvClSearch,
              Cmd_ProvClModels, Cmd_ProvClEnableAll, Cmd_ProvClDisableAll, Cmd_ProvClDefaultFooter, Cmd_SetClCode,
              Cmd_SetClDisc);
    SetWindowTextW(cl_.primary, L"Sign in with Claude Code");
    SetWindowTextW(cl_.secondary, L"Disconnect");
    SetWindowTextW(cl_.models_heading, L"Claude Account Models");

    make_card(cl_api_, L"Claude API", Cmd_ProvClApiManage, Cmd_ProvClApiRefresh, Cmd_ProvClApiDefault,
              Cmd_ProvClApiSearch, Cmd_ProvClApiModels, Cmd_ProvClApiEnableAll, Cmd_ProvClApiDisableAll,
              Cmd_ProvClApiDefaultFooter, Cmd_ProvClApiConnect, Cmd_ProvClApiDisc);
    SetWindowTextW(cl_api_.primary, L"Connect API key…");
    SetWindowTextW(cl_api_.secondary, L"Disconnect");
    SetWindowTextW(cl_api_.models_heading, L"Claude API Models");
    SetWindowTextW(cl_api_.models_hint, L"Queried from Anthropic. None enabled until you select them.");

    active_heading_ = ui_kit::create_static(parent_, inst_, 0, L"Active Agent", font_, false);
    active_hint_ = ui_kit::create_static(parent_, inst_, 0,
                                         L"Choose a provider, then an enabled model for that bucket.", font_small_,
                                         true);
    active_prov_lbl_ = ui_kit::create_static(parent_, inst_, 0, L"Provider", font_small_, true);
    active_prov_ = ui_kit::create_select(parent_, inst_, Cmd_ProvActiveProvider, font_);
    active_model_lbl_ = ui_kit::create_static(parent_, inst_, 0, L"Model", font_small_, true);
    active_model_ = ui_kit::create_select(parent_, inst_, Cmd_ProvActiveModel, font_);

    hide_all();
    return true;
}

void ProvidersSettingsUi::destroy() {
    hide_all();
    for (HWND h : all_controls()) {
        if (h) {
            DestroyWindow(h);
        }
    }
    oa_ = {};
    oa_api_ = {};
    cl_ = {};
    cl_api_ = {};
    title_ = subtitle_ = nullptr;
    active_heading_ = active_hint_ = active_prov_lbl_ = active_prov_ = nullptr;
    active_model_lbl_ = active_model_ = nullptr;
    parent_ = nullptr;
    visible_ = false;
}

void ProvidersSettingsUi::set_fonts(HFONT font, HFONT font_small) {
    font_ = font;
    font_small_ = font_small ? font_small : font;
    const HWND small_set[] = {
        subtitle_,        active_hint_,     active_prov_lbl_, active_model_lbl_, oa_.badge,        oa_.meta,
        oa_.auth,         oa_.default_lbl,  oa_.count,        oa_.models_hint,   oa_.enabled_lbl,  oa_api_.badge,
        oa_api_.meta,     oa_api_.auth,     oa_api_.default_lbl, oa_api_.count,  oa_api_.models_hint,
        oa_api_.enabled_lbl, cl_.badge,     cl_.meta,         cl_.auth,          cl_.default_lbl,  cl_.count,
        cl_.models_hint,  cl_.enabled_lbl,  cl_api_.badge,    cl_api_.meta,      cl_api_.auth,     cl_api_.default_lbl,
        cl_api_.count,    cl_api_.models_hint, cl_api_.enabled_lbl};
    for (HWND h : all_controls()) {
        if (!h) {
            continue;
        }
        HFONT use = font_;
        for (HWND s : small_set) {
            if (s == h) {
                use = font_small_;
                break;
            }
        }
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(use), TRUE);
    }
}

std::vector<HWND> ProvidersSettingsUi::all_controls() const {
    std::vector<HWND> out = {title_, subtitle_};
    auto add_card = [&](const CardHwnds& c) {
        const HWND hs[] = {c.name,          c.badge,         c.meta,           c.auth,          c.default_lbl,
                           c.default_sel,   c.count,         c.manage,         c.refresh,       c.primary,
                           c.secondary,     c.models_heading, c.models_hint,   c.search,        c.models,
                           c.enabled_lbl,   c.default_footer, c.enable_all,    c.disable_all};
        for (HWND h : hs) {
            if (h) {
                out.push_back(h);
            }
        }
    };
    add_card(oa_);
    add_card(oa_api_);
    add_card(cl_);
    add_card(cl_api_);
    out.push_back(active_heading_);
    out.push_back(active_hint_);
    out.push_back(active_prov_lbl_);
    out.push_back(active_prov_);
    out.push_back(active_model_lbl_);
    out.push_back(active_model_);
    return out;
}

void ProvidersSettingsUi::hide_all() {
    show_many(all_controls(), SW_HIDE);
}

void ProvidersSettingsUi::set_visible(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
}

bool ProvidersSettingsUi::owns_hwnd(HWND child) const {
    if (!child) {
        return false;
    }
    for (HWND h : all_controls()) {
        if (h == child) {
            return true;
        }
    }
    return false;
}

bool ProvidersSettingsUi::on_card(HWND child) const {
    return child && child != title_ && child != subtitle_ && owns_hwnd(child);
}

std::wstring ProvidersSettingsUi::model_blurb(const ModelChoice& m) const {
    const std::string& id = m.id;
    if (id.find("opus") != std::string::npos) {
        return L"Flagship reasoning";
    }
    if (id.find("sonnet") != std::string::npos) {
        return L"General purpose";
    }
    if (id.find("haiku") != std::string::npos) {
        return L"Fast and reliable";
    }
    if (id.rfind("gpt-6", 0) == 0) {
        return L"Flagship reasoning";
    }
    if (id.find("codex") != std::string::npos) {
        return L"Coding";
    }
    if (id.find("mini") != std::string::npos || id.find("nano") != std::string::npos) {
        return L"Fast and reliable";
    }
    if (id.rfind("gpt-5", 0) == 0) {
        return L"General purpose";
    }
    return L"";
}

void ProvidersSettingsUi::fill_provider_defaults(HWND select, Session& session, const std::string& provider) {
    if (!select) {
        return;
    }
    std::vector<ui_kit::SelectItem> items;
    const auto rows = session.models_for_provider(provider, true);
    int cur = 0;
    const std::string def = session.provider_default_model_id(provider);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        ui_kit::SelectItem it;
        it.label = utf16(rows[i].display.empty() ? rows[i].id : rows[i].display);
        it.data = static_cast<LPARAM>(i + 1);
        items.push_back(it);
        if (rows[i].id == def) {
            cur = static_cast<int>(i);
        }
    }
    if (items.empty()) {
        items.push_back({is_api_provider(provider) ? L"(enable models below)" : L"(no enabled models)", 0});
    }
    ui_kit::select_set_items(select, items);
    ui_kit::select_set_index(select, cur);
}

void ProvidersSettingsUi::fill_active_models(Session& session) {
    std::vector<ui_kit::SelectItem> prov = {{L"OpenAI ChatGPT", 1},
                                            {L"OpenAI API", 2},
                                            {L"Claude Account", 3},
                                            {L"Claude API", 4}};
    ui_kit::select_set_items(active_prov_, prov);
    ui_kit::select_set_index(active_prov_, provider_index(session.settings.default_provider));

    const std::string pid = coerce_default_provider(session.settings.default_provider);
    std::vector<ui_kit::SelectItem> models;
    const auto rows = session.models_for_provider(pid, true);
    int cur = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        models.push_back({utf16(rows[i].display.empty() ? rows[i].id : rows[i].display),
                          static_cast<LPARAM>(i + 1)});
        if (rows[i].id == session.selected_model) {
            cur = static_cast<int>(i);
        }
    }
    if (models.empty()) {
        models.push_back({is_api_provider(pid) ? L"(enable API models in Settings)" : L"(connect / load models)", 0});
    }
    ui_kit::select_set_items(active_model_, models);
    ui_kit::select_set_index(active_model_, cur);
}

void ProvidersSettingsUi::fill_model_list(HWND list, Session& session, const std::string& provider,
                                          const std::wstring& filter, std::vector<std::string>* id_store) {
    if (!list || !id_store) {
        return;
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    id_store->clear();
    for (const auto& m : session.models_for_provider(provider, false)) {
        const std::wstring label = utf16(m.display.empty() ? m.id : m.display);
        const std::wstring blurb = model_blurb(m);
        std::wstring row = label;
        if (!blurb.empty()) {
            row += L"  —  ";
            row += blurb;
        }
        if (!session.is_model_enabled(provider, m.id) && blurb.empty()) {
            row += L"  —  Disabled";
        }
        if (!contains_ci(row, filter) && !contains_ci(utf16(m.id), filter)) {
            continue;
        }
        id_store->push_back(m.id);
        const int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str())));
        if (idx >= 0) {
            SendMessageW(list, LB_SETITEMDATA, idx, session.is_model_enabled(provider, m.id) ? 1 : 0);
        }
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void ProvidersSettingsUi::update_counts(Session& session) {
    auto set_count = [&](HWND h, const std::string& provider) {
        if (!h) {
            return;
        }
        const auto all = session.models_for_provider(provider, false);
        const auto en = session.models_for_provider(provider, true);
        std::wstring t = std::to_wstring(en.size()) + L" of " + std::to_wstring(all.size()) + L" models enabled";
        SetWindowTextW(h, t.c_str());
    };
    auto set_enabled = [&](HWND h, const std::string& provider) {
        if (!h) {
            return;
        }
        const auto all = session.models_for_provider(provider, false);
        const auto en = session.models_for_provider(provider, true);
        std::wstring t = L"Enabled " + std::to_wstring(en.size()) + L" of " + std::to_wstring(all.size()) + L".";
        SetWindowTextW(h, t.c_str());
    };
    set_count(oa_.count, "openai");
    set_count(oa_api_.count, "openai-api");
    set_count(cl_.count, "claude");
    set_count(cl_api_.count, "claude-api");
    set_enabled(oa_.enabled_lbl, "openai");
    set_enabled(oa_api_.enabled_lbl, "openai-api");
    set_enabled(cl_.enabled_lbl, "claude");
    set_enabled(cl_api_.enabled_lbl, "claude-api");
}

void ProvidersSettingsUi::set_expand(Expand e) {
    expand_ = e;
}

void ProvidersSettingsUi::paint_frame(HDC dc, const RECT& r) const {
    if (!dc || IsRectEmpty(&r)) {
        return;
    }
    fill_solid(dc, r, theme().surface);
    frame_rect_color(dc, r, theme().divider);
}

void ProvidersSettingsUi::paint_chrome(HDC dc) const {
    if (!visible_ || !dc) {
        return;
    }
    paint_frame(dc, oa_.frame);
    paint_frame(dc, oa_api_.frame);
    paint_frame(dc, cl_.frame);
    paint_frame(dc, cl_api_.frame);
    paint_frame(dc, active_frame_);
}

int ProvidersSettingsUi::place_card(CardHwnds& card, int x, int y, int w, bool connected, bool expanded) {
    const int pad = dip(parent_, 14);
    const int gap = dip(parent_, 8);
    const int row = dip(parent_, 28);
    const int btn_h = dip(parent_, 30);
    const int btn_w = dip(parent_, 128);
    int cy = y + pad;

    move_show(card.name, x + pad, cy, dip(parent_, 160), row);
    move_show(card.badge, x + pad + dip(parent_, 170), cy + dip(parent_, 4), dip(parent_, 110), dip(parent_, 20));
    cy += row + gap / 2;

    const int meta_h = dip(parent_, 34);
    move_show(card.meta, x + pad, cy, (std::max)(dip(parent_, 120), w / 2 - pad), meta_h);
    const int wide_w = btn_w + dip(parent_, 40);
    if (connected) {
        move_show(card.default_lbl, x + w / 2, cy - dip(parent_, 16), dip(parent_, 120), dip(parent_, 16));
        move_show(card.default_sel, x + w / 2, cy, dip(parent_, 160), btn_h);
        move_show(card.count, x + w / 2, cy + btn_h + 2, dip(parent_, 200), dip(parent_, 16));
        const int actions_y = cy + meta_h + dip(parent_, 18);
        int bx = x + w - pad - btn_w;
        move_show(card.secondary, bx, actions_y, btn_w, btn_h);
        bx -= btn_w + gap;
        move_show(card.refresh, bx, actions_y, btn_w, btn_h);
        bx -= btn_w + gap;
        move_show(card.manage, bx, actions_y, btn_w, btn_h);
        hide(card.primary);
    } else {
        hide(card.default_lbl);
        hide(card.default_sel);
        hide(card.count);
        hide(card.manage);
        hide(card.refresh);
        hide(card.secondary);
        move_show(card.primary, x + w - pad - wide_w, cy, wide_w, btn_h);
    }
    cy += meta_h;
    move_show(card.auth, x + pad, cy, w / 2, dip(parent_, 18));
    cy += connected ? btn_h + gap + dip(parent_, 18) : dip(parent_, 24);

    if (connected && expanded) {
        move_show(card.models_heading, x + pad, cy, dip(parent_, 220), row);
        move_show(card.models_hint, x + pad + dip(parent_, 200), cy + 4, dip(parent_, 320), dip(parent_, 18));
        move_show(card.search, x + w - pad - dip(parent_, 200), cy, dip(parent_, 200), btn_h);
        cy += row + gap;
        const int list_h = dip(parent_, 160);
        move_show(card.models, x + pad, cy, w - pad * 2, list_h);
        cy += list_h + gap;
        move_show(card.enabled_lbl, x + pad, cy, dip(parent_, 160), dip(parent_, 18));
        move_show(card.default_footer, x + w / 2 - dip(parent_, 80), cy - 4, dip(parent_, 160), btn_h);
        move_show(card.enable_all, x + w - pad - dip(parent_, 200), cy - 4, dip(parent_, 90), btn_h);
        move_show(card.disable_all, x + w - pad - dip(parent_, 100), cy - 4, dip(parent_, 90), btn_h);
        cy += btn_h + pad;
    } else {
        hide(card.models_heading);
        hide(card.models_hint);
        hide(card.search);
        hide(card.models);
        hide(card.enabled_lbl);
        hide(card.default_footer);
        hide(card.enable_all);
        hide(card.disable_all);
        cy += pad / 2;
    }

    card.frame = {x, y, x + w, cy};
    return cy + gap;
}

void ProvidersSettingsUi::layout(const RECT& area) {
    area_ = area;
    if (!visible_) {
        return;
    }
    const int x = area.left;
    int y = area.top;
    const int w = (std::max)(1, static_cast<int>(area.right - area.left));

    move_show(title_, x, y, w, dip(parent_, 24));
    y += dip(parent_, 28);
    move_show(subtitle_, x, y, w, dip(parent_, 18));
    y += dip(parent_, 28);

    const int oa_top = y;
    y = place_card(oa_, x, y, w, oa_connected_, expand_ == Expand::OpenAI && oa_connected_);
    oa_.frame.top = oa_top;

    const int oa_api_top = y;
    y = place_card(oa_api_, x, y, w, oa_api_connected_, expand_ == Expand::OpenAiApi && oa_api_connected_);
    oa_api_.frame.top = oa_api_top;

    const int cl_top = y;
    y = place_card(cl_, x, y, w, cl_connected_, expand_ == Expand::Claude && cl_connected_);
    cl_.frame.top = cl_top;

    const int cl_api_top = y;
    y = place_card(cl_api_, x, y, w, cl_api_connected_, expand_ == Expand::ClaudeApi && cl_api_connected_);
    cl_api_.frame.top = cl_api_top;

    const int act_top = y;
    const int pad2 = dip(parent_, 14);
    move_show(active_heading_, x + pad2, y + pad2, dip(parent_, 160), dip(parent_, 22));
    move_show(active_hint_, x + pad2 + dip(parent_, 140), y + pad2 + 4, w - dip(parent_, 180), dip(parent_, 18));
    y += dip(parent_, 42);
    move_show(active_prov_lbl_, x + pad2, y, dip(parent_, 80), dip(parent_, 16));
    move_show(active_model_lbl_, x + pad2 + dip(parent_, 240), y, dip(parent_, 80), dip(parent_, 16));
    y += dip(parent_, 18);
    move_show(active_prov_, x + pad2, y, dip(parent_, 220), dip(parent_, 30));
    move_show(active_model_, x + pad2 + dip(parent_, 240), y, dip(parent_, 240), dip(parent_, 30));
    y += dip(parent_, 44);
    active_frame_ = {x, act_top, x + w, y};
}

void ProvidersSettingsUi::refresh(Session& session) {
    if (!visible_) {
        return;
    }
    const auto oa = openai_provider_status(session.account.signed_in, session.account.email, session.account.plan,
                                           session.account.type);
    const auto oa_api = openai_api_provider_status();
    const auto cl = claude_provider_status();
    const auto cl_api = claude_api_provider_status();
    const ClaudeCodeSession cl_sess = claude_code_session_status(false);
    oa_connected_ = oa.connected;
    oa_api_connected_ = oa_api.connected;
    cl_connected_ = cl.connected;
    cl_api_connected_ = cl_api.connected;

    auto set_status = [](CardHwnds& card, const ProviderStatus& st, const std::wstring& product,
                         const std::wstring& detail_override) {
        SetWindowTextW(card.badge, st.connected ? L"Connected" : L"Not Connected");
        std::wstring meta = product;
        const std::wstring detail = detail_override.empty() ? utf16(st.detail) : detail_override;
        if (!detail.empty()) {
            meta += L"\r\n";
            meta += detail;
        }
        SetWindowTextW(card.meta, meta.c_str());
        std::wstring auth = L"Authentication: ";
        auth += utf16(st.auth_label.empty() ? (st.connected ? "Connected" : "Not connected") : st.auth_label);
        SetWindowTextW(card.auth, auth.c_str());
        EnableWindow(card.primary, st.connected ? FALSE : TRUE);
        EnableWindow(card.secondary, st.connected ? TRUE : FALSE);
    };

    set_status(oa_, oa, L"ChatGPT subscription", {});
    set_status(oa_api_, oa_api, L"OpenAI API (BYOK)", {});
    {
        std::wstring detail;
        if (cl_sess.logged_in && !cl_sess.email.empty()) {
            detail = utf16(cl_sess.email);
            if (!cl_sess.subscription.empty()) {
                detail += L" · ";
                detail += utf16(cl_sess.subscription);
            }
        }
        set_status(cl_, cl, L"Claude Code", detail);
    }
    set_status(cl_api_, cl_api, L"Anthropic API (BYOK)", {});

    const bool claude_cli = !discover_claude_cli().empty();
    EnableWindow(cl_.primary, (claude_cli && !cl.connected) ? TRUE : FALSE);

    fill_provider_defaults(oa_.default_sel, session, "openai");
    fill_provider_defaults(oa_.default_footer, session, "openai");
    fill_provider_defaults(oa_api_.default_sel, session, "openai-api");
    fill_provider_defaults(oa_api_.default_footer, session, "openai-api");
    fill_provider_defaults(cl_.default_sel, session, "claude");
    fill_provider_defaults(cl_.default_footer, session, "claude");
    fill_provider_defaults(cl_api_.default_sel, session, "claude-api");
    fill_provider_defaults(cl_api_.default_footer, session, "claude-api");
    fill_model_list(oa_.models, session, "openai", oa_filter_, &oa_model_ids_);
    fill_model_list(oa_api_.models, session, "openai-api", oa_api_filter_, &oa_api_model_ids_);
    fill_model_list(cl_.models, session, "claude", cl_filter_, &cl_model_ids_);
    fill_model_list(cl_api_.models, session, "claude-api", cl_api_filter_, &cl_api_model_ids_);
    fill_active_models(session);
    update_counts(session);

    if (!IsRectEmpty(&area_)) {
        layout(area_);
    }
    if (parent_) {
        InvalidateRect(parent_, &area_, FALSE);
    }
}

ProvidersSettingsAction ProvidersSettingsUi::handle_command(int id, WORD notify, Session& session, HWND owner) {
    if (!visible_) {
        return ProvidersSettingsAction::None;
    }

    HWND select_face = nullptr;
    switch (id) {
        case Cmd_ProvOaDefault:
            select_face = oa_.default_sel;
            break;
        case Cmd_ProvOaDefaultFooter:
            select_face = oa_.default_footer;
            break;
        case Cmd_ProvOaApiDefault:
            select_face = oa_api_.default_sel;
            break;
        case Cmd_ProvOaApiDefaultFooter:
            select_face = oa_api_.default_footer;
            break;
        case Cmd_ProvClDefault:
            select_face = cl_.default_sel;
            break;
        case Cmd_ProvClDefaultFooter:
            select_face = cl_.default_footer;
            break;
        case Cmd_ProvClApiDefault:
            select_face = cl_api_.default_sel;
            break;
        case Cmd_ProvClApiDefaultFooter:
            select_face = cl_api_.default_footer;
            break;
        case Cmd_ProvActiveProvider:
            select_face = active_prov_;
            break;
        case Cmd_ProvActiveModel:
            select_face = active_model_;
            break;
        default:
            break;
    }
    if (select_face && notify == BN_CLICKED) {
        ui_kit::select_handle_command(select_face, notify);
        return ProvidersSettingsAction::None;
    }

    auto apply_default_from_select = [&](HWND sel, const std::string& provider) -> bool {
        const int idx = ui_kit::select_get_index(sel);
        const auto rows = session.models_for_provider(provider, true);
        if (idx < 0 || idx >= static_cast<int>(rows.size())) {
            return false;
        }
        session.set_provider_default_model(provider, rows[static_cast<std::size_t>(idx)].id);
        return true;
    };

    if ((id == Cmd_ProvOaDefault || id == Cmd_ProvOaDefaultFooter) && notify == CBN_SELCHANGE) {
        return apply_default_from_select(id == Cmd_ProvOaDefault ? oa_.default_sel : oa_.default_footer, "openai")
                   ? ProvidersSettingsAction::PersistAndSync
                   : ProvidersSettingsAction::None;
    }
    if ((id == Cmd_ProvOaApiDefault || id == Cmd_ProvOaApiDefaultFooter) && notify == CBN_SELCHANGE) {
        return apply_default_from_select(id == Cmd_ProvOaApiDefault ? oa_api_.default_sel : oa_api_.default_footer,
                                         "openai-api")
                   ? ProvidersSettingsAction::PersistAndSync
                   : ProvidersSettingsAction::None;
    }
    if ((id == Cmd_ProvClDefault || id == Cmd_ProvClDefaultFooter) && notify == CBN_SELCHANGE) {
        return apply_default_from_select(id == Cmd_ProvClDefault ? cl_.default_sel : cl_.default_footer, "claude")
                   ? ProvidersSettingsAction::PersistAndSync
                   : ProvidersSettingsAction::None;
    }
    if ((id == Cmd_ProvClApiDefault || id == Cmd_ProvClApiDefaultFooter) && notify == CBN_SELCHANGE) {
        return apply_default_from_select(id == Cmd_ProvClApiDefault ? cl_api_.default_sel : cl_api_.default_footer,
                                         "claude-api")
                   ? ProvidersSettingsAction::PersistAndSync
                   : ProvidersSettingsAction::None;
    }
    if (id == Cmd_ProvActiveProvider && notify == CBN_SELCHANGE) {
        session.set_default_provider(provider_at_index(ui_kit::select_get_index(active_prov_)));
        fill_active_models(session);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvActiveModel && notify == CBN_SELCHANGE) {
        const int idx = ui_kit::select_get_index(active_model_);
        const std::string pid = coerce_default_provider(session.settings.default_provider);
        const auto rows = session.models_for_provider(pid, true);
        if (idx >= 0 && idx < static_cast<int>(rows.size())) {
            session.selected_model = rows[static_cast<std::size_t>(idx)].id;
            session.settings.selected_model = session.selected_model;
            session.set_provider_default_model(pid, session.selected_model);
            return ProvidersSettingsAction::PersistAndSync;
        }
        return ProvidersSettingsAction::None;
    }

    if (id == Cmd_ProvOaManage && notify == BN_CLICKED) {
        set_expand(expand_ == Expand::OpenAI ? Expand::None : Expand::OpenAI);
        return ProvidersSettingsAction::Relayout;
    }
    if (id == Cmd_ProvOaApiManage && notify == BN_CLICKED) {
        set_expand(expand_ == Expand::OpenAiApi ? Expand::None : Expand::OpenAiApi);
        return ProvidersSettingsAction::Relayout;
    }
    if (id == Cmd_ProvClManage && notify == BN_CLICKED) {
        set_expand(expand_ == Expand::Claude ? Expand::None : Expand::Claude);
        return ProvidersSettingsAction::Relayout;
    }
    if (id == Cmd_ProvClApiManage && notify == BN_CLICKED) {
        set_expand(expand_ == Expand::ClaudeApi ? Expand::None : Expand::ClaudeApi);
        return ProvidersSettingsAction::Relayout;
    }
    if (id == Cmd_ProvOaRefresh && notify == BN_CLICKED) {
        return ProvidersSettingsAction::RefreshOpenAI;
    }
    if (id == Cmd_ProvOaApiRefresh && notify == BN_CLICKED) {
        return ProvidersSettingsAction::RefreshOpenAiApi;
    }
    if (id == Cmd_ProvClRefresh && notify == BN_CLICKED) {
        return ProvidersSettingsAction::RefreshClaude;
    }
    if (id == Cmd_ProvClApiRefresh && notify == BN_CLICKED) {
        return ProvidersSettingsAction::RefreshClaudeApi;
    }
    if (id == Cmd_SetOaSignIn && notify == BN_CLICKED) {
        return ProvidersSettingsAction::OpenAISignIn;
    }
    if (id == Cmd_SetOaSignOut && notify == BN_CLICKED) {
        return ProvidersSettingsAction::OpenAISignOut;
    }
    if (id == Cmd_ProvOaApiConnect && notify == BN_CLICKED) {
        return ProvidersSettingsAction::OpenAiApiKey;
    }
    if (id == Cmd_ProvOaApiDisc && notify == BN_CLICKED) {
        return ProvidersSettingsAction::OpenAiApiDisconnect;
    }
    if (id == Cmd_SetClCode && notify == BN_CLICKED) {
        return ProvidersSettingsAction::ClaudeCodeLogin;
    }
    if (id == Cmd_SetClDisc && notify == BN_CLICKED) {
        return ProvidersSettingsAction::ClaudeDisconnect;
    }
    if ((id == Cmd_ProvClApiConnect || id == Cmd_SetClKey) && notify == BN_CLICKED) {
        return ProvidersSettingsAction::ClaudeApiKey;
    }
    if (id == Cmd_ProvClApiDisc && notify == BN_CLICKED) {
        return ProvidersSettingsAction::ClaudeApiDisconnect;
    }

    auto bulk_safe = [&](const std::string& provider, bool enable) {
        for (const auto& m : session.models_for_provider(provider, false)) {
            session.set_model_enabled(provider, m.id, enable);
        }
        if (!enable && !is_api_provider(provider)) {
            const auto all = session.models_for_provider(provider, false);
            if (!all.empty()) {
                session.set_model_enabled(provider, all.front().id, true);
            }
        }
        session.set_default_provider(session.settings.default_provider);
    };

    if (id == Cmd_ProvOaEnableAll && notify == BN_CLICKED) {
        bulk_safe("openai", true);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvOaDisableAll && notify == BN_CLICKED) {
        bulk_safe("openai", false);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvOaApiEnableAll && notify == BN_CLICKED) {
        bulk_safe("openai-api", true);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvOaApiDisableAll && notify == BN_CLICKED) {
        bulk_safe("openai-api", false);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvClEnableAll && notify == BN_CLICKED) {
        bulk_safe("claude", true);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvClDisableAll && notify == BN_CLICKED) {
        bulk_safe("claude", false);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvClApiEnableAll && notify == BN_CLICKED) {
        bulk_safe("claude-api", true);
        return ProvidersSettingsAction::PersistAndSync;
    }
    if (id == Cmd_ProvClApiDisableAll && notify == BN_CLICKED) {
        bulk_safe("claude-api", false);
        return ProvidersSettingsAction::PersistAndSync;
    }

    if ((id == Cmd_ProvOaSearch || id == Cmd_ProvOaApiSearch || id == Cmd_ProvClSearch || id == Cmd_ProvClApiSearch) &&
        (notify == EN_CHANGE || notify == EN_UPDATE)) {
        if (id == Cmd_ProvOaSearch) {
            oa_filter_ = get_text(oa_.search);
            fill_model_list(oa_.models, session, "openai", oa_filter_, &oa_model_ids_);
        } else if (id == Cmd_ProvOaApiSearch) {
            oa_api_filter_ = get_text(oa_api_.search);
            fill_model_list(oa_api_.models, session, "openai-api", oa_api_filter_, &oa_api_model_ids_);
        } else if (id == Cmd_ProvClSearch) {
            cl_filter_ = get_text(cl_.search);
            fill_model_list(cl_.models, session, "claude", cl_filter_, &cl_model_ids_);
        } else {
            cl_api_filter_ = get_text(cl_api_.search);
            fill_model_list(cl_api_.models, session, "claude-api", cl_api_filter_, &cl_api_model_ids_);
        }
        return ProvidersSettingsAction::None;
    }

    auto toggle_model = [&](HWND list, std::vector<std::string>& ids, const std::string& provider) {
        const int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(ids.size())) {
            return ProvidersSettingsAction::None;
        }
        const std::string& mid = ids[static_cast<std::size_t>(sel)];
        const bool on = session.is_model_enabled(provider, mid);
        if (on && !is_api_provider(provider) && session.models_for_provider(provider, true).size() <= 1) {
            SendMessageW(list, LB_SETCURSEL, -1, 0);
            return ProvidersSettingsAction::None;
        }
        session.set_model_enabled(provider, mid, !on);
        InvalidateRect(list, nullptr, TRUE);
        SendMessageW(list, LB_SETCURSEL, -1, 0);
        return ProvidersSettingsAction::PersistAndSync;
    };

    if ((id == Cmd_ProvOaModels || id == Cmd_ProvOaApiModels || id == Cmd_ProvClModels || id == Cmd_ProvClApiModels) &&
        (notify == LBN_SELCHANGE || notify == LBN_DBLCLK)) {
        if (id == Cmd_ProvOaModels) {
            return toggle_model(oa_.models, oa_model_ids_, "openai");
        }
        if (id == Cmd_ProvOaApiModels) {
            return toggle_model(oa_api_.models, oa_api_model_ids_, "openai-api");
        }
        if (id == Cmd_ProvClModels) {
            return toggle_model(cl_.models, cl_model_ids_, "claude");
        }
        return toggle_model(cl_api_.models, cl_api_model_ids_, "claude-api");
    }

    (void)owner;
    return ProvidersSettingsAction::None;
}

bool ProvidersSettingsUi::measure_item(MEASUREITEMSTRUCT* mi) const {
    if (!mi || !parent_) {
        return false;
    }
    if (mi->CtlID != Cmd_ProvOaModels && mi->CtlID != Cmd_ProvOaApiModels && mi->CtlID != Cmd_ProvClModels &&
        mi->CtlID != Cmd_ProvClApiModels) {
        return false;
    }
    mi->itemHeight = static_cast<UINT>(dip(parent_, 36));
    return true;
}

bool ProvidersSettingsUi::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di) {
        return false;
    }
    const std::vector<std::string>* ids = nullptr;
    if (di->CtlID == Cmd_ProvOaModels) {
        ids = &oa_model_ids_;
    } else if (di->CtlID == Cmd_ProvOaApiModels) {
        ids = &oa_api_model_ids_;
    } else if (di->CtlID == Cmd_ProvClModels) {
        ids = &cl_model_ids_;
    } else if (di->CtlID == Cmd_ProvClApiModels) {
        ids = &cl_api_model_ids_;
    } else {
        return false;
    }
    if (di->itemID >= ids->size()) {
        return true;
    }
    // Enabled state travels as LB_SETITEMDATA (1/0) from fill_model_list.
    fill_solid(di->hDC, di->rcItem, theme().surface);
    const bool checked = (SendMessageW(di->hwndItem, LB_GETITEMDATA, di->itemID, 0) != 0);
    const int s = dip(parent_, 14);
    const int gx = di->rcItem.left + dip(parent_, 8);
    const int gy = di->rcItem.top + (di->rcItem.bottom - di->rcItem.top - s) / 2;
    ui_kit::draw_checkbox_glyph(di->hDC, gx, gy, s, checked, false, false);

    wchar_t buf[256]{};
    SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, reinterpret_cast<LPARAM>(buf));
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, theme().text);
    HFONT old = font ? static_cast<HFONT>(SelectObject(di->hDC, font)) : nullptr;
    RECT tr = di->rcItem;
    tr.left = gx + s + dip(parent_, 10);
    tr.right -= dip(parent_, 8);
    DrawTextW(di->hDC, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old) {
        SelectObject(di->hDC, old);
    }
    return true;
}

}  // namespace scyllagpt
