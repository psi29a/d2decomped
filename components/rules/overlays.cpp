// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of overlays.hpp's functions; their notes are in the header.
#include "overlays.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::rules {

void add_one_shot(OneShots& list, int row, std::uint32_t start) {
    if (row < 0) return;
    if (row < 0x8c || (row > 0x8e && row != 0x9e))
        if (const auto found = std::ranges::find(list, row, &OneShot::row); found != list.end()) list.erase(found);
    list.insert(list.begin(), OneShot{ row, start });
}

bool cast_overlay_starts(int row, const OneShots& shown, std::uint32_t now_ms, int rate, int frames) {
    return row > 0 && std::ranges::none_of(shown, [&](const OneShot& shot) { return shot.row == row && overlay_frame(now_ms - shot.start, rate, frames, true) >= 0; });
}

void aura_overlay_made(std::vector<AuraOverlay>& list, AuraOverlay made) {
    made.shown = true; made.count = 0;
    for (const auto& other : list) {
        if (other.state == made.state) { made.shown = other.shown; made.count = other.count; break; }
        if (other.shown) { made.shown = false; break; }
    }
    list.insert(list.begin(), made);
}

void aura_overlays_rotate(std::span<AuraOverlay> list, int from) {
    int next = 0, low = 0;
    for (auto& overlay : list) {
        if (overlay.state == from) { overlay.shown = false; overlay.count = -1; }
        if ((next == 0 || overlay.state < next) && overlay.state > from) next = overlay.state;
        if (low == 0 || overlay.state < low) low = overlay.state;
    }
    if (next == 0) next = low;
    for (auto& overlay : list) if (overlay.state == next) { overlay.shown = true; overlay.count = 0; }
}

}
