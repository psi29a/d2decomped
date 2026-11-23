// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of hud_glide.hpp's functions; their notes are in the header.
#include "hud_glide.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace d2d::rules {

int glide(Glide& slot, bool life, int cur, int max, std::uint32_t frame) {
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

}
