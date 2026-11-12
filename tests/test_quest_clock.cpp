// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The quest clock (rules/quest_clock.hpp): FUN_0052d870 steps the quest
// timers (FUN_00543e10) on every 20th game frame, and a timer of n ticks
// (FUN_00543f10: due at the count + n, run once the count is past it) goes
// on the (n + 1)th step. No game data needed.
#include <quest_clock.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>

int main() try {
    using d2d::rules::QuestClock, d2d::rules::quest_timer_steps;
    QuestClock clock;
    int steps = 0, first = 0;
    for (int frame = 1; frame <= 400; ++frame)
        if (clock.frame_step()) { ++steps; if (first == 0) first = frame; }
    assert(first == 20 && steps == 20 && clock.count == 20 && clock.frame == 400);   // frames 20, 40, ... 400

    // A timer set now of n ticks: the count it's due at is passed n + 1 steps on.
    for (int ticks : { 1, 3, 8, 0xf, 0x14 }) {
        const std::uint32_t due = clock.count + std::uint32_t(ticks);
        int step = 0;
        for (std::uint32_t count = clock.count; due >= count; ++count) ++step;
        assert(step == quest_timer_steps(ticks));
    }
    static_assert(quest_timer_steps(0x14) == 21 && quest_timer_steps(1) == 2);
    std::puts("quest_clock: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_clock: %s\n", error.what());
    return 1;
}
