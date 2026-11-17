// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::rules {

// FUN_00470390 / FUN_00470c30: AnimRate * 16 in an 8.8 counter,
// advanced once per game tick. Kinds 3 / 8 loop without LoopWaitTime;
// kind 4 (a cast overlay) ends at the first wrap. Only kind 6 waits.
// -1 means the one-shot ended, or there are no drawable frames.
constexpr int overlay_frame(std::uint32_t elapsed_ms, int rate, int frames, bool once = false) {
    if (frames <= 0 || rate < 0) return -1;
    const auto ticks = std::uint64_t(elapsed_ms / 40u);
    auto frame = ticks * std::uint32_t(rate) / 16u;
    if (once && frame >= std::uint32_t(frames)) return -1;
    // The original subtracts only one length per tick. A faster-than-
    // one-loop rate can leave the counter past the drawable frame range.
    if (!once && std::uint32_t(rate) > std::uint64_t(frames) * 16u) {
        frame = ticks * (std::uint32_t(rate) - std::uint64_t(frames) * 16u) / 16u;
        return frame < std::uint32_t(frames) ? int(frame) : -1;
    }
    return int(frame % std::uint32_t(frames));
}

// A skill start's cast overlay (FUN_004c6140): its castoverlay, or its ItemCastOverlay when cast
// from an item and that is set (row > 0); played (FUN_00470390 kind 2, once) when the row is in
// the table (0 < row) and the caster has no overlay of that row yet (FUN_0046e100). `shown` is
// the caster's one-shot, `shown_ms` since it began, `rate` / `frames` that row's.
constexpr int cast_overlay_row(int cast_row, int item_row, bool from_item) {
    const int row = from_item && item_row > 0 ? item_row : cast_row;
    return row > 0 ? row : -1;
}
constexpr bool cast_overlay_starts(int row, int shown, std::uint32_t shown_ms, int rate, int frames) {
    return row > 0 && !(row == shown && overlay_frame(shown_ms, rate, frames, true) >= 0);
}

// A unit's kind-8 overlays: a state in the `aura` list (States.txt bit 1,
// data tables +0xd0) makes its overlay1 / overlay2 so (FUN_004d9920), one
// state's shown at a time. In the unit's list order, newest first
// (FUN_00470390 puts a new one at the head, +0x2c). `count` is +8, `shown`
// +0x3c. The algorithm is tools/emu/engine_overlay_frames.py's model,
// checked there against game.exe.
struct AuraOverlay { int state = 0, row = 0; bool shown = false; int count = 0; };
// FUN_0046def0, a new one at the head: the first other with its state lends
// it its shown and count; else, past one of another state that's shown, it
// starts hidden; else shown from 0.
inline void aura_overlay_made(std::vector<AuraOverlay>& list, AuraOverlay made) {
    made.shown = true; made.count = 0;
    for (const auto& other : list) {
        if (other.state == made.state) { made.shown = other.shown; made.count = other.count; break; }
        if (other.shown) { made.shown = false; break; }
    }
    list.insert(list.begin(), made);
}
// FUN_0046df40 from state `from`: its overlays hidden (count -1), then those
// of the next higher state shown from 0, or the lowest's when none is higher
// (0 counts as none). The unit's state off / on around it changes nothing here.
inline void aura_overlays_rotate(std::span<AuraOverlay> list, int from) {
    int next = 0, low = 0;
    for (auto& overlay : list) {
        if (overlay.state == from) { overlay.shown = false; overlay.count = -1; }
        if ((next == 0 || overlay.state < next) && overlay.state > from) next = overlay.state;
        if (low == 0 || overlay.state < low) low = overlay.state;
    }
    if (next == 0) next = low;
    for (auto& overlay : list) if (overlay.state == next) { overlay.shown = true; overlay.count = 0; }
}
// One client tick of them (FUN_00470c30 case 8, in list order): one whose
// state is off is removed (FUN_0046e040, which rotates from its state); a
// shown one counts to 50, then rotates.
template <class Active> void aura_overlays_tick(std::vector<AuraOverlay>& list, Active&& active) {
    for (std::size_t index = 0; index < list.size();) {
        if (!active(list[index].state)) {
            const int from = list[index].state;
            list.erase(list.begin() + std::ptrdiff_t(index));
            aura_overlays_rotate(list, from);
            continue;
        }
        if (list[index].shown) {
            if (list[index].count < 50) ++list[index].count;
            else aura_overlays_rotate(list, list[index].state);
        }
        ++index;
    }
}

// A quest balloon begins when its alert appears, independent of the NPC's
// current animation and the other NPCs' balloons.
struct OverlayClock {
    std::uint32_t start = 0;
    bool active = false;

    void set(bool shown, std::uint32_t now) {
        if (shown && !active) start = now;
        active = shown;
    }
};

}  // namespace d2d::rules
