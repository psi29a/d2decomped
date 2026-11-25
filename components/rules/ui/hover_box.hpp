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

// The span a monster's bar centres in (FUN_00454880, text_area_span): the screen, its right half while a left
// panel is up (UI 2, 0xf, 0x14, 0x16, 0x1d, 0x24, 0x25), its left half with the inventory or skill tree (UI 1, 4); none
// while the store, stash, cube (UI 0xc, 0x19, 0x1a; 0x17, 0x1b, 0x1c too) is up, or with both halves taken. {left, right}.
[[nodiscard]] constexpr std::array<int, 2> monster_bar_span(int screen_w, bool left_panel, bool right_panel, bool covered) {
    if (covered) return { 0, 0 };
    return { left_panel ? screen_w / 2 : 0, right_panel ? screen_w / 2 : screen_w };
}

// The hovered monster's bar (FUN_00454ad0 at 0x454d79..0x454d96): kept at the span's middle less half the name's
// width, y 0x20, not centred (FUN_005022f0), so hover_box puts its bottom at 0x22.
[[nodiscard]] constexpr std::array<int, 4> monster_bar_box(const std::array<int, 2>& span, int text_w, int text_h, int screen_w, int screen_h) {
    constexpr int kBarY = 0x20;
    return hover_box(((span[0] + span[1]) >> 1) - (text_w >> 1), kBarY, false, text_w, text_h, screen_w, screen_h);
}

}  // namespace d2d::rules
