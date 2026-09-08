#include "scyllagpt/ui_gallery.h"

#include "scyllagpt/theme.h"
#include "scyllagpt/ui_kit.h"
#include "scyllagpt/ui_space.h"

#include <algorithm>

namespace scyllagpt {
namespace {

int dip(HWND hwnd, int v) {
    return MulDiv(v, static_cast<int>(GetDpiForWindow(hwnd ? hwnd : GetDesktopWindow())), 96);
}

}  // namespace

bool UiGallery::create(HWND parent, HINSTANCE inst, HFONT font) {
    if (!parent || heading_) {
        return heading_ != nullptr;
    }
    parent_ = parent;
    font_ = font;

    heading_ = ui_kit::create_static(parent, inst, IdHeading, L"UI Gallery", font, false);
    desc_ = ui_kit::create_static(parent, inst, IdDesc, L"Dev samples for Scylla ui_kit controls.", font, true);
    btn_primary_ = ui_kit::create_button(parent, inst, IdBtnPrimary, L"Primary", ui_kit::ButtonKind::Primary, font);
    btn_secondary_ =
        ui_kit::create_button(parent, inst, IdBtnSecondary, L"Secondary", ui_kit::ButtonKind::Secondary, font);
    btn_ghost_ = ui_kit::create_button(parent, inst, IdBtnGhost, L"Ghost", ui_kit::ButtonKind::Ghost, font);
    btn_danger_ = ui_kit::create_button(parent, inst, IdBtnDanger, L"Danger", ui_kit::ButtonKind::Danger, font);
    field_ = ui_kit::create_text_field(parent, inst, IdField, font);
    document_ = ui_kit::create_markdown_view(parent, inst, IdDocument, font);
    ui_kit::set_markdown(document_, L"# Markdown document\n**Bold**, _italic_ and `inline code`.\n- First item\n- Second item\n```cpp\nconst auto answer = 42;\n```\n[Example link](https://example.com)");
    SetWindowTextW(field_, L"Text field");
    select_ = ui_kit::create_select(parent, inst, IdSelect, font);
    ui_kit::select_set_items(select_, {{L"Option A", 0}, {L"Option B", 1}, {L"Option C", 2}});
    ui_kit::select_set_index(select_, 0);
    check_ = ui_kit::create_checkbox(parent, inst, IdCheck, L"Checkbox", font);
    sw_ = ui_kit::create_switch(parent, inst, IdSwitch, L"Switch", font);
    empty_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_OWNERDRAW, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IdEmpty)), inst, nullptr);
    ui_kit::apply_control_chrome(empty_, font);
    btn_close_ = ui_kit::create_button(parent, inst, IdClose, L"Close gallery", ui_kit::ButtonKind::Secondary, font);

    hide_all();
    return true;
}

void UiGallery::destroy() {
    hide_all();
    heading_ = nullptr;
}

void UiGallery::hide_all() {
    visible_ = false;
    for (HWND h : {heading_, desc_, btn_primary_, btn_secondary_, btn_ghost_, btn_danger_, field_, select_, check_, sw_,
                    empty_, btn_close_, document_}) {
        if (h) {
            ShowWindow(h, SW_HIDE);
        }
    }
}

void UiGallery::show(bool visible) {
    visible_ = visible;
    if (!visible) {
        hide_all();
        return;
    }
    for (HWND h : {heading_, desc_, btn_primary_, btn_secondary_, btn_ghost_, btn_danger_, field_, select_, check_, sw_,
                    empty_, btn_close_, document_}) {
        if (h) {
            ShowWindow(h, SW_SHOW);
        }
    }
}

void UiGallery::layout(const RECT& area) {
    if (!visible_ || !parent_) {
        return;
    }
    const auto m = ui_space::metrics_for(parent_);
    int y = area.top + m.pad_outer;
    const int x0 = area.left + m.pad_outer;
    const int w = (std::max)(1, static_cast<int>(area.right - m.pad_outer - x0));

    MoveWindow(heading_, x0, y, w, dip(parent_, ui_space::kPageHeaderTitleHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderTitleHDip) + m.pad_tight;
    MoveWindow(desc_, x0, y, w, dip(parent_, ui_space::kPageHeaderDescHDip), TRUE);
    y += dip(parent_, ui_space::kPageHeaderDescHDip) + m.pad_section;

    int x = x0;
    for (HWND h : {btn_primary_, btn_secondary_, btn_ghost_, btn_danger_}) {
        MoveWindow(h, x, y, m.btn_w, m.btn_h, TRUE);
        x += m.btn_w + m.pad_tight;
    }
    y += m.btn_h + m.pad_row;

    MoveWindow(field_, x0, y, (std::min)(w, dip(parent_, 280)), m.row_h, TRUE);
    y += m.row_h + m.pad_row;
    MoveWindow(select_, x0, y, (std::min)(w, dip(parent_, 280)), m.row_h, TRUE);
    y += m.row_h + m.pad_row;
    MoveWindow(check_, x0, y, dip(parent_, 160), m.row_h, TRUE);
    MoveWindow(sw_, x0 + dip(parent_, 180), y, dip(parent_, 160), m.row_h, TRUE);
    y += m.row_h + m.pad_section;

    MoveWindow(empty_, x0, y, w, dip(parent_, 100), TRUE);
    InvalidateRect(empty_, nullptr, TRUE);
    y += dip(parent_, 100) + m.pad_row;
    MoveWindow(document_, x0, y, w, dip(parent_, 80), TRUE);
    y += dip(parent_, 80) + m.pad_row;
    MoveWindow(btn_close_, x0, y, m.btn_w + 40, m.btn_h, TRUE);
}

bool UiGallery::handle_command(WORD id, WORD notify) {
    if (!visible_) {
        return false;
    }
    if (id == IdSelect && ui_kit::select_handle_command(select_, notify)) {
        return true;
    }
    if (id == IdClose) {
        show(false);
        return true;
    }
    if (id >= IdBtnPrimary && id <= IdClose) {
        return true;
    }
    return false;
}

bool UiGallery::draw_item(const DRAWITEMSTRUCT* di, HFONT font) const {
    if (!di || di->CtlType != ODT_STATIC || di->hwndItem != empty_) {
        return false;
    }
    ui_kit::paint_empty_state(di->hDC, di->rcItem, font, font, L"Empty state",
                              L"Shown when a list has nothing to display.");
    return true;
}

}  // namespace scyllagpt
