// SPDX-License-Identifier: GPL-3.0-or-later
// Where the automap scrolls: its cels and marks draw at (px / div -
// scroll.x, py / div - scroll.y) (FUN_00459700, after FUN_0045b440 and
// FUN_00457640; docs/research/re/automap.md "Drawing"). Checked against
// game.exe in tools/emu/automap_scroll.py. And which units the view
// draws, which is what puts an object's icon on the map.
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

// A client px coordinate's tile (FUN_00643340: 2y + x for x, 2y - x for
// y, over 160; one less below zero, an exact multiple too).
constexpr int client_tile(int value) { return value < 0 ? value / 160 - 1 : value / 160; }

// Whether a unit at client px (unit_x, unit_y) lands in the view's unit
// grid, the draw list that sets its flag 0x10000000 and so its automap
// icon (FUN_004ddb70 -> FUN_004dd7c0 -> FUN_004dc7b0). `view_x` /
// `view_y`: the view's top left, the player less half the view
// (FUN_0044c990: W / 2, (H - 40) / 2); `width` x `height` the view, W x
// (H - 40) (FUN_00476000). The grid is width / 160 + 11 + height / 80 + 11
// tiles square (FUN_004dce60), from the view's tile less 3 in x and less
// width / 160 + 11 in y (FUN_004ddb70).
constexpr bool in_unit_grid(int view_x, int view_y, int width, int height, int unit_x, int unit_y) {
    const int columns = width / 160 + 11, size = columns + height / 80 + 11;
    const int cell_x = client_tile(2 * unit_y + unit_x) - (client_tile(2 * view_y + view_x) - 3);
    const int cell_y = client_tile(2 * unit_y - unit_x) - (client_tile(2 * view_y - view_x) - columns);
    return cell_x >= 0 && cell_y >= 0 && cell_x < size && cell_y < size;
}

// Whether a room (act tiles) has its units put in that grid: its client
// px box (FUN_00619c40, corners by FUN_006433c0: (x - y) * 80 - 80,
// (x + y) * 40 + 80) meets the view's, widened 200 left and 400 down
// (FUN_004dd7c0).
constexpr bool room_in_view(int view_x, int view_y, int width, int height, int room_x, int room_y, int room_width, int room_height) {
    const int left = (room_x - room_y - room_height) * 80 - 80, top = (room_x + room_y) * 40 + 80;
    const int right = (room_x + room_width - room_y) * 80 - 80, bottom = (room_x + room_width + room_y + room_height) * 40 + 80;
    return left <= view_x + width && top <= view_y + height + 400 && view_x - 200 <= right && view_y <= bottom;
}

}  // namespace d2d::rules
