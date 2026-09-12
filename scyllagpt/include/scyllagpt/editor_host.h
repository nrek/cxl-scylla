#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "scyllagpt/language.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace scyllagpt {

struct EditorViewState {
    std::int64_t first_line = 0;
    std::int64_t anchor = 0;
    std::int64_t caret = 0;
    int x_offset = 0;
};

struct EditorSel {
    std::wstring text;
    int start_line = 1;  // 1-based
    int end_line = 1;
};

bool editor_register(HINSTANCE inst);
HWND editor_create(HWND parent, int control_id, HINSTANCE inst);

void editor_apply_chrome(HWND sci, HFONT mono, int font_size_dip, int dpi);
void editor_set_text(HWND sci, std::wstring_view text);
std::wstring editor_get_text(HWND sci);
void editor_set_readonly(HWND sci, bool ro);
void editor_clear_undo(HWND sci);

void editor_set_language(HWND sci, const std::wstring& path, bool large_file);
void editor_apply_indent(HWND sci, IndentInfo info);

void editor_save_view(HWND sci, EditorViewState* out);
void editor_restore_view(HWND sci, const EditorViewState& st);

bool editor_find_next(HWND sci, std::wstring_view query, bool case_sensitive = false);
bool editor_replace_next(HWND sci, std::wstring_view query, std::wstring_view replacement,
                         bool case_sensitive = false);
void editor_goto_line(HWND sci, int line_1based);
EditorSel editor_selection(HWND sci);

void editor_status(HWND sci, int* line_1based, int* col_1based);

void editor_undo(HWND sci);
void editor_redo(HWND sci);
void editor_cut(HWND sci);
void editor_copy(HWND sci);
void editor_paste(HWND sci);
void editor_select_all(HWND sci);
void editor_set_word_wrap(HWND sci, bool on);
void editor_set_whitespace(HWND sci, bool visible);
HWND editor_create_minimap(HWND parent, HWND primary, int control_id, HINSTANCE inst, int dpi);
void editor_sync_minimap(HWND primary, HWND minimap);

}  // namespace scyllagpt
