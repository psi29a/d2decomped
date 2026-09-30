// The Den of Evil through a game: Akara gives it, the Den's cleared, she
// rewards once; a later game picks the state up from the flags.
#include <quests.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    QuestBits quest_bits{};
    DenQuest den;
    auto akara = [&] { return den.talk(quest_bits, 148); };

    assert(den.alert(quest_bits, 148) && !den.alert(quest_bits, 150));
    // Not given: Akara greets with it; the others say nothing.
    assert(akara().size() == 1 && akara()[0].string == 64 && akara()[0].greet);
    assert(den.talk(quest_bits, 150).empty());
    assert(!den.said(quest_bits, 148, 64) && den.state == 2 && qbit(quest_bits, 1, 2));
    assert(den.talk(quest_bits, 150).size() == 1 && den.talk(quest_bits, 150)[0].string == 66 && !den.talk(quest_bits, 150)[0].greet);

    // Into the Den: bit 4; the others' early-return topics.
    den.enter_den(quest_bits);
    assert(den.state == 3 && qbit(quest_bits, 1, 4) && !qbit(quest_bits, 1, 3));
    assert(akara()[0].string == 71 && !akara()[0].greet);

    // A new game from these flags: back in the Den's state.
    DenQuest again;
    again.join(quest_bits);
    assert(again.state == 3 && again.log == 2);

    assert(den.killed(quest_bits, 9) == DenQuest::Kill::none && den.log == 2);
    assert(den.killed(quest_bits, 5) == DenQuest::Kill::few && den.log == 4);         // "Monsters remaining"

    assert(den.killed(quest_bits, 0) == DenQuest::Kill::cleared && den.state == 4);
    assert(qbit(quest_bits, 1, 1) && qbit(quest_bits, 1, 13) && den.killed(quest_bits, 0) == DenQuest::Kill::none);

    // Akara's reward, once.
    assert(akara().size() == 1 && akara()[0].string == 76 && akara()[0].greet);
    assert(den.talk(quest_bits, 147)[0].string == 79);
    assert(den.said(quest_bits, 148, 76) && qbit(quest_bits, 1, 0) && !qbit(quest_bits, 1, 1) && !qbit(quest_bits, 1, 4));
    assert(qbit(quest_bits, 41, 1) && qbit(quest_bits, 41, 13));
    assert(!den.said(quest_bits, 148, 76) && akara().empty() && !den.alert(quest_bits, 148));

    // Cleared without talking to her first: still her reward.
    QuestBits other_bits{};
    DenQuest other_den;
    other_den.enter_den(other_bits);
    assert(other_den.killed(other_bits, 0) == DenQuest::Kill::cleared && other_den.talk(other_bits, 148)[0].string == 76);

    // Sisters to the Slaughter: available 20 ticks in; Cain gives it;
    // the Catacombs; Andariel's kill (portal at tick 10, log 3 at 12);
    // Warriv's reward, once.
    QuestBits andy_bits{};
    AndyQuest andy;
    for (int i = 0; i < 19; ++i) andy.tick();
    assert(andy.state == 0 && andy.talk(andy_bits, AndyQuest::kCain).empty());
    andy.tick();
    assert(andy.state == 1 && andy.alert(andy_bits, AndyQuest::kCain) && andy.talk(andy_bits, AndyQuest::kCain)[0].string == 166);
    andy.said(andy_bits, AndyQuest::kCain, 166);
    andy.talk_closed(AndyQuest::kCain);
    assert(andy.state == 2 && andy.log == 1 && qbit(andy_bits, 6, 2) && andy.talk(andy_bits, AndyQuest::kWarriv)[0].string == 171);
    andy.enter(andy_bits, 1, 2);
    assert(andy.state == 3 && qbit(andy_bits, 6, 3) && andy.talk(andy_bits, AndyQuest::kWarriv)[0].string == 177);
    andy.enter(andy_bits, 36, AndyQuest::kLair);
    assert(andy.killed(andy_bits, true) && andy.state == 4 && qbit(andy_bits, 6, 1) && qbit(andy_bits, 6, 13) && andy.cain);
    assert(!andy.killed(andy_bits, true));                                            // no second drop
    int portal_tick = 0;
    for (int i = 2; i <= 12; ++i) if (andy.tick()) portal_tick = i;
    assert(portal_tick == 10 && andy.log == 3 && andy.after_kill == 0);
    assert(andy.alert(andy_bits, AndyQuest::kWarriv) && andy.talk(andy_bits, AndyQuest::kWarriv)[0].string == 183 && andy.talk(andy_bits, AndyQuest::kWarriv)[0].greet);
    assert(andy.talk(andy_bits, AndyQuest::kCain)[0].string == 184 && andy.talk(andy_bits, 154)[0].string == 180);
    assert(andy.said(andy_bits, AndyQuest::kWarriv, 183) && qbit(andy_bits, 6, 0) && !qbit(andy_bits, 6, 1) && andy.state == 5 && andy.log == 0xd);
    assert(!andy.said(andy_bits, AndyQuest::kWarriv, 183) && !andy.alert(andy_bits, AndyQuest::kWarriv));
    assert(quest_name(183) == 3719 && quest_name(64) == 3714 && quest_name(185) == 0);

    // The Forgotten Tower: the tome read first (log 1); the Tower (log 4),
    // cellar 5 (log 2); the Countess's kill is the completion; the success
    // talk once, its replay until the player leaves town.
    QuestBits tower_bits{};
    TowerQuest tower;
    assert(tower.talk(tower_bits, 150).empty() && !tower.alert(tower_bits, 150));
    tower.read_tome(tower_bits);
    assert(tower.state == 2 && tower.log == 1 && tower.tome_early && qbit(tower_bits, 5, 2));
    assert(tower.talk(tower_bits, 150)[0].string == 133 && !tower.talk(tower_bits, 150)[0].greet);
    tower.enter(tower_bits, 1, 3);                                                   // out of town: state 3, no mark
    assert(tower.state == 3 && !qbit(tower_bits, 5, 3) && tower.talk(tower_bits, 147)[0].string == 139);
    tower.enter(tower_bits, 6, TowerQuest::kTower);
    assert(tower.log == 4 && qbit(tower_bits, 5, 6));
    TowerQuest tower_again;
    tower_again.join(tower_bits);
    assert(tower_again.state == 3 && tower_again.log == 4);
    tower.enter(tower_bits, 24, TowerQuest::kCellar);
    assert(tower.state == 3 && tower.log == 2 && qbit(tower_bits, 5, 4));
    assert(tower.killed(tower_bits, true) && qbit(tower_bits, 5, 0) && qbit(tower_bits, 5, 13) && tower.state == 5);
    assert(!tower.killed(tower_bits, true));                                         // once
    for (int i = 0; i < 6; ++i) tower.tick();
    assert(tower.log == 2);
    tower.tick();
    assert(tower.log == 13);
    assert(tower.alert(tower_bits, 150) && !tower.alert(tower_bits, TowerQuest::kQuest) && !tower.alert(tower_bits, 155));
    assert(tower.talk(tower_bits, 155)[0].string == 141 && tower.talk(tower_bits, 155)[0].greet);
    assert(tower.said(tower_bits, 155, 141) && !tower.due && tower.told);
    assert(!tower.said(tower_bits, 150, 140) && tower.talk(tower_bits, 150)[0].string == 140 && !tower.talk(tower_bits, 150)[0].greet);
    tower.enter(tower_bits, 1, 2);
    assert(tower.talk(tower_bits, 150).empty());
    // Not in cellar 5 at her death: bit 14, nothing to say.
    QuestBits away_bits{};
    TowerQuest away;
    away.enter(away_bits, 6, TowerQuest::kTower);
    assert(away.state == 2 && away.log == 3 && qbit(away_bits, 5, 2));
    assert(!away.killed(away_bits, false) && qbit(away_bits, 5, 14) && !qbit(away_bits, 5, 0) && away.talk(away_bits, 150).empty());
    std::puts("ok");
}
