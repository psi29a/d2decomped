// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Act 2's records raise `news` (a log send, FUN_00544300 flag 1 -> each player's FUN_00544190) at their own
// flag-1 sites, and not at the flag-0 ones or their joins: Radament (FUN_005988b0), the Tainted Sun
// (FUN_00599f50), the Arcane Sanctuary (FUN_0059ad50), the Summoner (FUN_0059bcb0) and the Seven Tombs
// (FUN_0059c650). a2q2 has no FUN_00544300 site.
#include <level_ids.hpp>
#include <quests.hpp>

#include <cassert>
#include <cstdio>
#include <utility>

using namespace d2d::rules;

namespace {

constexpr int kPlayer = 1;
constexpr int kTrueTomb = 66;   // one of the tombs (act +0x94 picks which)

// Takes the record's news, as World::act2_update does.
bool took(bool& news) { return std::exchange(news, false); }

// Radament's Lair: Atma's talk closing (0x5989bc), the kill's timer (0x598f8e) and the reward's log 0xd
// (0x598b42, flag 1 here unlike act 1's rewards).
void radament() {
    QuestBits bits{}, game_bits{};
    RadamentQuest radament;
    radament.join(bits, game_bits, false);
    assert(!radament.news);
    radament.talk_closed(bits, RadamentQuest::kAtma);         // nothing waits yet
    assert(radament.log == 0 && !radament.news);
    radament.said(bits, game_bits, RadamentQuest::kAtma, RadamentQuest::kInit, kPlayer);
    assert(radament.state == 2 && !radament.news);
    radament.talk_closed(bits, RadamentQuest::kAtma);
    assert(radament.log == 1 && took(radament.news));
    radament.enter(bits, level_ids::kLutGholein, kPlayer);     // out of town: state 3, no send
    assert(radament.state == 3 && !radament.news);
    radament.killed(bits, game_bits, true, false);
    assert(radament.state == 4 && !radament.news);
    while (radament.log_in > 0) radament.tick();
    assert(radament.log == 3 && took(radament.news));
    radament.said(bits, game_bits, RadamentQuest::kAtma, RadamentQuest::kSuccess, kPlayer);
    assert(radament.log == 0xd && radament.state == 5 && took(radament.news));
}

// The Tainted Sun: the darkness (0x59a364, from its timer LAB_0059ed80 and the altar's room FUN_0059a3f0),
// the altar's room's log 2 (0x59a455), the altar's timer (0x59a71e); Drognan's log 2 (0x59ecd4) and the
// reward's log 0xd (0x59ec56) are flag 0, and the join's darkness isn't sent.
void tainted() {
    QuestBits bits{}, game_bits{};
    TaintedSunQuest tainted;
    tainted.join(bits, game_bits);
    assert(tainted.enter(level_ids::kLutGholein, TaintedSunQuest::kLostCity, kPlayer) && !tainted.news);
    tainted.dark_in = 0;
    tainted.tick(bits);
    assert(tainted.dark && tainted.log == 1 && took(tainted.news));
    tainted.said(bits, game_bits, TaintedSunQuest::kDrognan, TaintedSunQuest::kTold, kPlayer);
    assert(tainted.log == 2 && tainted.state == 2 && !tainted.news);
    tainted.altar_seen();                                         // dark already and log 2: nothing to send
    assert(!tainted.news);
    assert(tainted.use_altar(bits, game_bits, false, false).used && !tainted.news);
    while (tainted.log_in > 0) tainted.tick(bits);
    assert(tainted.log == 3 && took(tainted.news));
    tainted.said(bits, game_bits, TaintedSunQuest::kDrognan, TaintedSunQuest::kThanksFirst, kPlayer);
    assert(tainted.log == 0xd && tainted.state == 5 && !tainted.news);

    TaintedSunQuest early;                                        // the altar's room first: dark, log 1 sent
    early.altar_seen();
    assert(early.dark && early.state == 3 && early.log == 1 && took(early.news));
    TaintedSunQuest told;                                         // past the dark with no log: log 2 sent
    told.state = 2;
    told.altar_seen();
    assert(told.log == 2 && took(told.news));

    QuestBits dark_bits{}, dark_game{};
    quest::set(dark_bits, TaintedSunQuest::kQuest, 2);
    TaintedSunQuest joined;
    joined.join(dark_bits, dark_game);
    assert(joined.dark && joined.log == 1 && !joined.news);
}

// The Arcane Sanctuary: Drognan's talk closing (0x59ae8e), Jerhyn's 377 (0x59b199), the staff made
// (0x59b6b0), into the Sanctuary under log 4 (0x59f110); the journal's log 5 (0x59b07f) is flag 0.
void arcane() {
    QuestBits bits{}, game_bits{};
    ArcaneQuest arcane;
    arcane.chain();
    arcane.said(bits, game_bits, ArcaneQuest::kDrognan, ArcaneQuest::kToJerhyn, -1, kPlayer);
    assert(arcane.state == 2 && !arcane.news);
    arcane.talk_closed(bits, ArcaneQuest::kDrognan);
    assert(arcane.log == 2 && took(arcane.news));
    arcane.talk_closed(bits, ArcaneQuest::kDrognan);              // once
    assert(!arcane.news);
    arcane.said(bits, game_bits, ArcaneQuest::kJerhyn, ArcaneQuest::kToPalace, -1, kPlayer);
    assert(arcane.log == 3 && took(arcane.news));
    arcane.enter(bits, 54, ArcaneQuest::kSanctuary, kPlayer);
    assert(arcane.state == 4 && arcane.log == 4 && took(arcane.news));
    arcane.enter(bits, 54, ArcaneQuest::kSanctuary, kPlayer);     // log 4 already
    assert(!arcane.news);
    arcane.said(bits, game_bits, ArcaneQuest::kDrognan, ArcaneQuest::kJournal, -1, kPlayer);
    assert(arcane.log == 5 && !arcane.news);

    ArcaneQuest made;
    made.staff_made(bits);
    assert(made.state == 1 && made.log == 1 && took(made.news));
    made.staff_made(bits);                                        // log 1 already
    assert(!made.news);
}

// The Summoner: his first think (0x59c389), the death's timer's second run (0x59c006), the thanks with
// bit 13 (0x59be04).
void summoner() {
    QuestBits bits{}, game_bits{};
    SummonerQuest summoner;
    summoner.join(bits);
    summoner.engaged(bits);
    assert(summoner.log == 2 && took(summoner.news));
    summoner.engaged(bits);
    assert(!summoner.news);
    assert(summoner.killed(bits, true) && !summoner.news);
    bool voiced = false;
    while (summoner.log_in > 0) {
        voiced = summoner.tick() || voiced;
        if (!summoner.voiced) assert(!summoner.news);
    }
    assert(voiced && summoner.log == 4 && took(summoner.news));
    summoner.said(bits, game_bits, SummonerQuest::kThanksFirst, kPlayer);
    assert(summoner.log == 0xd && summoner.state == 3 && took(summoner.news));
}

// The Seven Tombs: Jerhyn's talk closing after 430 (0x59c7c4) and 442 (0x59c7e2), Tyrael's (0x59c798),
// into the Canyon at log 0 (0x59d299), Duriel's timer (0x59cf07), Meshif's 450 with bit 13 (0x59cdfc);
// the true tomb's and the Lair's log 2 (0x59d2e1) are flag 0.
void tombs() {
    QuestBits bits{}, game_bits{};
    SevenTombsQuest tombs;
    tombs.true_tomb = kTrueTomb;
    tombs.join(bits);
    tombs.chain();
    assert(!tombs.news);
    tombs.said(bits, game_bits, SevenTombsQuest::kJerhyn, SevenTombsQuest::kStart, false, kPlayer);
    assert(tombs.state == 2 && !tombs.news);
    tombs.talk_closed(bits, SevenTombsQuest::kJerhyn);
    assert(tombs.log == 1 && took(tombs.news));
    tombs.talk_closed(bits, SevenTombsQuest::kJerhyn);           // nothing waits
    assert(!tombs.news);
    tombs.enter(bits, level_ids::kLutGholein, SevenTombsQuest::kCanyon, kPlayer);   // log 1 already
    assert(!tombs.news);
    tombs.enter(bits, SevenTombsQuest::kCanyon, SevenTombsQuest::kLair, kPlayer);   // state 2: nothing
    assert(!tombs.news);
    tombs.duriel_died(bits, true);
    assert(tombs.state == 3 && !tombs.news);
    while (tombs.log_in > 0) tombs.tick();
    assert(tombs.log == 3 && took(tombs.news));
    assert(tombs.said(bits, game_bits, SevenTombsQuest::kTyrael, SevenTombsQuest::kTyraelLine, true, kPlayer).town_portal && !tombs.news);
    tombs.talk_closed(bits, SevenTombsQuest::kTyrael);
    assert(tombs.log == 4 && took(tombs.news));
    tombs.said(bits, game_bits, SevenTombsQuest::kJerhyn, SevenTombsQuest::kReward, false, kPlayer);
    assert(quest::bit(bits, SevenTombsQuest::kQuest, 4) && !tombs.news);
    tombs.talk_closed(bits, SevenTombsQuest::kJerhyn);
    assert(tombs.log == 6 && took(tombs.news));
    tombs.said(bits, game_bits, SevenTombsQuest::kMeshif, SevenTombsQuest::kMeshifLine, false, kPlayer);
    assert(tombs.log == 0xd && tombs.state == 5 && took(tombs.news));

    QuestBits canyon_bits{};
    SevenTombsQuest canyon;
    canyon.true_tomb = kTrueTomb;
    canyon.chain();
    canyon.enter(canyon_bits, level_ids::kLutGholein, SevenTombsQuest::kCanyon, kPlayer);
    assert(canyon.state == 1 && canyon.log == 1 && took(canyon.news));   // the state moves only from 0
    SevenTombsQuest tomb;                                         // the true tomb at log 0: log 2, flag 0
    tomb.true_tomb = kTrueTomb;
    tomb.chain();
    tomb.enter(canyon_bits, SevenTombsQuest::kCanyon, kTrueTomb, kPlayer);
    assert(tomb.state == 2 && tomb.log == 2 && !tomb.news);
    SevenTombsQuest lair;                                         // the Lair at state 1: log 2, flag 0
    lair.chain();
    lair.enter(canyon_bits, kTrueTomb, SevenTombsQuest::kLair, kPlayer);
    assert(lair.state == 2 && lair.log == 2 && !lair.news);
}

}  // namespace

// Each act 2 record's flag-1 sites raise news; its flag-0 sites and joins don't.
int main() {
    radament();
    tainted();
    arcane();
    summoner();
    tombs();
    std::puts("ok");
    return 0;
}
