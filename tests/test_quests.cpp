// SPDX-License-Identifier: GPL-3.0-or-later
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

    // Sisters to the Slaughter: available 20 ticks after the chain reaches it; Cain gives it;
    // the Catacombs; Andariel's kill (portal at tick 10, log 3 at 12);
    // Warriv's reward, once.
    QuestBits andy_bits{};
    AndyQuest andy;
    andy.tick();
    assert(andy.start_in == 0 && andy.state == 0);                                   // no timer till the chain
    andy.chain();
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
    assert(andy.talk(andy_bits, AndyQuest::kCain)[0].string == 184 && andy.talk(andy_bits, monster_ids::kCharsi)[0].string == 180);
    assert(andy.said(andy_bits, AndyQuest::kWarriv, 183) && qbit(andy_bits, 6, 0) && !qbit(andy_bits, 6, 1) && andy.state == 5 && andy.log == 0xd);
    assert(!andy.said(andy_bits, AndyQuest::kWarriv, 183) && !andy.alert(andy_bits, AndyQuest::kWarriv));

    // Sisters' Burial Grounds: open once the Den's done; Kashya gives it
    // (the log waits for the talk to close); the Burial Grounds; Blood
    // Raven's kill (log 3 at tick 15); Kashya's reward, once.
    QuestBits burial_bits{};
    BurialQuest burial;
    burial.join(burial_bits);
    assert(burial.state == 0 && burial.talk(burial_bits, BurialQuest::kKashya).empty() && !burial.alert(burial_bits, BurialQuest::kKashya));
    assert(!burial.chain() && burial.state == 1 && burial.alert(burial_bits, BurialQuest::kKashya));
    assert(burial.talk(burial_bits, BurialQuest::kKashya)[0].string == 81 && burial.talk(burial_bits, BurialQuest::kKashya)[0].greet);
    assert(!burial.said(burial_bits, BurialQuest::kKashya, 81) && burial.state == 2 && burial.log == 0 && qbit(burial_bits, 2, 2));
    burial.talk_closed(burial_bits, BurialQuest::kKashya);
    assert(burial.log == 1 && burial.talk(burial_bits, 155)[0].string == 86 && !burial.alert(burial_bits, BurialQuest::kKashya));
    burial.enter(burial_bits, 1, 3);                                                  // out of town: state 3, bit 3 (log 1)
    assert(burial.state == 3 && qbit(burial_bits, 2, 3) && burial.talk(burial_bits, BurialQuest::kKashya)[0].string == 87);
    burial.enter(burial_bits, 3, BurialQuest::kBurial);
    assert(burial.log == 2 && qbit(burial_bits, 2, 4));
    BurialQuest burial_again;
    burial_again.join(burial_bits);
    assert(burial_again.state == 3 && burial_again.log == 2);
    assert(burial.killed(burial_bits, true) && burial.state == 4 && qbit(burial_bits, 2, 13) && qbit(burial_bits, 2, 1) && !qbit(burial_bits, 2, 14));
    for (int i = 0; i < 14; ++i) burial.tick();
    assert(burial.log == 2);
    burial.tick();
    assert(burial.log == 3 && burial.alert(burial_bits, BurialQuest::kKashya) && burial.talk(burial_bits, BurialQuest::kKashya)[0].string == 92);
    assert(burial.talk(burial_bits, BurialQuest::kKashya)[0].greet && burial.talk(burial_bits, 147)[0].string == 95);
    assert(burial.said(burial_bits, BurialQuest::kKashya, 92) && qbit(burial_bits, 2, 0) && !qbit(burial_bits, 2, 1) && burial.state == 5 && burial.log == 0xd);
    assert(!burial.said(burial_bits, BurialQuest::kKashya, 92) && !burial.alert(burial_bits, BurialQuest::kKashya) && burial.chain());
    assert(burial.talk(burial_bits, 155)[0].string == 96 && !burial.talk(burial_bits, BurialQuest::kKashya)[0].greet);   // block 4 while in town
    burial.enter(burial_bits, 1, 2);
    assert(burial.talk(burial_bits, 155).empty());
    BurialQuest burial_done;                                                          // a later game: closed
    burial_done.join(burial_bits);
    assert(burial_done.chain() && burial_done.state == 0 && !burial_done.killed(burial_bits, true));
    // Killed far from the player: someone else's kill, no reward.
    QuestBits far_bits{};
    BurialQuest reach;
    assert(!reach.killed(far_bits, false) && qbit(far_bits, 2, 14) && !qbit(far_bits, 2, 1) && reach.talk(far_bits, BurialQuest::kKashya).empty());
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
    cain.rescued(true);                                      // Tristram Cain walks to his portal: not due yet
    assert(cain.log == 6 && !cain.camp_due && !cain.enter(cain_bits, CainQuest::kTristram, 1));
    CainQuest no_room;                                       // his spawn failed: straight to the camp
    no_room.rescued(false);
    assert(no_room.camp_due && no_room.log == 6);
    cain.portal_entered();
    assert(!cain.camp_cain && cain.enter(cain_bits, CainQuest::kTristram, 1) && cain.camp_cain);
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

    // The log state sent for a record without its own (FUN_00543f90).
    QuestBits log_bits{};
    assert(quest_log(log_bits, 1, 3, 2, 4) == 2 && quest_log(log_bits, 1, 5, 13, 4) == 12);
    qset(log_bits, 1, 13);
    assert(quest_log(log_bits, 1, 5, 13, 4) == 13);
    qset(log_bits, 1, 14);
    assert(quest_log(log_bits, 1, 3, 2, 4) == 12);
    assert(quest_log(log_bits, 4, 5, 6, 6) == 12 && quest_log(log_bits, 4, 6, 13, 6) == 12);   // Cain: missed the rescue
    qset(log_bits, 4, 13);
    assert(quest_log(log_bits, 4, 5, 6, 6) == 6);

    // The log (FUN_004a1950): hidden till there's something to say.
    QuestBits text_bits{};
    QuestState log_state{};
    for (int quest = 1; quest <= 6; ++quest) assert(quest_text(text_bits, quest, log_state).shown == 2);
    log_state.log[1] = 1;
    auto text = quest_text(text_bits, 1, log_state);
    assert(text.string == 3735 && text.speech == 64 && text.shown == 3 && text.count == -1);
    log_state.log[1] = 4; log_state.den_left = 5;
    text = quest_text(text_bits, 1, log_state);
    assert(text.string == 3738 && text.count == 5);
    log_state.den_left = 1;
    assert(quest_text(text_bits, 1, log_state).string == 3739);
    qset(text_bits, 1, 13); qset(text_bits, 1, 1);                                     // the reward's due: state 4 + 1
    text = quest_text(text_bits, 1, log_state);
    assert(text.string == 3740 && text.speech == 64 && text.count == -1);
    qset(text_bits, 1, 1, false); qset(text_bits, 1, 0);                              // done in this game: the animation
    text = quest_text(text_bits, 1, log_state);
    assert(text.string == 3726 && text.speech == 76 && text.shown == 0);
    qset(text_bits, 1, 12);
    assert(quest_text(text_bits, 1, log_state).shown == 1);
    qset(text_bits, 1, 13, false);                                                    // done in a game before
    text = quest_text(text_bits, 1, log_state);
    assert(text.string == 3728 && text.speech == 76);
    log_state.log[2] = 2;
    assert(quest_text(text_bits, 2, log_state).string == 3742 && quest_text(text_bits, 2, log_state).speech == 81);
    qset(text_bits, 6, 13); qset(text_bits, 6, 1);                                     // Andariel's reward due: state 10
    assert(quest_text(text_bits, 6, log_state).string == 3760 && quest_text(text_bits, 6, log_state).speech == 166);
    log_state.log[3] = 0xd; qset(text_bits, 3, 13);                                    // Tools: no reward state, the server's 13
    text = quest_text(text_bits, 3, log_state);
    assert(text.string == 3726 && text.speech == 146 && text.shown == 0);
    log_state.log[4] = 12;                                                             // another player's: 3727 reads 3729
    assert(quest_text(text_bits, 4, log_state).string == 3729);
    log_state.game[5] = 1 << 13;                                                       // the Countess dead in this game, not by us
    text = quest_text(text_bits, 5, log_state);
    assert(text.string == 3729 && text.speech == 0 && text.shown == 3);
    qset(text_bits, 5, 1); qset(text_bits, 5, 15);                                     // closed with the reward due: state 10, none
    assert(quest_text(text_bits, 5, log_state).shown == 2);
    qset(text_bits, 2, 1); qset(text_bits, 2, 15);
    assert(quest_text(text_bits, 2, log_state).string == 3743);

    // The chain (+0xf0 / +0x10: 1 → 2 → 4 → 3 → 6, 5 → 3), from quest 1 at
    // the first join (FUN_00546270).
    struct Game {
        DenQuest den; BurialQuest burial; CainQuest cain; TowerQuest tower; ToolsQuest tools; AndyQuest andy;
        explicit Game(const QuestBits& bits) {
            den.join(bits); burial.join(bits); cain.join(bits, false, false); tower.join(bits); tools.join(bits); andy.join(bits);
            run(1);
        }
        void run(int quest) { chain(quest, den, burial, cain, tower, tools, andy); }
    };
    QuestBits chain_bits{};
    Game fresh(chain_bits);
    assert(fresh.burial.state == 0 && fresh.cain.state == 0 && fresh.tools.state == 0 && fresh.andy.start_in == 0);
    fresh.den.state = 5;                                                               // Akara's reward (FUN_0058fdd0)
    fresh.run(1);
    assert(fresh.burial.state == 1 && fresh.cain.state == 0);
    fresh.tower.state = 5;                                                             // the Countess's success talk (FUN_00594960)
    fresh.run(5);
    assert(fresh.tools.state == 1 && fresh.andy.start_in == 0);
    fresh.cain.state = 6;                                                             // Akara's ring (FUN_00592250)
    fresh.run(4);
    assert(fresh.tools.state == 1);                                                    // Tools holds it
    fresh.tools.state = 5;                                                             // the malus back (FUN_00591490)
    fresh.run(3);
    assert(fresh.andy.start_in == 20);
    qset(chain_bits, 1, 0);
    assert(Game(chain_bits).burial.state == 1 && Game(chain_bits).cain.state == 0);
    qset(chain_bits, 2, 15);
    assert(Game(chain_bits).cain.state == 1 && Game(chain_bits).tools.state == 0);
    qset(chain_bits, 4, 0);
    assert(Game(chain_bits).tools.state == 1 && Game(chain_bits).andy.start_in == 0);
    qset(chain_bits, 3, 0);
    Game late_game(chain_bits);
    assert(late_game.andy.start_in == 20);
    for (int i = 0; i < 20; ++i) late_game.andy.tick();
    assert(late_game.andy.state == 1);
    std::puts("ok");
}
