// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Act 1's records raise `news` (a log send, FUN_00544300 flag 1 -> each player's FUN_00544190) at their own
// flag-1 sites, and not at the flag-0 ones or their joins: the Den (FUN_0058fbe0), the Burial Grounds
// (FUN_00590830), Tools (FUN_005912e0), Cain (FUN_005920d0) and Andariel (FUN_00595b20).
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>

#include <cassert>
#include <cstdio>
#include <utility>

using namespace d2d::rules;

namespace {

// Takes the record's news, as World::act1_update does.
bool took(bool& news) { return std::exchange(news, false); }

// The Den of Evil: Akara's talk closing (0x58fc6f), out of town under another log (0x590595), into the Den
// (0x5904d3), each "monsters remaining" (0x59041d, 0x590455); the reward's log 0xd is flag 0 (FUN_0058fdd0).
void den() {
    QuestBits bits{};
    DenQuest den;
    den.join(bits);
    assert(!den.news);
    den.talk_closed(DenQuest::kAkara);                      // not given yet: nothing waits
    assert(den.log == 0 && !den.news);
    assert(!den.said(bits, DenQuest::kAkara, 64) && !den.news);
    den.talk_closed(monster_ids::kKashya);                  // someone else's talk
    assert(den.log == 0 && !den.news);
    den.talk_closed(DenQuest::kAkara);
    assert(den.log == 1 && took(den.news) && !den.told && !den.closing);
    den.left_town(bits);                                    // log 1 already: state 3, bit 3, no send
    assert(den.state == 3 && quest::bit(bits, DenQuest::kQuest, 3) && !den.news);
    {   // Out of town before the talk closed: the bits by log 0 (bit 4), then log 1 is sent; the hook's gone.
        QuestBits early_bits{};
        DenQuest early;
        early.said(early_bits, DenQuest::kAkara, 64);
        early.left_town(early_bits);
        assert(early.state == 3 && quest::bit(early_bits, DenQuest::kQuest, 4) && early.log == 1 && took(early.news));
        early.talk_closed(DenQuest::kAkara);
        assert(!early.news);
    }
    {   // Into the Den before Akara: the hook's cleared, so her talk closing later sends nothing.
        QuestBits first_bits{};
        DenQuest first;
        first.enter_den(first_bits);
        assert(first.log == 2 && took(first.news));
        first.said(first_bits, DenQuest::kAkara, 64);
        first.talk_closed(DenQuest::kAkara);
        assert(first.log == 2 && !first.news);
    }
    den.enter_den(bits);
    assert(den.log == 2 && took(den.news));
    den.enter_den(bits);                                    // log 2 already: no send
    assert(!den.news);
    assert(den.killed(9, true) == DenQuest::Kill::none && !den.news);
    assert(den.killed(5, true) == DenQuest::Kill::few && took(den.news));
    assert(den.killed(4, true) == DenQuest::Kill::few && took(den.news));   // every death under 6
    assert(den.killed(7, false) == DenQuest::Kill::few && took(den.news));  // a monster back up at log 4
    assert(den.killed(0, true) == DenQuest::Kill::cleared && !den.news);
    DenQuest::credit(bits, true);
    assert(den.said(bits, DenQuest::kAkara, 76) && den.log == 0xd && !den.news);
}

// Sisters' Burial Grounds: Kashya's talk closing (0x59094c), into the Burial Grounds (0x590ff4), the kill's
// timer (0x590c04); the reward's log 0xd is flag 0 (FUN_00590980).
void burial() {
    QuestBits bits{};
    BurialQuest burial;
    burial.join(bits);
    assert(!burial.chain());                    // opened, not passed on yet (state 1)
    assert(!burial.said(bits, BurialQuest::kKashya, 81, 1) && !burial.news);
    burial.talk_closed(bits, BurialQuest::kKashya);
    assert(burial.log == 1 && took(burial.news));
    burial.enter(bits, 1, 3, 1);                            // out of town: state 3, no send
    assert(!burial.news);
    burial.enter(bits, 3, BurialQuest::kBurial, 1);
    assert(burial.log == 2 && took(burial.news));
    burial.enter(bits, 3, BurialQuest::kBurial, 1);         // log 2 already
    assert(!burial.news);
    assert(burial.killed(bits, true) && !burial.news);
    while (burial.log_in > 0) burial.tick();
    assert(burial.log == 3 && took(burial.news));
    assert(burial.said(bits, BurialQuest::kKashya, 92, 1) && burial.log == 0xd && !burial.news);
}

// Tools of the Trade: Charsi's talk closing (0x5913ff, then 0x591421 after the return), out of town under
// another log (0x591891), the malus picked up (0x5919b4); the stand's log 1 is flag 0 (FUN_00591ac0).
void tools() {
    QuestBits bits{};
    ToolsQuest tools;
    tools.join(bits);
    assert(!tools.open() && !tools.news);
    assert(!tools.said(bits, ToolsQuest::kCharsi, 146, false) && !tools.news);
    tools.talk_closed(bits, ToolsQuest::kCharsi);
    assert(tools.log == 1 && took(tools.news));
    tools.enter(bits, 1);                                   // out of town at log 1: no send
    assert(tools.state == 3 && !tools.news);
    ToolsQuest early;
    early.state = 2;
    early.enter(bits, 1);                                   // the talk not closed yet: log 0 -> 1 is sent
    assert(early.log == 1 && took(early.news));
    assert(tools.operate(bits, 8) == ToolsQuest::Stand::drop && tools.log == 1 && !tools.news);
    tools.picked_up(bits);
    assert(tools.log == 2 && took(tools.news));
    assert(tools.said(bits, ToolsQuest::kCharsi, 163, true) && !tools.news);
    tools.talk_closed(bits, ToolsQuest::kCharsi);
    assert(tools.log == 0xd && took(tools.news));
}

// The Search for Cain: Akara's talk closing (0x5921e9; 0x59221f after the decipher, whose own log 3 is
// flag 0), the tree (0x593c3d), the fifth stone (0x593a65), the Gibbet (0x593446), the Rogues (0x596ff6);
// the Tristram entry's log 4 is flag 0 (FUN_00596de0).
void cain() {
    QuestBits bits{};
    CainQuest cain;
    cain.join(bits, false, false);
    assert(!cain.open() && !cain.news);
    cain.said(bits, CainQuest::kAkara, 97, false, 1);
    assert(!cain.news);
    cain.talk_closed(bits, CainQuest::kAkara);
    assert(cain.log == 1 && took(cain.news));
    assert(cain.tree(bits, false) && cain.log == 2 && took(cain.news));
    assert(cain.said(bits, CainQuest::kAkara, 112, true, 1) == CainQuest::Said::decipher && cain.log == 3 && !cain.news);
    cain.talk_closed(bits, CainQuest::kAkara);
    assert(cain.log == 3 && took(cain.news));
    cain.order = { 17, 18, 19, 20, 21 };
    for (int stone = 17; stone < 21; ++stone) assert(cain.stone(bits, stone, true, true) == CainQuest::Stone::lit && !cain.news);
    assert(cain.stone(bits, 21, true, true) == CainQuest::Stone::portal && cain.log == 4 && took(cain.news));
    assert(cain.gibbet(bits, true) && !cain.news);
    cain.rescued(true);
    assert(cain.log == 6 && took(cain.news));

    QuestBits late_bits{};
    CainQuest late;
    late.join(late_bits, false, false);
    late.open();
    late.enter(late_bits, level_ids::kRogueEncampment, level_ids::kLutGholein, 1);   // Act 2 first: the Rogues got him
    assert(late.state == 7 && late.log == 5 && took(late.news));
}

// Sisters to the Slaughter: Cain's talk closing (0x595baf), Catacombs 4 under log 2 (0x596071), the kill's
// timer at 12 (0x59656a); another Catacombs level's log 1 (0x596092) and the reward's 0xd are flag 0.
void andy() {
    QuestBits bits{};
    AndyQuest andy;
    andy.join(bits);
    andy.state = 1;
    andy.said(bits, AndyQuest::kCain, 166, 1);
    assert(!andy.news);
    andy.talk_closed(AndyQuest::kCain);
    assert(andy.log == 1 && took(andy.news));
    andy.enter(bits, level_ids::kRogueEncampment, 34);
    assert(andy.log == 1 && !andy.news);
    andy.enter(bits, 36, level_ids::kCatacombsLevel4);
    assert(andy.log == 2 && took(andy.news));
    AndyQuest early;
    early.enter(bits, 33, 34);                              // log 0 -> 1 in Catacombs 1: flag 0
    assert(early.log == 1 && !early.news);
    assert(andy.killed(bits) && !andy.news);
    while (andy.after_kill != 0) andy.tick();
    assert(andy.log == 3 && took(andy.news));
    assert(andy.said(bits, AndyQuest::kWarriv, 183, 1) && andy.log == 0xd && !andy.news);
}

}  // namespace

// Each act 1 record's flag-1 sites raise news; its flag-0 sites and joins don't.
int main() {
    den();
    burial();
    tools();
    cain();
    andy();
    std::puts("ok");
    return 0;
}
