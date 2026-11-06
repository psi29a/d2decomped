// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace d2d::rules {

// The life / mana / stamina value the HUD shows (FUN_00496dd0, 0x496dd0):
// the globes, the stamina bar and their texts read it, not the stat. One
// slot a stat (16 bytes at 0x7bef30 + 0x10 n): the last value and its
// frame, the value a glide starts from and its frame. Values in 256ths;
// `frame` the client's (FUN_0044da90, 25 a second). A rise or fall under
// an eighth of max glides over 7..15 frames; a bigger one, and any life
// loss, shows at once. Checked against game.exe by tools/emu/globe_glide.py
// (tests/globe_glide_cases.inc).
struct Glide { int value = 0, value_frame = 0, from = 0, from_frame = 0; };

inline int glide(Glide& slot, bool life, int cur, int max, std::uint32_t frame) {
    if (max == 0) return 0;
    const int now = int(frame);
    if (frame < 2) slot.value = slot.from = cur;
    if (life && cur < slot.value) { slot.value = slot.from = cur; slot.value_frame = slot.from_frame = now; }
    if (slot.value != cur) {
        if (std::abs(slot.value - cur) < max / 8) { slot.from_frame = slot.value_frame; slot.from = cur == max ? cur : slot.value; }
        else { slot.from = cur; slot.from_frame = now; }
        slot.value = cur; slot.value_frame = now;
    }
    const int duration = std::clamp(slot.value_frame - slot.from_frame, 7, 15);
    int shown = cur;
    // x87 at Windows' 53-bit precision: a double, then _ftol (0x682fd0) truncates.
    if (cur > 0 && now - slot.value_frame < duration && slot.value != slot.from)
        shown = slot.from + int(double(now - slot.value_frame + 1) / duration * (slot.value - slot.from));
    return std::clamp(shown, 0, std::max(max, 0));
}

}  // namespace d2d::rules
