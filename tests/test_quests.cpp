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

    // Tools of the Trade: the chain opens it; Charsi gives it; out of
    // town; the stand refuses below clvl 8, then drops the malus once;
    // Charsi takes it back; the imbue's used once.
    QuestBits tools_bits{};
    ToolsQuest tools;
    tools.join(tools_bits);
    assert(tools.state == 0 && tools.talk(tools_bits, ToolsQuest::kCharsi, false, 1).empty() && !tools.alert(tools_bits, ToolsQuest::kCharsi, false, 1));
    assert(!tools.open() && tools.state == 1 && tools.alert(tools_bits, ToolsQuest::kCharsi, false, 1));
    assert(tools.talk(tools_bits, ToolsQuest::kCharsi, false, 1)[0].string == 146 && tools.talk(tools_bits, ToolsQuest::kCharsi, false, 1)[0].greet);
    assert(!tools.said(tools_bits, ToolsQuest::kCharsi, 146, false) && tools.state == 2 && !qbit(tools_bits, 3, 2));
    tools.talk_closed(tools_bits, ToolsQuest::kCharsi);
    assert(qbit(tools_bits, 3, 2) && tools.log == 1 && tools.talk(tools_bits, 265, false, 1)[0].string == 147);
    tools.enter(tools_bits, 2);                                                      // not from town
    assert(tools.state == 2);
    tools.enter(tools_bits, 1);
    assert(tools.state == 3 && qbit(tools_bits, 3, 3) && tools.talk(tools_bits, ToolsQuest::kCharsi, false, 1)[0].string == 157);
    assert(tools.operate(tools_bits, 7) == ToolsQuest::Stand::refuse && tools.state == 3);
    assert(tools.operate(tools_bits, 8) == ToolsQuest::Stand::drop && tools.state == 4 && tools.log == 1);
    assert(tools.operate(tools_bits, 8) == ToolsQuest::Stand::none);
    assert(tools.talk(tools_bits, ToolsQuest::kCharsi, false, 8).empty());          // state 4: nothing till it's in hand
    assert(tools.picked_up(tools_bits) && qbit(tools_bits, 3, 6) && tools.log == 2 && !tools.picked_up(tools_bits));
    assert(tools.log_state(tools_bits, true, 8) == 2);
    assert(tools.talk(tools_bits, ToolsQuest::kCharsi, true, 7).empty() && !tools.alert(tools_bits, ToolsQuest::kCharsi, true, 7));
    assert(tools.alert(tools_bits, ToolsQuest::kCharsi, true, 8) && tools.talk(tools_bits, ToolsQuest::kCharsi, true, 8)[0].string == 163);
    assert(tools.talk(tools_bits, 150, true, 8)[0].string == 162);
    assert(!tools.said(tools_bits, ToolsQuest::kCharsi, 163, false));                 // not holding it
    assert(tools.said(tools_bits, ToolsQuest::kCharsi, 163, true) && tools.state == 5 && qbit(tools_bits, 3, 1) && qbit(tools_bits, 3, 13));
    tools.talk_closed(tools_bits, ToolsQuest::kCharsi);
    assert(tools.log == 0xd && tools.log_state(tools_bits, false, 8) == 10 && tools.open());   // passes the chain on
    tools.imbued(tools_bits);
    assert(qbit(tools_bits, 3, 0) && !qbit(tools_bits, 3, 1) && tools.log_state(tools_bits, false, 8) == 0xd);
    assert(!tools.alert(tools_bits, ToolsQuest::kCharsi, false, 8) && tools.talk(tools_bits, 150, false, 8).empty());
    // A later game: off; the stand says no.
    ToolsQuest later;
    later.join(tools_bits);
    assert(!later.active && later.open() && later.state == 0 && later.operate(tools_bits, 30) == ToolsQuest::Stand::refuse);
    assert(quest_name(163) == 3716);
    std::puts("ok");
}
