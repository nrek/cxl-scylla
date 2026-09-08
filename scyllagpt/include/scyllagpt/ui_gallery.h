#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {

// Dev-only control gallery for ui_kit samples.
class UiGallery {
public:
    enum : UINT {
        IdHost = 3800,
        IdBtnPrimary,
        IdBtnSecondary,
        IdBtnGhost,
        IdBtnDanger,
        IdField,
        IdSelect,
        IdCheck,
        IdSwitch,
        IdClose,
        IdHeading,
        IdDesc,
        IdEmpty,
        IdDocument,
    };

    bool create(HWND parent, HINSTANCE inst, HFONT font);
    void destroy();
    void show(bool visible);
    void layout(const RECT& area);
    bool visible() const { return visible_; }
    bool handle_command(WORD id, WORD notify);
    bool draw_item(const DRAWITEMSTRUCT* di, HFONT font) const;

private:
    void hide_all();

    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    bool visible_ = false;

    HWND heading_ = nullptr;
    HWND desc_ = nullptr;
    HWND btn_primary_ = nullptr;
    HWND btn_secondary_ = nullptr;
    HWND btn_ghost_ = nullptr;
    HWND btn_danger_ = nullptr;
    HWND field_ = nullptr;
    HWND select_ = nullptr;
    HWND check_ = nullptr;
    HWND sw_ = nullptr;
    HWND empty_ = nullptr;
    HWND btn_close_ = nullptr;
    HWND document_ = nullptr;
};

}  // namespace scyllagpt
