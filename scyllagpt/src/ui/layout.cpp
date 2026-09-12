#include "scyllagpt/layout.h"

#include <algorithm>
#include <limits>

namespace scyllagpt {
namespace {

int clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

}  // namespace

PaneLayout compute_panes(int client_w, int files_pref, int agent_pref, int history_pref, bool focus_editor,
                          int files_mode, int history_mode, int narrow_tab) {
    PaneLayout L;
    const int minF = 180;
    const int maxF = 600;
    const int minE = 400;
    const int minA = 300;
    const int minH = 180;
    const int maxH = 600;
    const int split = 4;

    L.narrow_tabs = !focus_editor && client_w < 860;
    L.show_editor = true;
    L.show_agent = true;
    L.show_files = false;
    L.show_history = false;

    if (focus_editor) {
        L.show_files = false;
        L.show_history = false;
        L.show_agent = true;
        L.show_editor = true;
    } else if (client_w >= 1280) {
        L.show_files = files_mode != 2;
        L.show_history = history_mode != 2;
    } else if (client_w >= 1080) {
        L.show_files = files_mode != 2;
        L.show_history = history_mode == 1;
    } else if (client_w >= 860) {
        L.show_files = files_mode == 1;
        L.show_history = history_mode == 1;
    } else {
        L.show_files = files_mode == 1;
        L.show_history = history_mode == 1;
        if (narrow_tab == 1) {
            L.show_editor = false;
        } else {
            L.show_agent = false;
        }
    }

    int n = 0;
    if (L.show_files) {
        ++n;
    }
    if (L.show_editor) {
        ++n;
    }
    if (L.show_agent) {
        ++n;
    }
    if (L.show_history) {
        ++n;
    }
    const int splits = std::max(0, n - 1);
    L.splitter = split;
    int avail = client_w - splits * split;
    if (avail < 1) {
        avail = 1;
    }

    auto take = [&](bool on, int pref, int mn, int mx = (std::numeric_limits<int>::max)()) {
        if (!on) {
            return 0;
        }
        return clamp(pref, mn, std::min(mx, std::max(mn, avail)));
    };

    L.files = take(L.show_files, files_pref > 0 ? files_pref : 300, minF, maxF);
    L.agent = take(L.show_agent, agent_pref > 0 ? agent_pref : 650, minA);
    L.history = take(L.show_history, history_pref > 0 ? history_pref : 300, minH, maxH);

    int used = (L.show_files ? L.files : 0) + (L.show_agent ? L.agent : 0) + (L.show_history ? L.history : 0);
    int remain = avail - used;
    if (L.show_editor) {
        if (remain < minE) {
            int deficit = minE - remain;
            auto shrink = [&](int& col, bool on, int mn) {
                if (deficit <= 0 || !on) {
                    return;
                }
                const int can = col - mn;
                const int cut = std::min(can, deficit);
                col -= cut;
                deficit -= cut;
            };
            shrink(L.history, L.show_history, minH);
            shrink(L.files, L.show_files, minF);
            shrink(L.agent, L.show_agent, minA);
            used = (L.show_files ? L.files : 0) + (L.show_agent ? L.agent : 0) + (L.show_history ? L.history : 0);
            remain = avail - used;
        }
        L.editor = std::max(remain, L.show_editor ? minE : 0);
        if (!L.show_editor) {
            L.editor = 0;
        }
    } else {
        L.editor = 0;
        // Give leftover to the remaining main pane.
        if (L.show_agent) {
            L.agent = std::max(L.agent, remain + L.agent);
        }
    }

    // If still overflowing, drop optional columns (never the last remaining pane).
    int total = (L.show_files ? L.files : 0) + (L.show_editor ? L.editor : 0) + (L.show_agent ? L.agent : 0) +
                (L.show_history ? L.history : 0) + splits * split;
    if (total > client_w && L.show_history) {
        L.show_history = false;
        L.history = 0;
        return compute_panes(client_w, files_pref, agent_pref, 0, focus_editor, files_mode, 2, narrow_tab);
    }
    if (total > client_w && L.show_files) {
        L.show_files = false;
        L.files = 0;
        return compute_panes(client_w, files_pref, agent_pref, history_pref, focus_editor, 2, history_mode, narrow_tab);
    }
    return L;
}

}  // namespace scyllagpt
