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

// Checks which quest records the World sends by event, so the View-change stand-in skips them, and whom a send reaches (FUN_00544190).
int main() {
    {   // Which players a send reaches, by their bits: the oracle's cases (tools/emu/quest_log_sends.py).
        struct OracleCase { int quest; bool bit0, bit13, bit14, bit15, sent; };
        static constexpr OracleCase oracle_cases[]{
#include "quest_log_sends_cases.inc"
        };
        for (const auto& sample : oracle_cases) {
            QuestBits bits{};
            quest::set(bits, sample.quest, 0, sample.bit0);
            quest::set(bits, sample.quest, 13, sample.bit13);
            quest::set(bits, sample.quest, 14, sample.bit14);
            quest::set(bits, sample.quest, 15, sample.bit15);
            assert(quest_log_goes_to(bits, sample.quest) == sample.sent);
        }
    }
    // The records whose `news` the World turns into an event (world/quests/act1.cpp, act2.cpp, act3.cpp, act4.cpp, act5.cpp).
    for (const int quest : { DenQuest::kQuest, BurialQuest::kQuest, ToolsQuest::kQuest, CainQuest::kQuest, TowerQuest::kQuest, AndyQuest::kQuest,
                             RadamentQuest::kQuest, TaintedSunQuest::kQuest, ArcaneQuest::kQuest, SummonerQuest::kQuest, SevenTombsQuest::kQuest,
                             GuardianQuest::kQuest, IzualQuest::kQuest, HellforgeQuest::kQuest, TerrorQuest::kQuest, SiegeQuest::kQuest, RescueQuest::kQuest,
                             PrisonQuest::kQuest, BetrayalQuest::kQuest, EveQuest::kQuest, RiteQuest::kQuest })
        assert(quest_log_sent_by_event(quest));
    int count = 0;
    for (int quest = 0; quest < 0x30; ++quest) count += quest_log_sent_by_event(quest) ? 1 : 0;
    assert(count == 21);
    // a2q2 (10: no FUN_00544300 site), a3q1 .. a3q5 and a4q1's 0x18 still read the View's change, as do the words
    // with no shown slot.
    for (const int quest : { 7, 8, HoradricStaffQuest::kQuest }) assert(!quest_log_sent_by_event(quest));
    for (int quest = 0xf; quest <= 0x15; ++quest) assert(!quest_log_sent_by_event(quest));
    assert(!quest_log_sent_by_event(0) && !quest_log_sent_by_event(0x17) && !quest_log_sent_by_event(0x18));
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
