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
#include <utility>

using namespace d2d::rules;

// Checks which quest records the World sends by event, so the View-change stand-in skips them (FUN_00544190).
int main() {
    // The records whose `news` the World turns into an event (world/quests/act3.cpp, act4.cpp, act5.cpp).
    for (const int quest : { GuardianQuest::kQuest, IzualQuest::kQuest, HellforgeQuest::kQuest, TerrorQuest::kQuest, SiegeQuest::kQuest, RescueQuest::kQuest,
                             PrisonQuest::kQuest, BetrayalQuest::kQuest, EveQuest::kQuest, RiteQuest::kQuest, TowerQuest::kQuest })
        assert(quest_log_sent_by_event(quest));
    int count = 0;
    for (int quest = 0; quest < 0x30; ++quest) count += quest_log_sent_by_event(quest) ? 1 : 0;
    assert(count == 11);
    // a1q1 .. a1q4, a1q6, act 2, a3q1 .. a3q5 and a4q1's 0x18 still read the View's change.
    for (int quest = 1; quest <= 0x15; ++quest) assert(quest == TowerQuest::kQuest || !quest_log_sent_by_event(quest));
    assert(!quest_log_sent_by_event(0x17) && !quest_log_sent_by_event(0x18));
    {   // The Forgotten Tower's flag-1 sites (FUN_00594960, FUN_00595010, Q05_TimerNotifyCompletion) raise news; its join doesn't.
        QuestBits bits{};
        TowerQuest joined;
        quest::set(bits, TowerQuest::kQuest, 2);
        joined.join(bits);
        assert(!joined.news && joined.log == 1);
        TowerQuest tower;
        assert(tower.enter(bits, 1, TowerQuest::kTower, 0) && tower.log == 3 && std::exchange(tower.news, false));   // level 20, state 0: log 3
        assert(tower.enter(bits, 1, TowerQuest::kCellar, 0) && tower.log == 2 && std::exchange(tower.news, false));  // cellar 5: log 2
        assert(!tower.enter(bits, 1, TowerQuest::kCellar, 0) && !tower.news);                                          // log 2 already: no send
        tower.state = 5; tower.log_in = 1;
        tower.tick();
        assert(tower.log == 13 && tower.news);                                                                         // the timer: log 0xd
        TowerQuest tome;
        tome.tome_early = true; tome.said(bits, -1, TowerQuest::kTome, 0);
        assert(tome.log == 1 && tome.news);
        assert(TowerQuest::log_goes_to(bits));
        quest::set(bits, TowerQuest::kQuest, 0);
        assert(!TowerQuest::log_goes_to(bits));
    }
    std::puts("ok");
    return 0;
}
