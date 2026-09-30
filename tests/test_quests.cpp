// The Den of Evil through a game: Akara gives it, the Den's cleared, she
// rewards once; a later game picks the state up from the flags.
#include <quests.hpp>

#include <algorithm>
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

    // The Search for Cain: Akara gives it; out of town; the tree's scroll;
    // Akara deciphers it; the stones in order (a wrong one ignored); the
    // Gibbet; camp Cain; Akara's ring, once.
    QuestBits cain_bits{};
    CainQuest cain;
    cain.join(cain_bits, false, false);
    cain.open();
    assert(cain.state == 1 && cain.alert(cain_bits, CainQuest::kAkara) && cain.talk(cain_bits, CainQuest::kAkara, false)[0].string == 97);
    assert(cain.talk(cain_bits, CainQuest::kAkara, false)[0].greet && cain.talk(cain_bits, 150, false).empty());
    cain.said(cain_bits, CainQuest::kAkara, 97, false);
    cain.talk_closed(cain_bits, CainQuest::kAkara);
    assert(cain.state == 2 && cain.log == 1 && qbit(cain_bits, 4, 2) && cain.talk(cain_bits, 150, false)[0].string == 98);
    cain.enter(cain_bits, 1, 2);
    assert(cain.state == 3 && qbit(cain_bits, 4, 3) && cain.talk(cain_bits, CainQuest::kAkara, false)[0].string == 104);
    assert(cain.tree(cain_bits, false) && cain.state == 4 && cain.log == 2 && !cain.tree(cain_bits, false));
    assert(cain.alert(cain_bits, CainQuest::kAkara, true) && !cain.alert(cain_bits, CainQuest::kAkara, false));
    assert(cain.talk(cain_bits, CainQuest::kAkara, true)[0].string == 112 && cain.talk(cain_bits, 150, false)[0].string == 105);
    assert(cain.said(cain_bits, CainQuest::kAkara, 112, false) == CainQuest::Said::none);
    assert(cain.said(cain_bits, CainQuest::kAkara, 112, true) == CainQuest::Said::decipher && cain.state == 5 && cain.deciphered);
    cain.talk_closed(cain_bits, CainQuest::kAkara);
    assert(cain.log == 3 && cain.talk(cain_bits, CainQuest::kAkara, false)[0].string == 117);
    std::uint32_t lo = 1, hi = 666;
    cain.stone_order(lo, hi);
    auto order = cain.order;
    std::ranges::sort(order);
    assert(order == (std::array<int, 5>{ 17, 18, 19, 20, 21 }) && lo != 1);
    assert(cain.stone(cain_bits, cain.order[0], false, true) == CainQuest::Stone::none);            // no bkd
    assert(cain.stone(cain_bits, cain.order[1], true, true) == CainQuest::Stone::none);             // out of order
    for (int i = 0; i < 4; ++i) assert(cain.stone(cain_bits, cain.order[std::size_t(i)], true, true) == CainQuest::Stone::lit);
    assert(cain.stone(cain_bits, cain.order[4], true, true) == CainQuest::Stone::portal && cain.log == 4 && qbit(cain_bits, 4, 4));
    assert(cain.stone(cain_bits, cain.order[4], true, true) == CainQuest::Stone::none);
    assert(cain.gibbet(cain_bits, true) && qbit(cain_bits, 4, 1) && qbit(cain_bits, 4, 13) && !cain.gibbet(cain_bits, true));
    cain.rescued();
    assert(cain.log == 6 && !cain.camp_cain && cain.enter(cain_bits, CainQuest::kTristram, 1) && cain.camp_cain);
    assert(cain.alert(cain_bits, CainQuest::kCampCain) && cain.talk(cain_bits, CainQuest::kCampCain, false)[0].string == 123);
    cain.said(cain_bits, CainQuest::kCampCain, 123, false);
    assert(!cain.alert(cain_bits, CainQuest::kCampCain) && !cain.talk(cain_bits, CainQuest::kCampCain, false)[0].greet);
    assert(cain.alert(cain_bits, CainQuest::kAkara) && cain.talk(cain_bits, CainQuest::kAkara, false)[0].string == 118);
    assert(cain.said(cain_bits, CainQuest::kAkara, 118, false) == CainQuest::Said::ring && qbit(cain_bits, 4, 0) && !qbit(cain_bits, 4, 1));
    assert(cain.state == 6 && cain.log == 0xd && cain.rescued_flag);
    assert(cain.said(cain_bits, CainQuest::kAkara, 118, false) == CainQuest::Said::none);

    // Not rescued before Act 2: the Rogues get him (bit 14, camp Cain's
    // other line); a later game with it done has camp Cain from the start.
    QuestBits late_bits{};
    CainQuest late;
    late.join(late_bits, false, false);
    late.open();
    late.enter(late_bits, 1, CainQuest::kLut);
    assert(late.state == 7 && late.log == 5 && qbit(late_bits, 4, 14) && late.missed && late.camp_due);
    assert(late.enter(late_bits, CainQuest::kLut, 1) && late.talk(late_bits, CainQuest::kCampCain, false)[0].string == 125);
    late.said(late_bits, CainQuest::kCampCain, 125, false);
    assert(late.talk(late_bits, CainQuest::kCampCain, false)[0].string == 123);    // game.exe's: rec+0x1c, not list B yet
    late.said(late_bits, CainQuest::kCampCain, 123, false);
    assert(late.talk(late_bits, CainQuest::kCampCain, false)[0].string == 125 && !late.talk(late_bits, CainQuest::kCampCain, false)[0].greet);
    CainQuest next_game;
    next_game.join(cain_bits, false, false);
    assert(!next_game.active && next_game.camp_spawn() && next_game.stones_init() && !next_game.stones_init());
    std::puts("ok");
}
