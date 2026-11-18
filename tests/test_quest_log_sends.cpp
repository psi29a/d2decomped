// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Which quests' log sends the World makes itself (FUN_00544300's flag-1 sites -> FUN_00544190 -> S->C 0x5d as ev::QuestNews):
// the client reads no send into a change of the View's log for those (rules::quest_log_sent_by_event), and does for the others.
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act4.hpp>
#include <quests_act5.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    // The records whose `news` the World turns into an event (world/quests/act3.cpp, act4.cpp, act5.cpp).
    for (const int quest : { GuardianQuest::kQuest, IzualQuest::kQuest, HellforgeQuest::kQuest, TerrorQuest::kQuest, SiegeQuest::kQuest, RescueQuest::kQuest,
                             PrisonQuest::kQuest, BetrayalQuest::kQuest, EveQuest::kQuest, RiteQuest::kQuest })
        assert(quest_log_sent_by_event(quest));
    int count = 0;
    for (int quest = 0; quest < 0x30; ++quest) count += quest_log_sent_by_event(quest) ? 1 : 0;
    assert(count == 10);
    // Acts 1 and 2, a3q1 .. a3q5 and a4q1's 0x18 still read the View's change.
    for (int quest = 1; quest <= 0x15; ++quest) assert(!quest_log_sent_by_event(quest));
    assert(!quest_log_sent_by_event(0x17) && !quest_log_sent_by_event(0x18));
    std::puts("ok");
    return 0;
}
