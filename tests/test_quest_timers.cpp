// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Quest timers moved onto the quest clock (issue #383), counted in quest
// steps (rules/quest_clock.hpp), each against its game.exe callback. No
// game data.
#include <quest_clock.hpp>
#include <quests.hpp>
#include <quests_act4.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    // Terror's End: the 1-tick release timer returns 0 till Diablo comes,
    // so it runs every 2 steps; its run count (+0x18) passes 9 on the 10th
    // run, step 20. A run with him not made yet (no start point) is
    // followed by another 2 steps on.
    TerrorQuest terror;
    assert(!terror.release_step());   // not armed: no timer
    terror.arm();
    std::vector<int> calls;
    for (int step = 1; step <= 24; ++step)
        if (terror.release_step()) calls.push_back(step);
    assert((calls == std::vector{ 20, 22, 24 }) && terror.release_runs == 12);
    terror.diablo_came();
    assert(!terror.armed && terror.diablo_made && !terror.release_step());

    std::puts("quest_timers: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_timers: %s\n", error.what());
    return 1;
}
