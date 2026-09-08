#include "scyllagpt/ui_space.h"

#include <algorithm>

namespace scyllagpt {
namespace ui_space {

int dip(HWND hwnd, int v) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return MulDiv(v, static_cast<int>(dpi), 96);
}

FormMetrics metrics_for(HWND hwnd) {
    FormMetrics m;
    m.pad_outer = dip(hwnd, kPadOuterDip);
    m.pad_section = dip(hwnd, kPadSectionDip);
    m.pad_row = dip(hwnd, kPadRowDip);
    m.pad_tight = dip(hwnd, kPadTightDip);
    m.row_h = dip(hwnd, kRowHDip);
    m.label_w = dip(hwnd, kLabelWDip);
    m.btn_w = dip(hwnd, kBtnWDip);
    m.btn_h = dip(hwnd, kBtnHDip);
    return m;
}

RECT content_column(HWND hwnd, const RECT& area, int max_w_dip) {
    RECT r = area;
    const int max_w = dip(hwnd, max_w_dip);
    if (r.right - r.left > max_w) {
        r.right = r.left + max_w;
    }
    return r;
}

void place_labeled_row(HWND label, HWND field, const RECT& host, int y, const FormMetrics& m, int field_extra_w) {
    const int x0 = host.left + m.pad_outer;
    const int x1 = host.right - m.pad_outer;
    HWND dpi_hwnd = label ? label : field;
    // Label vertically centered against the field row (equal optical mid-line).
    const int label_h = dip(dpi_hwnd, 18);
    const int label_y = y + (std::max)(0, (m.row_h - label_h) / 2);
    if (label) {
        MoveWindow(label, x0, label_y, m.label_w, label_h, TRUE);
        ShowWindow(label, SW_SHOW);
    }
    if (field) {
        const int fx = x0 + m.label_w + m.pad_tight;
        int fw = x1 - fx;
        if (field_extra_w > 0 && field_extra_w < fw) {
            fw = field_extra_w;  // absolute width
        } else if (field_extra_w < 0) {
            fw += field_extra_w;  // reserve trailing space (e.g. a Browse / Save button)
        }
        MoveWindow(field, fx, y, (std::max)(fw, m.btn_w), m.row_h, TRUE);
        ShowWindow(field, SW_SHOW);
    }
}

}  // namespace ui_space
}  // namespace scyllagpt
