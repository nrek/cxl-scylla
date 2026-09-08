#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <vector>

namespace scyllagpt {
inline int chat_row_at_point(HWND list, POINT point, const std::vector<std::wstring>& groups) {
    RECT client{}; GetClientRect(list, &client);
    if (!PtInRect(&client, point)) return -1;
    const auto hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
    if (HIWORD(hit)) return -1;
    const int index = LOWORD(hit);
    if (index >= SendMessageW(list, LB_GETCOUNT, 0, 0)) return -1;
    RECT row{};
    if (SendMessageW(list, LB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&row)) == LB_ERR) return -1;
    if (index < static_cast<int>(groups.size()) && !groups[index].empty())
        row.top += MulDiv(26, GetDpiForWindow(list), 96);
    return PtInRect(&row, point) ? index : -1;
}
// groups is owned by Ui and outlives this child window. Keyboard selection is
// deliberately left to the list box; only mouse hits require content geometry.
inline LRESULT CALLBACK chat_list_mouse_guard(HWND list, UINT msg, WPARAM wp, LPARAM lp,
                                             UINT_PTR id, DWORD_PTR data) {
    const auto* groups = reinterpret_cast<const std::vector<std::wstring>*>(data);
    if (groups && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN ||
                   msg == WM_RBUTTONDBLCLK || msg == WM_LBUTTONUP || (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON)))) {
        if (chat_row_at_point(list, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, *groups) < 0) {
            if (msg == WM_LBUTTONUP && GetCapture() == list) ReleaseCapture();
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(list, chat_list_mouse_guard, id);
    return DefSubclassProc(list, msg, wp, lp);
}
} // namespace scyllagpt
