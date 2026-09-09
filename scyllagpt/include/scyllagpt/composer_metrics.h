#pragma once

// Composer sizing: how tall the input box should be for the text it currently holds.

#include <algorithm>

namespace scyllagpt {

constexpr int kComposerMinLines = 3;
constexpr int kComposerMaxLines = 12;

// Visible text lines for a composer holding `content_lines` wrapped lines. Below the floor the
// box keeps its resting height; above the ceiling the RichEdit scrolls instead of growing.
inline int composer_visible_lines(int content_lines, int min_lines = kComposerMinLines,
                                  int max_lines = kComposerMaxLines) {
    if (max_lines < min_lines) max_lines = min_lines;
    return std::clamp(content_lines, min_lines, max_lines);
}

}  // namespace scyllagpt
