// SPDX-License-Identifier: GPL-3.0-or-later
// The quest clock — game.exe 1.14d, QUESTS\QUESTS.CPP (docs/research/re/quests.md
// "The quest clock").
#pragma once

#include <cstdint>

namespace d2d::rules {

// The game's frame (FUN_0052d870) counts game +0xa8 on and, on every 20th,
// steps the quest list's counter (FUN_00543e10: game +0x10f4, +0x14). A
// quest timer counts those steps, not frames.
struct QuestClock {
    static constexpr std::uint32_t kFrames = 20;   // game +0xa8 % 0x14
    std::uint32_t frame = 0;   // game +0xa8
    std::uint32_t count = 0;   // game +0x10f4, +0x14
    // A game frame. True: the quest timers step.
    bool frame_step() {
        if (++frame % kFrames != 0) return false;
        ++count;
        return true;
    }
};

// A timer of `ticks` (FUN_00543f10) is due at the count + ticks, and
// FUN_00543e10 runs it once the count is past that: on the (ticks + 1)th
// step. One its callback keeps going (anything but 1) is due again `ticks`
// on, so every ticks + 1 steps.
constexpr int quest_timer_steps(int ticks) { return ticks + 1; }

}  // namespace d2d::rules
