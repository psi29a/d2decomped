// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

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
