// SPDX-License-Identifier: GPL-3.0-or-later
// A menu box's frame: FUN_00452e50 draws UI\menu\boxpieces (0x7a2798, loaded
// at 0x456d24) round the rectangle {left, top, last_x, last_y}, each cel by its
// bottom-left (FUN_004f6480, draw mode 5). The NPC windows (FUN_004b8100, a
// window whose +0xc is 0) and Configure Controls draw it.
#pragma once

namespace d2d::rules {

struct BoxCel { int frame = 0, left = 0, bottom = 0; };

// In draw order: the top edge (frames 2..7), the bottom edge (0x10..0x15), the
// left side (10..12), the right side (0xd..0xf), one counter running across all
// four; then the corners 0, 1 (top) and 8, 9 (bottom).
template <typename Out> constexpr void box_piece_cels(int left, int top, int last_x, int last_y, Out&& out) {
    const int right = last_x + 1, bottom = last_y + 1;
    int piece = 0;
    for (int along = left + 12; along < right - 13; along += 12) { ++piece; out(BoxCel{ piece % 6 + 2, along, top + 12 }); }
    for (int along = left + 13; along < right - 13; along += 12) { ++piece; out(BoxCel{ piece % 6 + 0x10, along, bottom + 10 }); }
    for (int down = top + 0x16; down < bottom - 1; down += 12) { ++piece; out(BoxCel{ piece % 3 + 10, left - 4, down }); }
    for (int down = top + 24; down < bottom - 1; down += 12) { ++piece; out(BoxCel{ piece % 3 + 0xd, right - 7, down }); }
    out(BoxCel{ 0, left, top + 12 });
    out(BoxCel{ 1, right - 12, top + 12 });
    out(BoxCel{ 8, left, bottom + 1 });
    out(BoxCel{ 9, right - 12, bottom + 1 });
}

}  // namespace d2d::rules
