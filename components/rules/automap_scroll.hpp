// SPDX-License-Identifier: GPL-3.0-or-later
// Where the automap scrolls: its cels and marks draw at (px / div -
// scroll.x, py / div - scroll.y) (FUN_00459700, after FUN_0045b440 and
// FUN_00457640; docs/research/re/automap.md "Drawing"). Checked against
// game.exe in tools/emu/automap_scroll.py.
#pragma once

namespace d2d::rules {

struct AutomapScroll { int x, y; };

// `client_x` / `client_y`: the player's client px; `width` x `height` the
// screen; `mini` the small map (divisor 20) in its corner, `on_left` the
// left one, `portraits` up; `side` 1 / 2 a side panel open (FUN_0045ae90),
// which moves the full map a quarter screen.
constexpr AutomapScroll automap_scroll(int client_x, int client_y, int width, int height, bool mini, bool on_left, bool portraits,
                                       int side) {
    const int divisor = mini ? 20 : 10;
    const int box_x = on_left ? 0 : width * 2 / 3, box_y = on_left ? (portraits ? 0x60 : 0) : 0x4e;   // FUN_00457520
    const int offset_x = mini ? width / 3 - box_x - 0x10 : 0, offset_y = mini ? height / 3 - box_y - 0x10 : 0;   // 0x7a5188 / 0x7a518c
    const int shift = mini ? 0 : side == 1 ? -(width / 4) : side == 2 ? width / 4 : 0;
    // The view's top left (0x7a520c / 0x7a5208): the player less half the
    // screen, y 16 lower; then less half the screen again (0x7a5220 = width,
    // 0x7a521c = height - 40) over the divisor. Full size the player's cell
    // lands on their feet, (400, 294) at 800 x 600.
    const int view_left = client_x - width / 2, view_top = client_y - height / 2 + 0x10;
    return { view_left / divisor - width / 2 - shift + 0x28 + offset_x, view_top / divisor - (height - 40) / 2 + 0xf + offset_y };
}

}  // namespace d2d::rules
