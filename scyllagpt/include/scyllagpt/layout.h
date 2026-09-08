#pragma once

namespace scyllagpt {

struct PaneLayout {
    int files = 0;
    int editor = 0;
    int agent = 0;
    int history = 0;
    int splitter = 4;
    bool show_files = false;
    bool show_editor = true;
    bool show_agent = true;
    bool show_history = false;
    bool narrow_tabs = false;
};

// client_w is DIPs (96-DPI pixels). files_mode/history_mode: 0 auto, 1 force on, 2 force off.
// narrow_tab: 0 editor, 1 agent (only used when client_w < 860).
// Terminal band height is computed outside this function (settings.terminal_h when visible).
PaneLayout compute_panes(int client_w, int files_pref, int agent_pref, int history_pref, bool focus_editor,
                          int files_mode, int history_mode, int narrow_tab);

}  // namespace scyllagpt
