// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>

namespace d2d::rules {

// The frame's hover box (FUN_00503000) for a text `text_w` x `text_h` kept
// at (x, y) (FUN_00502280): text_w + 8 wide, text_h tall; centred, x is its
// middle, else its left; the left clamped to 0 .. screen_w - width; the
// bottom y + 2, at least text_h - 5, at most screen_h - 5. {left, top,
// right, bottom}.
[[nodiscard]] constexpr std::array<int, 4> hover_box(int x, int y, bool centred, int text_w, int text_h, int screen_w, int screen_h) {
    const int width = text_w + 8;
    const int left = std::min(std::max(centred ? x - width / 2 : x, 0), screen_w - width);
    int bottom = std::max(y + 2, text_h - 5);
    if (bottom > screen_h - 5) bottom = screen_h - 5;
    return { left, bottom - text_h, left + width, bottom };
}

// The skill tree's tooltip (FUN_004abf60 -> FUN_004eef00, 0x4ef3ce..0x4ef3fb):
// kept at the icon's left - 0x18 and its bottom + the text's height + 3,
// not centred: the box hangs under the icon.
[[nodiscard]] constexpr std::array<int, 4> skill_tree_tip_box(int icon_left, int icon_bottom, int text_w, int text_h, int screen_w, int screen_h) {
    return hover_box(icon_left - 0x18, icon_bottom + text_h + 3, false, text_w, text_h, screen_w, screen_h);
}

}  // namespace d2d::rules
