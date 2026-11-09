// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace d2d::rules {

// When the automap takes in what's been drawn (FUN_00459020, each client
// frame): once the player has moved 80 client px from where it last did,
// by an octagon distance on the unit's client px (FUN_00620650 /
// FUN_006206b0): the larger axis twice plus the smaller, halved, past
// 0x4f. Then (last_x, last_y) moves there. The last spot starts at (0, 0)
// (0x7a51fc / 0x7a51f4), so the first frame in a game reveals.
struct AutomapStep { int last_x = 0, last_y = 0; };

constexpr bool automap_moved(AutomapStep& step, int pixel_x, int pixel_y) {
    const int across = step.last_x > pixel_x ? step.last_x - pixel_x : pixel_x - step.last_x;
    const int down = step.last_y > pixel_y ? step.last_y - pixel_y : pixel_y - step.last_y;
    const int octagon = down < across ? down + across * 2 : across + down * 2;
    if (octagon / 2 <= 0x4f) return false;
    step.last_x = pixel_x;
    step.last_y = pixel_y;
    return true;
}

}  // namespace d2d::rules
