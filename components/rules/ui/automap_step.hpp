// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace d2d::rules {

// When the automap takes in what's been drawn (FUN_00459020, each client
// frame): once the player has moved 80 client px from where it last did,
// by an octagon distance on the unit's client px (FUN_00620650 /
// FUN_006206b0): the larger axis twice plus the smaller, halved, past
// 0x4f. Then (last_x, last_y) moves there. The last spot starts at (0, 0)
// (0x7a51fc / 0x7a51f4), so the first frame in a game reveals.
// `pause` is 0x7a51a4: automap_pause sets it, automap_frame counts it down.
struct AutomapStep { int last_x = 0, last_y = 0, pause = 0; };

constexpr bool automap_moved(AutomapStep& step, int pixel_x, int pixel_y) {
    const int across = step.last_x > pixel_x ? step.last_x - pixel_x : pixel_x - step.last_x;
    const int down = step.last_y > pixel_y ? step.last_y - pixel_y : pixel_y - step.last_y;
    const int octagon = down < across ? down + across * 2 : across + down * 2;
    if (octagon / 2 <= 0x4f) return false;
    step.last_x = pixel_x;
    step.last_y = pixel_y;
    return true;
}

// FUN_00459140: a unit put somewhere at once (FUN_004654c0: S->C 0x15's
// reassign, FUN_00460f10, FUN_004c8b80) sets 0x7a51a4 to 2.
constexpr void automap_pause(AutomapStep& step) { step.pause = 2; }

// One client game frame's FUN_00459020 (from FUN_0044c790): while paused
// it counts down and takes nothing in, the last spot kept; else
// automap_moved.
constexpr bool automap_frame(AutomapStep& step, int pixel_x, int pixel_y) {
    if (step.pause != 0) {
        --step.pause;
        return false;
    }
    return automap_moved(step, pixel_x, pixel_y);
}

}  // namespace d2d::rules
