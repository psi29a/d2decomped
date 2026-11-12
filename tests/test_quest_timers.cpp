// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Quest timers moved onto the quest clock (issue #383), counted in quest
// steps (rules/quest_clock.hpp), each against its game.exe callback. No
// game data.
#include <quest_clock.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act4.hpp>
#include <quests_act5.hpp>

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

    // The Guardian: Mephisto's death queues 12 ticks (0x5bc9d6); the log
    // goes to 4 on step 13, sent again (news), once.
    GuardianQuest guardian;
    guardian.log = 2;
    guardian.arm_log();
    assert(guardian.log_in == quest_timer_steps(GuardianQuest::kLogTicks));
    for (int step = 1; step < 13; ++step) { guardian.quest_step(); assert(guardian.log == 2 && !guardian.news); }
    guardian.quest_step();
    assert(guardian.log == 4 && guardian.news && guardian.log_in == 0);
    guardian.news = false;
    guardian.arm_log();   // data +0 was cleared: a later death queues it again
    for (int step = 1; step <= 13; ++step) guardian.quest_step();
    assert(guardian.log == 4 && !guardian.news);   // already 4: nothing sent (0x5bc727)
    GuardianQuest closed;
    closed.open = false;                           // +9 clear: no timer
    closed.arm_log();
    assert(closed.log_in == 0);
    // The notifier (0x5bc210): quiet once done (bit 0 or 15) without 13 / 14.
    QuestBits bits{};
    assert(GuardianQuest::told(bits));
    quest::set(bits, GuardianQuest::kQuest, 0);
    assert(!GuardianQuest::told(bits));
    quest::set(bits, GuardianQuest::kQuest, 13);
    assert(GuardianQuest::told(bits));

    // Prison of Ice: the thaw's 1-tick timer returns 0 at its first run, so
    // fana's mode 2 comes on step 2 and Anya on step 4 (FUN_0058aab0).
    PrisonQuest prison;
    QuestBits prison_bits{};
    assert(prison.thaw(prison_bits, true) && prison.thawing);
    std::vector<int> thaws;
    for (int step = 1; step <= 6; ++step)
        if (prison.thaw_quest_step()) thaws.push_back(step * 10 + prison.thaw_tick());
    assert((thaws == std::vector{ 21, 42 }) && !prison.thawing);

    std::puts("quest_timers: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_timers: %s\n", error.what());
    return 1;
}
