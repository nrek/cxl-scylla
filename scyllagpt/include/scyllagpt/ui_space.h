#pragma once

// Compact 4px spacing grid (authority: universal UI rebuild). Feature UIs must use these metrics.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scyllagpt {
namespace ui_space {

constexpr int kPadOuterDip = 16;     // outer inset from content host
constexpr int kPadSectionDip = 16;   // between major sections
constexpr int kPadRowDip = 12;       // between form rows
constexpr int kPadTightDip = 8;      // related controls
constexpr int kPadXsDip = 4;
constexpr int kRowHDip = 32;         // standard control height
constexpr int kCompactHDip = 28;
constexpr int kLargeHDip = 36;
constexpr int kLabelWDip = 132;      // form label column
constexpr int kBtnWDip = 104;
constexpr int kBtnHDip = 28;
constexpr int kNavWDip = 176;        // settings left nav
constexpr int kNavRowHDip = 32;
constexpr int kPanelTabHDip = 32;
constexpr int kPanelToolHDip = 28;
constexpr int kMinTerminalHDip = 140;
constexpr int kDefaultTerminalHDip = 240;
constexpr int kPageHeaderTitleHDip = 22;
constexpr int kPageHeaderDescHDip = 18;
constexpr int kContentMaxWDip = 760;  // reading-width cap for settings forms

struct FormMetrics {
    int pad_outer = 0;
    int pad_section = 0;
    int pad_row = 0;
    int pad_tight = 0;
    int row_h = 0;
    int label_w = 0;
    int btn_w = 0;
    int btn_h = 0;
};

FormMetrics metrics_for(HWND hwnd);

// Place a left label + right field on one row inside |host|.
void place_labeled_row(HWND label, HWND field, const RECT& host, int y, const FormMetrics& m, int field_extra_w = 0);

// Cap a settings host to reading width. Forms stretched across an ultrawide window put the field
// column a screen away from its label; every panel narrows through here instead of hand-rolling it.
RECT content_column(HWND hwnd, const RECT& area, int max_w_dip = kContentMaxWDip);

int dip(HWND hwnd, int v);

}  // namespace ui_space
}  // namespace scyllagpt
