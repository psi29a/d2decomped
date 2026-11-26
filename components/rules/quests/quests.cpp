// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quests.hpp's quests, with the research behind each.
#include "quests.hpp"

#include "level_ids.hpp"
#include "monster_ids.hpp"
#include "quest_clock.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {
namespace quest {

bool bit(const QuestBits& quest_bits, int quest, int bit) {
    const int bit_index = quest * 16 + bit;
    return quest_bits[std::size_t(bit_index >> 3)] >> (bit_index & 7) & 1;
}

void set(QuestBits& quest_bits, int quest, int bit, bool set) {
    const int bit_index = quest * 16 + bit;
    const auto mask = std::uint8_t(1u << (bit_index & 7));
    auto& byte = quest_bits[std::size_t(bit_index >> 3)];
    byte = std::uint8_t(set ? byte | mask : byte & ~mask);
}

}
}

namespace d2d::rules {

// The quest log's state the server sends a player for a quest whose record
// has no log function of its own (+0xe8 0, FUN_00543f90): `done` is the
// record's +0xd, the state it's over from; 12 reads "another player's".
int quest_log(const QuestBits& quest_bits, int quest, int state, int log, int done) {
    const bool b13 = quest::bit(quest_bits, quest, 13), b14 = quest::bit(quest_bits, quest, 14);
    if (state < done) return b14 || (quest == 4 && log == 6 && !b13) ? 12 : log;
    if (b13) return log;
    if (quest == 4 && b14) return state == 6 ? 12 : log;
    return quest == 10 ? (log != 4 ? 12 : 4) : 12;
}

PalaceSpots palace_spots(const QuestBits& game_bits, bool tombs_open, int tombs_state) {
    const bool tombs_early = tombs_open && tombs_state < 2;
    PalaceSpots spots;
    spots.outside = !quest::bit(game_bits, 8, 13) && !quest::bit(game_bits, 12, 13) && tombs_early;
    spots.guard = !quest::bit(game_bits, 14, 13);
    spots.palace = !(!quest::bit(game_bits, 8, 13) && !quest::bit(game_bits, 9, 13) && tombs_early);
    spots.palace_west = tombs_early;
    return spots;
}

// The chain from `quest` (its +0xf0, on through +0x10 while one passes
// on): 1 → 2 → 4 → 3 → 6, 5 → 3 (the records' +0x10).

// The first join runs it from quest 1 (FUN_00546270).
void chain(int quest, DenQuest& den, BurialQuest& burial, CainQuest& cain, TowerQuest& tower, ToolsQuest& tools, AndyQuest& andy) {
    for (;;) {
        switch (quest) {
        case 1: if (!den.chain()) return; quest = 2; break;
        case 2: if (!burial.chain()) return; quest = 4; break;
        case 4: if (!cain.open()) return; quest = 3; break;
        case 5: if (!tower.chain()) return; quest = 3; break;
        case 3: if (!tools.open()) return; quest = 6; break;
        default: andy.chain(); return;
        }
    }
}

QuestText quest_text(const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state) {
    // The entries at 0x723f30: quests 1..6, 9..14, 17..22, 25..27 and 35..40 have a record.
    const bool act4 = quest >= 25 && quest <= 27, act5 = quest >= 35 && quest <= 40;
    if (!act4 && !act5 && (quest < 1 || quest > 22 || quest == 7 || quest == 8 || quest == 15 || quest == 16)) return {};
    const auto& rec = kQuestLogRecords[std::size_t(quest - (quest < 7 ? 1 : quest < 15 ? 3 : quest < 23 ? 5 : act4 ? 7 : 14))];
    auto flag = [&](int bit) { return d2d::rules::quest::bit(quest_bits, quest, bit); };
    auto game = [&](int bit) { return (quest_state.game[std::size_t(quest)] >> bit & 1) != 0; };
    auto entry = [&](int string, int speech, int shown, int state, bool marks) {
        return QuestText{ string == 3725 ? 0 : string, -1, speech == 3725 ? 0 : speech, shown, state, marks };
    };
    const bool bit0 = flag(0), bit1 = flag(1), b13 = flag(13);
    int log_state = quest_state.log[std::size_t(quest)];
    // Siege on Harrogath's record (0x723d24) with bit 1 goes straight to its log state's line, 4 when there's none
    // (0x4a1a43), done or not.
    const bool siege_due = quest == 35 && bit1;
    if (siege_due && log_state == 0) log_state = 4;
    if (bit0 && !siege_due) {                                               // done: 13 in this game, 11 before
        log_state = b13 ? 13 : 11;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[1], flag(12) ? 1 : 0, log_state, false);
    }
    if (quest == 14 && !b13) {                                // the Seven Tombs' own case (record 0x723a64)
        const bool summoner = d2d::rules::quest::bit(quest_bits, 12, 0) || d2d::rules::quest::bit(quest_bits, 12, 13);
        const int tomb = summoner && log_state == 1 ? 7 : flag(3) ? 5 : flag(4) ? 6 : 0;
        if (tomb) return entry(rec[std::size_t(2 * tomb + 1)], rec[std::size_t(2 * tomb + 2)], 3, tomb, false);
    }
    if (!siege_due && b13 && bit1 && rec[2] != 0xffff) {      // the reward's due
        log_state = rec[2] + 1;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], 3, log_state, true);
    }
    if (!siege_due && bit1 && flag(15)) {
        // State 10 (0x4a1cf9), but Prison of Ice (37) 5 without bit 8, 6 with 8 and not 9; Betrayal of Harrogath
        // (38) 4, 5 with bit 4; Hell's Forge (27) shown as done (1).
        const int state = quest == 37 ? (!flag(8) ? 5 : !flag(9) ? 6 : 10) : quest == 38 ? (flag(4) ? 5 : 4) : 10;
        const int line = rec[std::size_t(2 * state + 1)];
        return line == 3725 ? QuestText{} : entry(line, rec[std::size_t(2 * state + 2)], quest == 27 ? 1 : 3, state, false);
    }
    if (log_state == 0)
        return (game(13) || game(15)) && !b13 && !bit1 ? entry(3729 + quest_state.multiplayer, 3725, 3, -1, false) : QuestText{};
    if (log_state > 13) return {};
    if (quest == 1) {                                       // the Den (name 3714): its count
        QuestText text = entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
        if (log_state == 3 || log_state == 4) { if (quest_state.den_left >= 2) text.count = quest_state.den_left; else text.string = 3739; }
        return text;
    }
    int line = rec[std::size_t(2 * log_state + 1)];
    if ((!b13 && !bit1 && game(13)) || flag(14)) line = 3729 + quest_state.multiplayer;   // "done by another player"
    else if (line == 3727 && !quest_state.multiplayer) line = 3729;
    // Rescue on Mount Arreat's line 22624 ("Rescue %d more Soldiers ...", quest name 0x585e) prints its count
    // ([0x7bf2ac], 0x4a2016: FUN_005269d0); a count of 0 shows 3729 (0xe91) in its place.
    if (line == 22624 && quest_state.arreat_left == 0) line = 3729;
    QuestText text = entry(line, rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
    if (line == 22624) text.count = quest_state.arreat_left;
    return text;
}

// The quest log's acts (FUN_004a2220): an act's tab is there once the act
// before it is over (quests 7, 15, 23, 26 done; the fifth only in the
// expansion); a tab asked for that isn't falls back to the last that is.
bool quest_act_open(const QuestBits& quest_bits, int act, bool expansion) {
    static constexpr std::array<int, 5> kActDone = { 0, 7, 15, 23, 26 };
    if (act <= 0) return act == 0;
    return act < 5 && (act < 4 || expansion) && quest::bit(quest_bits, kActDone[std::size_t(act)], 0);
}

int quest_log_act(const QuestBits& quest_bits, int act) {
    act = act > 4 ? 0 : act;
    while (act > 0 && !quest_act_open(quest_bits, act, true)) --act;
    return act;
}

int quest_log_pick(const std::vector<QuestLogPick>& entries, int pending, int remembered) {
    if (pending >= 0) return pending;
    int listed = 0, under_way = -1;
    for (const auto& entry : entries) {
        if (entry.fresh || entry.shown == 0) return entry.slot;
        if (entry.shown == 3 && under_way < 0) under_way = entry.slot;
        ++listed;
    }
    if (!listed) return -1;
    return remembered >= 0 ? remembered : under_way;
}

// A message below row 100's (164) is looked for in rows 0..99, any other in
// rows 100 on, so 164 is found at row 100 (bugs.md 46: the old reading,
// 0xe8c, was wrong; tools/emu/quest_name.py).
int quest_name(int message) {
    struct Run { int first, last, name; };
    static constexpr std::array<Run, 32> kRuns{ {
        { 64, 80, 3714 }, { 81, 96, 3715 }, { 97, 126, 3717 }, { 127, 145, 3718 }, { 146, 165, 3716 }, { 166, 184, 3719 },
        { 304, 334, 923 }, { 335, 339, 924 }, { 340, 372, 925 }, { 373, 395, 926 }, { 397, 407, 926 }, { 408, 429, 927 }, { 430, 452, 928 },
        { 527, 542, 933 }, { 543, 548, 931 }, { 549, 570, 930 }, { 571, 593, 932 }, { 594, 627, 934 }, { 628, 635, 935 }, { 637, 663, 935 },
        { 670, 677, 937 }, { 678, 680, 939 }, { 681, 685, 938 }, { 20000, 20001, 938 }, { 20002, 20002, 22641 }, { 20077, 20095, 22618 },
        { 20096, 20115, 22622 }, { 20116, 20136, 22627 }, { 20137, 20137, 22633 }, { 20139, 20152, 22633 }, { 20153, 20168, 22637 }, { 20169, 20180, 22641 } } };
    const auto run = std::ranges::find_if(kRuns, [&](const Run& entry) { return message >= entry.first && message <= entry.last; });
    return run != kRuns.end() ? run->name : 0xe8c;
}

const FindLine* find_line(int level) {
    for (const auto& row : kFindLines)
        if (std::ranges::find(row.levels, level) != row.levels.end() && level != 0) return &row;
    return nullptr;
}



// An act 1 record's per-player send callback (FUN_00544300's fn): FUN_0058fbe0, FUN_00590830, FUN_005912e0,
// FUN_005920d0 and FUN_00595b20 send unless done or closed (bit 0 or 15) without bit 13 or 14; a1q5's
// FUN_00594830 reads no bit 15 (TowerQuest::log_goes_to). tools/emu/quest_log_sends.py.
bool quest_log_goes_to(const QuestBits& quest_bits, int quest) {
    if (quest == TowerQuest::kQuest) return TowerQuest::log_goes_to(quest_bits);
    const bool shut = quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 15);
    return !shut || quest::bit(quest_bits, quest, 13) || quest::bit(quest_bits, quest, 14);
}

// The player joins: the state from their flags (LAB_00590690).
auto DenQuest::join(const QuestBits& quest_bits) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// Into the Den (FUN_00590470, level 8).
auto DenQuest::enter_den(QuestBits& quest_bits) -> void {
    const bool bumped = state == 1 || state == 2;
    if (bumped) state = 3;
    if (log < 2) { log = 2; news = true; }               // flag 1 (0x5904d3)
    else if (!bumped) return;
    mark(quest_bits);
}

auto DenQuest::talk(const QuestBits& quest_bits, int npc) const -> std::vector<QuestMsg> {
    int block = 3;
    if (!quest::bit(quest_bits, kQuest, 1)) {
        if (quest::bit(quest_bits, kQuest, 0) || (state > 3 && !quest::bit(quest_bits, kQuest, 13))) return {};
        block = state - 1;                    // 0x736cd0: -1, 0, 1, 2, 3, 4
        if (block < 0 || block > 4) return {};
    }
    std::vector<QuestMsg> out;
    for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// Whether `npc` has something to say about it — the balloon over its
// head (FUN_005905b0, asked per NPC by FUN_00544590): Akara, before
// she's given it or while the reward is due.
auto DenQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc != kAkara || quest::bit(quest_bits, kQuest, 0)) return false;
    return (state == 1 && !quest::bit(quest_bits, kQuest, 1)) || quest::bit(quest_bits, kQuest, 1);
}

// A Den monster died (FUN_00590260): its log 4 sends are flag 1.
auto DenQuest::killed(int remaining, bool all_up) -> Kill {
    if (state >= 4) return Kill::none;                   // its callback's gone once cleared
    if (all_up && remaining == 0) {
        state = 4;
        return Kill::cleared;
    }
    if (all_up && remaining < 6) {
        log = 4;                                         // "Monsters remaining: N"
        news = true;                                     // flag 1, each death (0x59041d)
        return Kill::few;
    }
    if (log != 4 || remaining <= 5) return Kill::none;
    news = true;                                         // sent again (0x590455)
    return Kill::few;
}

// The clear's credit for one player neither done nor due: a participant
// bits 13 and 1, the reward due (FUN_005455f0), any other bit 14 (cleared
// by someone else, FUN_00590080). True: a participant's.
auto DenQuest::credit(QuestBits& quest_bits, bool listed) -> bool {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
    quest::set(quest_bits, kQuest, listed ? 13 : 14);
    if (listed) quest::set(quest_bits, kQuest, 1);
    return listed;
}

// The player heard `string` from `npc` (FUN_0058fdd0, packet 0x31).
// True: Akara's reward — a skill point (stat 5 + 1), done; quest 41
// (her respec) opens; bits 2..11 cleared (FUN_0065c3e0). The state
// going to 5 runs the chain (+0xf0), then log 13 (FUN_0058fbe0).
auto DenQuest::said(QuestBits& quest_bits, int npc, int string) -> bool {
    if (npc != kAkara) return false;
    if (string == 64) { state = 2; mark(quest_bits); return false; }
    if (string != 76 || !quest::bit(quest_bits, kQuest, 1)) return false;
    if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 0xd; }
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    quest::set(quest_bits, kRespecQuest, 13);
    quest::set(quest_bits, kRespecQuest, 1);
    for (int bit = 2; bit < 12; ++bit) quest::set(quest_bits, kQuest, bit, false);
    return true;
}

// The player joins (LAB_00591180); a done or closed quest is shut for
// the game (FUN_00546270 → FUN_00544410).
auto BurialQuest::join(const QuestBits& quest_bits) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// The chain reaches it (FUN_005910f0): the Den's done or closed. True:
// it passes on to quest 4's (+0x10).
auto BurialQuest::chain() -> bool {
    if (state == 0 && open) state = 1;
    return state == 5 || !open;
}

auto BurialQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    int block = -1;
    if (quest::bit(quest_bits, kQuest, 1)) block = 3;
    else if (rewarded.has(player)) block = 4;
    else if (state >= 1 && state <= 3 && !quest::bit(quest_bits, kQuest, 0)) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The balloon over `npc` (FUN_00591080).
auto BurialQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc != kKashya || quest::bit(quest_bits, kQuest, 0)) return false;
    return (state == 1 && !quest::bit(quest_bits, kQuest, 1)) || quest::bit(quest_bits, kQuest, 1);
}

// The player heard `string` from `npc` (FUN_00590980). True: Kashya's
// reward — done, and a free hireling (FUN_00579180). The state going
// to 5 runs the chain (+0xf0).
auto BurialQuest::said(QuestBits& quest_bits, int npc, int string, int player) -> bool {
    if (npc != kKashya) return false;
    if (string == 81) { pending = true; state = 2; mark(quest_bits); return false; }
    if (string != 92 || !quest::bit(quest_bits, kQuest, 1)) return false;
    if (quest::bit(quest_bits, kQuest, 13) && state != 5) { log = 0xd; state = 5; }
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    rewarded.add(player);
    return true;
}

// The talk with `npc` closed (LAB_00590920).
auto BurialQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {
    if (npc != kKashya || !pending) return;
    log = 1; pending = false; news = true;               // flag 1 (0x59094c)
    mark(quest_bits);
}

// The player went from level `from` to `dest` (FUN_00590fa0).
auto BurialQuest::enter(QuestBits& quest_bits, int from, int dest, int player) -> void {
    if (dest == kBurial && open) {
        const bool bumped = state < 3;
        if (bumped) state = 3;
        if (log <= 1) { log = 2; news = true; mark(quest_bits); return; }   // flag 1 (0x590ff4)
        if (bumped) { mark(quest_bits); return; }
    }
    if (from != 1) return;
    rewarded.remove(player);                         // FUN_00545310
    if (state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
}

// Blood Raven died (FUN_00590ec0). True: the player's kill for it
// (FUN_00590c40, bits 13 and 1) — the class's act1_complete_burial
// (event 0x22). `near`: the player's room is the killer's or next to it.
auto BurialQuest::killed(QuestBits& quest_bits, bool nearby) -> bool {
    if (!open) return false;
    state = 4;
    const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (first && nearby) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); }
    else if (first) quest::set(quest_bits, kQuest, 14);             // LAB_00590dd0
    log_in = quest_timer_steps(0xf);
    pending = false;                          // +0xa8 = 0
    return first && nearby;
}

// The player joins (0x596900).
auto AndyQuest::join(const QuestBits& quest_bits) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// A quest-clock step (QuestClock). 1: the kill's town portal is due at
// the player, if they're in Andariel's lair (her timer's 10, FUN_00596490).
auto AndyQuest::tick() -> int {
    if (start_in > 0 && --start_in == 0 && state == 0) state = 1;
    if (after_kill == 0) { after_kill_steps = 0; return 0; }
    if (++after_kill_steps < quest_timer_steps(1)) return 0;
    after_kill_steps = 0;
    ++after_kill;
    if (after_kill == 12) {
        if (log != 3 && log != 0xd) { log = 3; news = true; }   // flag 1 (0x59656a)
        after_kill = 0;
    }
    return after_kill == 10;
}

auto AndyQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0), b13 = quest::bit(quest_bits, kQuest, 13);
    const bool listed = (npc == kCain && cain.has(player)) || (npc == kAkara && akara.has(player)) || (npc == kKashya && kashya.has(player));
    const bool cak = npc == kCain || npc == kAkara || npc == kKashya;
    int block = -1;
    if (listed) block = 3;
    else if (quest::bit(quest_bits, kQuest, 1)) block = cak ? 4 : 3;
    else if (rewarded.has(player)) block = 4;
    else if (state == 1 && npc == kCain && !bit0) block = 0;
    else if (state != 0 && !(bit0 && !b13) && !(state >= 4 && !b13) && !(bit0 && b13)) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0 && block <= 4)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The balloon over `npc` (FUN_005967f0).
auto AndyQuest::alert(const QuestBits& quest_bits, int npc, int player) const -> bool {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1);
    if (npc == kCain) return cain.has(player) || (!bit0 && state == 1 && !bit1);
    if (npc == kWarriv) return !bit0 && bit1;
    return (npc == kAkara && akara.has(player)) || (npc == kKashya && kashya.has(player));
}

// The player heard `string` from `npc` (FUN_00595c60). True: Warriv's
// reward — done (bit 0), and the quest's complete message.
auto AndyQuest::said(QuestBits& quest_bits, int npc, int string, int player) -> bool {
    if (npc == kCain && string == 166) { state = 2; cain_pending = true; mark(quest_bits); }
    if (npc == kCain && string == 184) cain.remove(player);
    if (npc == kAkara && string == 179) akara.remove(player);
    if (npc == kKashya && string == 181) kashya.remove(player);
    if (npc != kWarriv || string != 183 || !quest::bit(quest_bits, kQuest, 1)) return false;
    if (quest::bit(quest_bits, kQuest, 13)) { log = 0xd; state = 5; }
    quest::set(quest_bits, kQuest, 1, false);
    quest::set(quest_bits, kQuest, 0);
    rewarded.add(player);
    return true;
}

// The talk with `npc` closed (0x595b80).
auto AndyQuest::talk_closed(int npc) -> void {
    if (npc == kCain && cain_pending) { log = 1; cain_pending = false; news = true; }   // flag 1 (0x595baf)
}

// The player went from level `from` to `dest` (FUN_00596010). Into
// Catacombs 4 (0x25) under log 2: log 2 (0x596058, flag 1); into another
// Catacombs level (0x22..0x24) with log 0: log 1 (0x596087, flag 0).
auto AndyQuest::enter(QuestBits& quest_bits, int from, int dest) -> void {
    if (dest < 34 || dest > 37) {
        if (state == 4 && dest == level_ids::kLutGholein) state = 5;
        else if (from == 1 && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
        return;
    }
    if (state <= 2) state = 3;
    if (dest == level_ids::kCatacombsLevel4) { if (log < 2) { log = 2; news = true; } }
    else if (log == 0) log = 1;
    mark(quest_bits);
}

// Andariel died (FUN_005965a0). True: the player's kill for the quest
// (bits 13 and 1, FUN_00596210) — Act 2 opens and her quest drop is
// due: two chipped gems, one standard. A merc's or pet's kill is its
// owner's (Fight::Kill::killer).
auto AndyQuest::killed(QuestBits& quest_bits) -> bool {
    const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (first) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); }
    after_kill = 1;
    state = 4;
    return first;
}

// A player in her level at her death (FUN_00596260): Cain, Akara and
// Kashya have news for them, unless they're done or closed.
auto AndyQuest::in_lair(const QuestBits& quest_bits, int player) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) return;
    cain.add(player); akara.add(player); kashya.add(player);
}

auto ActTwoOpening::talk(const QuestBits& quest_bits, int npc, int player, quest::Players& cain_missed) const -> std::vector<QuestMsg> {
    if (npc == kJerhyn && !quest::bit(quest_bits, kQuest, 0)) return { { kGreeting, true } };
    if (npc == kCain && quest::bit(quest_bits, 4, 14) && !told.has(player)) {
        cain_missed.remove(player);
        return { { kRescuedByRogues, true } };
    }
    return {};
}

auto ActTwoOpening::said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player) -> void {
    if (npc == kCain && string == kRescuedByRogues) told.add(player);
    if (npc != kJerhyn || string != kGreeting) return;
    quest::set(quest_bits, kQuest, 0);
    quest::set(game_bits, kQuest, 13);
}

// The player joins (FUN_00599230). Done with the book due but gone: the
// quest's bits go, to be done again in a later game.
auto RadamentQuest::join(QuestBits& quest_bits, QuestBits& game_bits, bool has_book) -> void {
    if (quest::bit(quest_bits, kQuest, 0)) {
        if (quest::bit(quest_bits, kQuest, 5) && !has_book)
            for (const int bit : { 0, 1, 15, 5 }) quest::set(quest_bits, kQuest, bit, false);
        quest::set(game_bits, kQuest, 13);
        open = false; state = 0;
        return;
    }
    if (quest::bit(quest_bits, kQuest, 15)) { reward_closed = quest::bit(quest_bits, kQuest, 1); state = 0; open = false; return; }
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

auto RadamentQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    int block = -1;
    if (quest::bit(quest_bits, kQuest, 1)) block = 3;
    else if (rewarded.has(player)) block = 4;
    else if (!quest::bit(quest_bits, kQuest, 0) && state != 0 && (state < 4 || quest::bit(quest_bits, kQuest, 13))) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0 && block <= 4)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// Atma's "!" (FUN_00598910).
auto RadamentQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc != kAtma || quest::bit(quest_bits, kQuest, 0)) return false;
    return (open && state == 1 && !quest::bit(quest_bits, kQuest, 15)) || quest::bit(quest_bits, kQuest, 1);
}

// The player heard `string` from `npc` (FUN_00598a70). Atma's thanks with
// the reward due: done (the book was the reward).
auto RadamentQuest::said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player) -> void {
    if (npc != kAtma) return;
    if (string == kInit) { pending = true; state = 2; mark(quest_bits); return; }
    if (string != kSuccess || !quest::bit(quest_bits, kQuest, 1)) return;
    if (quest::bit(quest_bits, kQuest, 13)) {
        if (state != 5) { log = 0xd; pending = false; state = 5; }
        if (!open) quest::set(game_bits, kQuest, 13);
    }
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    rewarded.add(player);
}

// The talk with `npc` closed (LAB_00598990).
auto RadamentQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {
    if (npc != kAtma || !pending) return;
    log = 1; pending = false;
    mark(quest_bits);
}

// The player went from level `from` (LAB_00599130): out of town.
auto RadamentQuest::enter(QuestBits& quest_bits, int from, int player) -> void {
    if (from != level_ids::kLutGholein) return;
    rewarded.remove(player);                         // FUN_00545310
    if (state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
}

auto RadamentQuest::killed(QuestBits& quest_bits, QuestBits& game_bits, bool nearby, bool has_book) -> Kill {
    if (!open) return {};
    state = 4;
    quest::set(game_bits, kQuest, 13);
    pending = false;                          // +0xa8 = 0
    const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (first && nearby) for (const int bit : { 13, 1, 5 }) quest::set(quest_bits, kQuest, bit);
    else if (first) quest::set(quest_bits, kQuest, 14);   // LAB_00598dc0
    if (log_in == 0) log_in = quest_timer_steps(0xc);
    return { quest::bit(quest_bits, kQuest, 5) && !has_book ? 1 : 0, quest::bit(quest_bits, kQuest, 13) };
}

// The Book of Skill read (0x55cd31 / 0x55e3d0): only while bit 5 is set;
// it clears, and a skill point (stat 5).
auto RadamentQuest::read_book(QuestBits& quest_bits) -> bool {
    if (!quest::bit(quest_bits, kQuest, 5)) return false;
    quest::set(quest_bits, kQuest, 5, false);
    return true;
}

// A later player's +0xd8 (FUN_0059e970) counts the parts they carry
// (data +0x18..+0x24, not kept here) and, with data +0x2b set (not
// traced), resets the Tombs' data +0x10 when its +0xe isn't 1.
// The player joins (FUN_0059e850): parts no longer carried are no
// longer shown, unless all three are, the staff's made or placed.
auto HoradricStaffQuest::join(QuestBits& quest_bits, const Items& items, bool staff_placed) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) open = false;
    if (quest::bit(quest_bits, kQuest, 0) || staff_placed || items.staff || (items.shaft && items.cube && items.amulet)) return;
    if (!items.shaft) quest::set(quest_bits, kQuest, 5, false);
    if (!items.cube) quest::set(quest_bits, kQuest, 6, false);
    if (!items.amulet) quest::set(quest_bits, kQuest, 4, false);
}

auto HoradricStaffQuest::talk(const QuestBits& quest_bits, int npc, const Items& items, int player) const -> std::vector<QuestMsg> {
    if (npc != kCain) return {};
    int block = 4;
    if (!quest::bit(quest_bits, kQuest, 1)) {
        const auto found = pick(quest_bits, items);
        if (found.news && found.block >= 0) block = found.block;
        else if (rewarded.has(player)) block = 5;
        else if (quest::bit(quest_bits, kQuest, 0)) return {};
        else if (found.block < 0) { if (!quest::bit(quest_bits, kQuest, 3)) return {}; block = 6; }
        else block = found.block;
    }
    return { { kLines[std::size_t(block)], block < 5 } };
}

// Cain's "!" (FUN_005996c0): the reward due, or a part to show.
auto HoradricStaffQuest::alert(const QuestBits& quest_bits, int npc, const Items& items) -> bool {
    return npc == kCain && (quest::bit(quest_bits, kQuest, 1) || pick(quest_bits, items).news);
}

// The player heard `string` from Cain (FUN_005997f0). True: he read the
// scroll, which goes (FUN_00544160 -> FUN_005440a0).
auto HoradricStaffQuest::said(QuestBits& quest_bits, int npc, int string, int player) -> bool {
    if (npc != kCain) return false;
    switch (string) {
    case 335: quest::set(quest_bits, kQuest, 3); return true;
    case 336: quest::set(quest_bits, kQuest, 4); break;
    case 337: quest::set(quest_bits, kQuest, 5); break;
    case 338: quest::set(quest_bits, kQuest, 6); break;
    case 339:
        quest::set(quest_bits, kQuest, 1, false);
        rewarded.add(player);
        for (const int bit : { 10, 4, 6, 5 }) quest::set(quest_bits, kQuest, bit);
        break;
    default: return false;
    }
    quest::set(quest_bits, kQuest, 3);
    return false;
}

// The log (+0xe8, FUN_0059e630): nothing till Act 1's over; the parts
// as carried and shown; 9 when one's been lost (the Cube or shaft after
// its chest, the amulet after the altar); once done 5 / 6 till the
// Seven Tombs, then 11 / 13.
auto HoradricStaffQuest::log_state(const QuestBits& quest_bits, const Items& items, bool altar_used) const -> int {
    auto bit = [&](int index) { return quest::bit(quest_bits, kQuest, index); };
    if (!quest::bit(quest_bits, 7, 0)) return 0;
    if (bit(0) || bit(1)) return !quest::bit(quest_bits, 14, 0) ? (bit(10) ? 5 : 6) : bit(13) ? 13 : 11;
    if (items.shaft && items.cube && items.amulet) return bit(6) ? 3 : bit(3) ? 2 : 4;
    if (items.staff) return bit(10) ? 5 : 6;
    if (items.scroll && !bit(3)) return 1;
    const bool lost = (cube_dropped && !items.cube) || (!items.amulet && altar_used) || (shaft_dropped && !items.shaft);
    if (bit(3)) return 2;
    if (items.amulet || items.shaft || items.cube) return 4;
    return lost ? 9 : 0;
}

// A quest chest opened (OperateFns 40 / 41 / 39: FUN_00599c10,
// FUN_00599cf0, FUN_00599df0): its part for each player who wants one
// (the scroll: no bit 0 or 3; the shaft, identified: no bit 0, no shaft
// or staff; the Cube: none carried). The caller adds the act's chest
// class forced magic (FUN_00585b90, EDX 4) and rand(5) + 5 gold piles
// on the quest seed.
auto HoradricStaffQuest::chest(int operate_fn, const QuestBits& quest_bits, const Items& items) -> std::string {
    if (operate_fn == kScrollChest) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 3) ? "tr1" : "";
    if (operate_fn == kShaftChest) {
        if (quest::bit(quest_bits, kQuest, 0) || items.shaft || items.staff) return "";
        shaft_dropped = true;
        return "msf";
    }
    if (operate_fn != kCubeChest || items.cube) return "";
    cube_dropped = true;
    return "box";
}

// A joined client's palace guard on his "!" (S->C 0x8a, FUN_004b3380):
// "Halt" (Sounds.txt 3983 guard_halt) with no talk open (FUN_004b1620,
// a talk with anyone), the player's bits 8 and 1 both clear
// (FUN_0065c310) and no harem blocker (object 318) in mode 2.
auto ArcaneQuest::guard_halts(bool talk_open, const QuestBits& quest_bits, bool harem_open) -> bool {
    return !talk_open && !harem_open && !quest::bit(quest_bits, kQuest, 8) && !quest::bit(quest_bits, kQuest, 1);
}

// The Horadric Staff made (FUN_0059b660).
auto ArcaneQuest::staff_made(QuestBits& quest_bits) -> void {
    if (!open) { let_in(quest_bits); return; }
    if (state == 0) state = 1;
    if (log == 0) log = 1;
}

// The player joins (FUN_0059b530).
auto ArcaneQuest::join(const QuestBits& quest_bits, QuestBits& game_bits, bool has_staff) -> void {
    auto bit = [&](int index) { return quest::bit(quest_bits, kQuest, index); };
    if (bit(0) || bit(15)) open = false;
    if (quest::bit(quest_bits, 11, 0)) { guard_in = true; guard_state = 2; }
    if (bit(0) || bit(1)) { quest::set(game_bits, kQuest, 13); guard_state = 2; guard_in = true; return; }
    if (has_staff) { log = 1; state = 1; }
    bool under_way = true;
    if (bit(5)) { log = 4; state = 4; }
    else if (bit(4)) { log = 3; state = 4; }
    else if (bit(3)) { log = 3; state = 3; }
    else if (bit(2)) { log = 2; state = 2; }
    else under_way = false;
    if (under_way || quest::bit(quest_bits, 10, 0)) { guard_state = 2; guard_in = true; }
}

// The "!" (FUN_0059aca0).
auto ArcaneQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (quest::bit(quest_bits, kQuest, 0)) return false;
    if (npc == kGuard) {
        if (quest::bit(quest_bits, kQuest, 1)) return false;
        if (guard_state == 0) return !quest::bit(quest_bits, kQuest, 7);
        return guard_state == 2 && guard_new && !quest::bit(quest_bits, kQuest, 8);
    }
    if ((npc == kDrognan && state == 1) || (npc == kJerhyn && state == 2)) return !quest::bit(quest_bits, kQuest, 1);
    return false;
}

// The player heard `string` from `npc` (FUN_0059af50). True: the
// journal's 396 heard in its room: the way to the Canyon opens there
// (FUN_0056d130: object 60 to level 46), once; the game's bit 13.
auto ArcaneQuest::said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player_room, int player) -> bool {
    if (npc == kGuard) {
        if (string == 186) quest::set(quest_bits, kQuest, 7);
        else if (string >= 187 && string <= 189) quest::set(quest_bits, kQuest, 8);
        return false;
    }
    if (string >= kThanksFirst && string <= kThanksLast && quest::bit(quest_bits, kQuest, 1)) {
        quest::set(quest_bits, kQuest, 1, false);
        rewarded.add(player);
    }
    if (string == kJournal) {
        if (open && log < 5) log = 5;
        if (!portal_made && player_room == journal_room && journal_room >= 0) {
            portal_made = true;
            quest::set(game_bits, kQuest, 13);
            return true;
        }
        return false;
    }
    if (npc == kDrognan && string == kToJerhyn) { state = 2; pending = true; let_in(quest_bits); }
    if (npc == kJerhyn && string == kToPalace) { state = 3; log = 3; mark(quest_bits); }
    return false;
}

// The talk with `npc` closed (LAB_0059ae60).
auto ArcaneQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {
    if (npc != kDrognan || !pending) return;
    log = 2; pending = false;
    mark(quest_bits);
}

// The player went from `from` to `dest` (FUN_0059f0c0).
auto ArcaneQuest::enter(QuestBits& quest_bits, int from, int dest, int player) -> void {
    if (dest == kSanctuary) {
        const bool before = state <= 3;
        if (before) state = 4;
        if (log < 4) log = 4;
        else if (!before) return;
        mark(quest_bits);
        return;
    }
    if (dest == kHarem) { quest::set(quest_bits, kQuest, 8); quest::set(quest_bits, kQuest, 7); }
    if (from == level_ids::kLutGholein) {
        rewarded.remove(player);                     // FUN_00545310
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && state == 3) { state = 4; mark(quest_bits); }
    }
}

// Horazon's Journal read (OperateFn 42, FUN_0059b970): its 396 to the
// player (FUN_005456a0), its room kept. True: open and not done, now
// state 5; each player then takes their credit (journal_credit).
auto ArcaneQuest::journal(int room) -> bool {
    journal_room = room;
    if (!open || state == 5) return false;
    state = 5;
    return true;
}

// A player's credit at the first read: in the Sanctuary without bit 0 /
// 1, 13, 1, 0 (their 2..11 cleared, then 8, 7: FUN_0059b3f0); else bit
// 14 (LAB_0059b320).
auto ArcaneQuest::journal_credit(QuestBits& quest_bits, bool in_sanctuary) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    if (in_sanctuary) {
        for (const int bit : { 13, 1, 0 }) quest::set(quest_bits, kQuest, bit);
        for (int bit = 2; bit < 12; ++bit) quest::set(quest_bits, kQuest, bit, false);
        quest::set(quest_bits, kQuest, 8);
        quest::set(quest_bits, kQuest, 7);
    } else quest::set(quest_bits, kQuest, 14);
}

auto SummonerQuest::engaged(QuestBits& quest_bits) -> void {      // FUN_0059c330
    if (!open) return;
    started = true;
    if (state == 0) state = 1;
    if (log < 2) { log = 2; mark(quest_bits); }
}

// He died (FUN_0059c150). True: the player took part (FUN_0059be70:
// their room is his or next to it): bits 13 and 1; else bit 14
// (LAB_0059c0f0).
auto SummonerQuest::killed(QuestBits& quest_bits, bool nearby) -> bool {
    if (!open) return false;
    state = 2;
    if (log_in == 0) log_in = quest_timer_steps(3);
    const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (first && nearby) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); return true; }
    if (first) quest::set(quest_bits, kQuest, 14);
    return false;
}

// A quest-clock step. The timer (LAB_0059bfd0) runs twice: first (data+9
// clear) each player with bit 13 in the Sanctuary says act2_complete_summoner
// (voice 0x33, LAB_0059bf80) and it goes again 3 ticks on (returns 0, at
// 0x59bfee); then log 4 (FUN_00544300 at 0x59c006) and it's done. True: the
// voice.
auto SummonerQuest::tick() -> bool {
    if (log_in == 0 || --log_in > 0) return false;
    if (!voiced) { voiced = true; log_in = quest_timer_steps(3); return true; }
    if (open) log = 4;
    return false;
}

// FUN_0059bbc0: the reward due → block 1; rewarded → 2; else by the
// state (0x73b278: -1, 0, 1, 2).
auto SummonerQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    int block = -1;
    const bool b13 = quest::bit(quest_bits, kQuest, 13);
    if (quest::bit(quest_bits, kQuest, 1)) block = 1;
    else if (rewarded.has(player)) block = 2;
    else if (open && state != 0 && !(quest::bit(quest_bits, kQuest, 0) && !b13) && !(state > 1 && !b13)) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0 && block < 3)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

auto SummonerQuest::said(QuestBits& quest_bits, QuestBits& game_bits, int string, int player) -> void {     // FUN_0059bd70
    if (string < kThanksFirst || string > kThanksLast || !quest::bit(quest_bits, kQuest, 1)) return;
    if (quest::bit(quest_bits, kQuest, 13)) { log = 0xd; quest::set(game_bits, kQuest, 13); state = 3; }
    rewarded.add(player);
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
}

auto SevenTombsQuest::join(QuestBits& quest_bits) -> void {           // LAB_0059d4f0
    quest::set(quest_bits, 10, 9, false);
    if (quest::bit(quest_bits, 10, 0)) { later = true; staff_placed = true; }
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
    if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
    else if (quest::bit(quest_bits, kQuest, 3)) { log = 5; state = 5; }
    else if (quest::bit(quest_bits, kQuest, 4)) { log = 6; state = 5; }
}

// The log (+0xe8, FUN_0059ca50).
auto SevenTombsQuest::log_state(const QuestBits& quest_bits) const -> int {
    if (!quest::bit(quest_bits, 7, 0) || quest::bit(quest_bits, kQuest, 0)) return 0;
    if (quest::bit(quest_bits, kQuest, 3)) return 5;
    if (quest::bit(quest_bits, kQuest, 4)) return 6;
    if (!open) return 0;
    if (quest::bit(quest_bits, kQuest, 5) || state < 3) return log;
    return 0xc;
}

// The player went from `from` to `dest` (FUN_0059d1c0): levels 40..74.
auto SevenTombsQuest::enter(QuestBits& quest_bits, int from, int dest, int player) -> void {
    if (dest < level_ids::kLutGholein || dest > level_ids::kArcaneSanctuary) return;
    if (from == level_ids::kLutGholein) rewarded.remove(player);
    if (!open) return;
    if (!duriel_dead && true_tomb) {
        if (dest == true_tomb) {
            if (state == 2) return;
            state = 2;
            if (log <= 1) log = 2;
            mark(quest_bits);
            return;
        }
        if (dest == kCanyon) {
            if (state == 0) { state = 2; mark(quest_bits); }
            if (log == 0) log = 1;
            return;
        }
    }
    if (dest != kLair || state > 1) return;
    state = 2;
    if (log <= 1) log = 2;
    mark(quest_bits);
}

// `summoner_done`: the game's quest 13 bit 13 (Drognan stays quiet in block 1).
auto SevenTombsQuest::talk(const QuestBits& quest_bits, int npc, bool summoner_done, int player) const -> std::vector<QuestMsg> {
    auto lines = [&](int block) {
        std::vector<QuestMsg> out;
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    };
    if (npc == kTyrael) return tyrael_talks ? lines(2) : std::vector<QuestMsg>{};
    if (quest::bit(quest_bits, kQuest, 13))
        for (const auto& [who, bit] : kTownLines) if (who == npc && !quest::bit(quest_bits, kQuest, bit)) return lines(6);
    if (quest::bit(quest_bits, kQuest, 3)) return lines(3);
    if (quest::bit(quest_bits, kQuest, 4)) return npc == kMeshif ? lines(5) : lines(4);
    if (rewarded.has(player)) return lines(4);
    if (state == 0) return {};
    const bool b13 = quest::bit(quest_bits, kQuest, 13);
    if ((quest::bit(quest_bits, kQuest, 0) && !b13) || (state > 3 && !b13)) return {};
    const int block = state - 1;
    if (block == 1 && npc == 177 && summoner_done) return {};
    if (block < 0 || block > 4) return {};
    return lines(block);
}

// FUN_0059d300.
auto SevenTombsQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc == kJerhyn) {
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        return (state == 1 && !quest::bit(quest_bits, kQuest, 3) && !quest::bit(quest_bits, kQuest, 4)) || quest::bit(quest_bits, kQuest, 3);
    }
    if (npc == kMeshif) return quest::bit(quest_bits, kQuest, 4);
    if (npc == kTyrael) return open && duriel_dead && !tyrael_portal;
    if (!quest::bit(quest_bits, kQuest, 13)) return false;
    for (const auto& [who, bit] : kTownLines) if (who == npc) return !quest::bit(quest_bits, kQuest, bit);
    return false;
}

auto SevenTombsQuest::said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool in_lair, int player) -> Heard {
    Heard out;
    if (npc == kTyrael) {
        if (!open || string != kTyraelLine || tyrael_portal) return out;
        out.town_portal = true;
        state = 4;
        if (!quest::bit(quest_bits, kQuest, 13) && !quest::bit(quest_bits, kQuest, 3) && !quest::bit(quest_bits, kQuest, 4) && in_lair) {   // FUN_0059c860
            quest::set(quest_bits, kQuest, 13);
            quest::set(quest_bits, kQuest, 3);
            out.act3 = true;
        }
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 3) && !quest::bit(quest_bits, kQuest, 4)) quest::set(quest_bits, kQuest, 14);   // LAB_0059c9f0
        tyrael_portal = true;
        tyrael_pending = true;
        return out;
    }
    if (npc == kJerhyn && string == kStart) { state = 2; jerhyn_pending = true; return out; }
    if (npc == kJerhyn && string == kReward) {
        if (!quest::bit(quest_bits, kQuest, 3)) return out;
        if (quest::bit(quest_bits, kQuest, 13)) state = 5;
        reward_pending = true;
        quest::set(quest_bits, kQuest, 4);
        quest::set(quest_bits, kQuest, 3, false);
        return out;
    }
    if (npc == kMeshif && string == kMeshifLine) {
        if (!quest::bit(quest_bits, kQuest, 4)) return out;
        out.take_staff = quest::bit(quest_bits, 10, 0);
        if (quest::bit(quest_bits, kQuest, 13)) { quest::set(game_bits, kQuest, 13); state = 5; log = 0xd; }
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 4, false);
        rewarded.add(player);
        return out;
    }
    static constexpr std::array<std::pair<int, int>, 6> kLineBits{ { { 444, 9 }, { 445, 6 }, { 446, 7 }, { 447, 11 }, { 449, 8 }, { 452, 10 } } };
    for (const auto& [line, bit] : kLineBits) if (string == line) quest::set(quest_bits, kQuest, bit);
    return out;
}

auto SevenTombsQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {   // LAB_0059c760
    if (npc == kTyrael) { if (tyrael_pending) { log = 4; tyrael_pending = false; } return; }
    if (npc != kJerhyn) return;
    if (jerhyn_pending) { log = 1; jerhyn_pending = false; }
    else if (reward_pending) { log = 6; reward_pending = false; }
    else return;
    mark(quest_bits);
}

// Duriel died (FUN_0059d050). `by_him`: the player at his death without
// bit 0 / 3 / 4 / 5 gets bit 5 (FUN_0059cfb0: in the Lair).
auto SevenTombsQuest::duriel_died(QuestBits& quest_bits, bool by_him) -> void {
    if (open) { state = 3; if (log_in == 0) log_in = quest_timer_steps(8); }
    if (by_him && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 3) && !quest::bit(quest_bits, kQuest, 4)) quest::set(quest_bits, kQuest, 5);
    duriel_dead = true;
    tyrael_talks = true;
}

// The staff into the orifice (FUN_0059dd80). The Horadric Staff done
// (quest 10 bits 0, 13; the staff and its parts taken), the timer to the
// way down (FUN_0059d870) once, of `ticks` (staff_ticks).
auto SevenTombsQuest::staff_in(QuestBits& quest_bits, int ticks) -> void {
    quest::set(quest_bits, 10, 0);
    quest::set(quest_bits, 10, 13);
    staff_placed = true;
    if (!timer_started) { timer_started = true; open_in = ticks; }
}

// The orifice's room comes up (InitFn 21, FUN_0059db50). True: it's
// open (mode 2): the staff went in, in this game or an earlier one.
auto SevenTombsQuest::orifice_seen() -> bool {
    if (later && !timer_started && !lair_open) { timer_started = true; open_in = 1; return true; }
    return lair_open;
}

// A quest-clock step. True: the way down opens (object 100 by the orifice).
auto SevenTombsQuest::tick() -> bool {
    if (log_in > 0 && --log_in == 0 && log != 3 && log != 4 && log != 5) log = 3;
    if (open_in < 0 || open_in-- > 0) return false;
    timer_started = false;
    if (!staff_placed) return false;
    staff_placed = false;
    lair_open = true;
    return true;
}

// The player joins (FUN_0059a5d0); done or closed: shut for the game.
auto TaintedSunQuest::join(const QuestBits& quest_bits, QuestBits& game_bits) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) open = false;
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) { quest::set(game_bits, kQuest, 13); return; }
    if (!quest::bit(quest_bits, kQuest, 2)) return;
    darken();                                 // FUN_0059a570: their sky's dark again
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { log = 2; state = 2; }
    else { log = 1; state = 1; }
}

// The player went from level `from` to `dest` (FUN_0059ee00). True:
// the desert's far end starts the darkness's timer, rand(17) ticks on
// the quest seed (FUN_0059edc0), which the caller rolls into dark_in.
auto TaintedSunQuest::enter(int from, int dest, int player) -> bool {
    const bool starts = (dest == kLostCity || dest == kValley) && state == 0 && open && dark_in < 0 && !dark;
    if (from == level_ids::kLutGholein) {
        rewarded.remove(player);                     // FUN_00545310
        if (state == 2) state = 3;
    }
    return starts;
}

// A quest-clock step (QuestClock). The darkness's dark_in counts ticks:
// it goes on the (dark_in + 1)th step.
auto TaintedSunQuest::tick(QuestBits& quest_bits) -> void {
    if (dark_in >= 0 && dark_in-- == 0 && !dark) { darken(); mark(quest_bits); }   // LAB_0059ed80
    if (log_in > 0 && --log_in == 0 && state == 4) log = 3;
}

// The altar's room comes up (InitFn 20 -> FUN_0059a3f0): in the temple,
// dark if not yet; log 2 if none. Its mode: 2 once used.
auto TaintedSunQuest::altar_seen() -> void {
    if (!open) return;
    if (state < 2) { darken(); state = 3; }
    if (log == 0) log = 2;
}

auto TaintedSunQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    int block = -1;
    if (quest::bit(quest_bits, kQuest, 1)) block = 3;
    else if (quest::bit(quest_bits, kQuest, 14)) return {};
    else if (rewarded.has(player)) block = 4;
    else if (state != 0 && open && !quest::bit(quest_bits, kQuest, 0) && state <= 3) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The "!" (FUN_0059a1c0): the reward due, over the town's people; or
// Drognan while it's new.
auto TaintedSunQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    static constexpr std::array<int, 10> kTown{ 176, 175, 198, 199, 177, 202, 244, 210, 200, 201 };
    if (quest::bit(quest_bits, kQuest, 0)) return false;
    return (quest::bit(quest_bits, kQuest, 1) && std::ranges::find(kTown, npc) != kTown.end()) || (npc == kDrognan && state == 1);
}

// The player heard `string` from `npc` (LAB_0059ebe0): Drognan's 348
// tells it; any thanks (362..372) with the reward due is done.
auto TaintedSunQuest::said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player) -> void {
    if (string == kTold) {
        if (!open || npc != kDrognan) return;
        if (log == 1) log = 2;
        if (state == 1) { state = 2; mark(quest_bits); }
        return;
    }
    if (string < kThanksFirst || string > kThanksLast) return;
    if (state != 5 && open && quest::bit(quest_bits, kQuest, 13)) { log = 0xd; state = 5; }
    if (!quest::bit(quest_bits, kQuest, 1)) return;
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    rewarded.add(player);
    if (!open) quest::set(game_bits, kQuest, 13);
}

auto TaintedSunQuest::use_altar(QuestBits& quest_bits, QuestBits& game_bits, bool has_amulet, bool has_staff) -> Use {
    const bool has_it = quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1);
    if (has_it && (quest::bit(quest_bits, 10, 0) || has_amulet || has_staff)) return { .refused = true };
    if (used) return {};
    used = true;
    Use out{ .used = true };
    out.amulets = wants_amulet(quest_bits, has_amulet, has_staff) ? 1 : 0;
    if (!open) return out;
    dark = false;
    state = 4;
    out.took_part = credit(quest_bits);       // LAB_0059a730: voice 0x34
    quest::set(game_bits, kQuest, 13);
    log_in = quest_timer_steps(10);
    return out;
}

// The chain reached it (+0xf0, FUN_00593d70, from quest 2's): state
// 0 → 1. True: it passes on to quest 3's (+0x10).
auto CainQuest::open() -> bool {
    if (state == 0 && active) { state = 1; return false; }
    return state == 6 || !active;
}

// A later player's +0xd8 (FUN_00592b90) only adds each scroll they carry
// to +0x7c, which the rewind left out above alone reads.
// The player joins, carrying the scroll (bks) / the deciphered one (bkd) (FUN_00597030).
auto CainQuest::join(const QuestBits& quest_bits, bool bks, bool bkd) -> void {
    active = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!active) {
        camp_due = true;
        if (quest::bit(quest_bits, kQuest, 0)) rescued_flag = true;
        else closed_join = true;
        rejoined = stones_open = true;
    } else if (quest::bit(quest_bits, kQuest, 4)) {
        log = 4; state = 5;
        rejoined = deciphered = stones_open = tree_used = true;
    } else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    if (bkd) { state = 5; log = 3; deciphered = tree_used = true; }
    else if (bks) { state = 4; log = 2; tree_used = true; }
}

auto CainQuest::talk(const QuestBits& quest_bits, int npc, bool bks, int player) const -> std::vector<QuestMsg> {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1), b13 = quest::bit(quest_bits, kQuest, 13), b14 = quest::bit(quest_bits, kQuest, 14);
    int block = -1;
    if (npc == kCampCain && !thanked.has(player) && b13) block = 5;
    else if (bit1) block = npc == kCampCain && thanked.has(player) ? 7 : 5;
    else if (missed.has(player)) block = 6;
    else if (rewarded.has(player)) block = npc == kCampCain && !thanked.has(player) ? 5 : b14 ? 8 : bit0 ? 7 : -1;
    else if (!b14 && state != 0 && !bit0 && !quest::bit(quest_bits, kQuest, 15)) block = bks ? 3 : state == 4 ? 2 : state <= 5 ? state - 1 : -1;
    if (npc == kCain) block = 9;
    std::vector<QuestMsg> out;
    if (block >= 0)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The balloon over `npc` (FUN_00592fb0).
auto CainQuest::alert(const QuestBits& quest_bits, int npc, bool bks, int player) const -> bool {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1), b13 = quest::bit(quest_bits, kQuest, 13);
    if (npc == kAkara) return (state == 4 && !bit0 && !bit1 && bks) || (state == 1 && !bit0 && !bit1) || (state == 6 && b13 && !bit0) || bit1;
    return npc == kCampCain && (missed.has(player) || (!thanked.has(player) && b13));
}

auto CainQuest::said(QuestBits& quest_bits, int npc, int string, bool bks, int player) -> Said {
    if (npc == kCampCain) {
        if (string == 125) { rewarded.add(player); missed.remove(player); }
        if (string == 126 || string == 123) thanked.add(player);
        return Said::none;
    }
    if (npc != kAkara) return Said::none;
    if (string == 97) { akara_log = true; state = 2; return Said::none; }
    if (string == 112 && bks) {
        decipher_log = deciphered = true;
        state = 5; log = 3;
        return Said::decipher;
    }
    if (string != 118 || !quest::bit(quest_bits, kQuest, 1)) return Said::none;
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    rewarded.add(player);
    if (quest::bit(quest_bits, kQuest, 13)) {
        log = 0xd; state = 6;
        rescued_flag = true;
    }
    if (closed_join) rescued_flag = true;
    return Said::ring;
}

// The talk with `npc` closed (FUN_005921b0).
auto CainQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {
    if (npc != kAkara) return;
    if (akara_log) { log = 1; akara_log = false; news = true; mark(quest_bits); }      // flag 1 (0x5921e9)
    if (decipher_log) { log = 3; mark(quest_bits); decipher_log = false; news = true; }   // flag 1 (0x59221f)
}

// The player went from level `from` to `dest` (FUN_00596de0). True:
// camp Cain's due in the camp (FUN_00592960).
auto CainQuest::enter(QuestBits& quest_bits, int from, int dest, int player) -> bool {
    if (dest == kTristram && !camp_cain && !resolved && state > 5) { state = 5; log = 4; mark(quest_bits); }
    const bool fresh = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (from == level_ids::kRogueEncampment) {
        rewarded.remove(player); missed.remove(player);
        if (fresh && state == 2) state = 3;          // the player's bit waits for item_event (0x596eef)
    }
    if (dest == level_ids::kRogueEncampment) return camp_spawn();
    if (dest == level_ids::kLutGholein && fresh && !resolved && state < 6) { rogues(quest_bits, player); tree_used = true; }
    return false;
}

// The Tree of Inifuss (OperateFn 12, FUN_00593af0). True: the scroll
// (bks) drops at the tree.
auto CainQuest::tree(const QuestBits& quest_bits, bool has_scroll) -> bool {
    if (!active || state >= 6 || tree_used || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || has_scroll) return false;
    state = 4; log = 2; news = true;                     // flag 1 (0x593c3d)
    tree_used = true;
    return true;
}

// The stones' order from the quest rng (game+0x10f4 +0x18, drawn on in
// place; FUN_00592e90): seed = low * 0x6ac690c5 + high; step n goes to
// slot low % 5 if it's free.
auto CainQuest::stone_order(std::uint32_t& low, std::uint32_t& high) -> void {
    order = {};
    for (int step = 0; step < 5;) {
        const std::uint64_t seed = std::uint64_t(low) * 0x6ac690c5u + high;
        low = std::uint32_t(seed); high = std::uint32_t(seed >> 32);
        if (!order[low % 5]) order[low % 5] = 17 + step++;
    }
    ordered = true;
}

// A Cairn Stone touched (OperateFn 9, FUN_00593710): the fifth sends log 4 (flag 1).
auto CainQuest::stone(QuestBits& quest_bits, int stone, bool bkd, bool fresh) -> Stone {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return Stone::none;
    if (!bkd) { ++throttle; return Stone::none; }
    if (!active || state > 5 || stones_done) return Stone::none;
    state = 5;
    if (stone != order[std::size_t(touched)]) return Stone::none;   // out of order: ignored, kept
    ++touched;
    if (!fresh) return Stone::none;
    if (touched < 5) return Stone::lit;
    stones_done = true;
    if (log < 4) { log = 4; news = true; quest::set(quest_bits, kQuest, 4); }   // flag 1 (0x593a65)
    return Stone::portal;
}

// The stones as Stony Field comes up (InitFn 6, FUN_005935e0). True:
// the portal's re-made at StoneAlpha (FUN_00592d50: x + 4, y + 4).
auto CainQuest::stones_init() -> bool {
    if (active && !rejoined) return false;
    rejoined = false;
    if (portal_made) return false;
    return portal_made = true;
}

// The Gibbet (OperateFn 10, FUN_00593480), `fresh` its mode 0. True: it
// opens, the player took part (bits 13, 1).
auto CainQuest::gibbet(QuestBits& quest_bits, bool fresh) -> bool {
    if (!active || resolved || state >= 6) return false;
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || !fresh) return false;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
    return true;
}

// The Gibbet's opened (its mode end, FUN_00593290): Tristram Cain
// (monster 0x92) comes out at x + 3, y + 3 and walks to his own portal
// (World::cain_walk). `spawned` false (no room for him): a town portal's
// due by it (x + 6, y + 6) and he goes straight to the camp (+0x52).
auto CainQuest::rescued(bool spawned) -> void {
    if (!spawned && !camp_cain) camp_due = true;
    log = 6; news = true;                                // flag 1 (0x593446)
}

// Cain's portal in Tristram (object 189: InitFn 61 FUN_00594290 mode 1,
// its event 7 FUN_005942c0 every 25 frames from its opening): 1 -> 2,
// 2 -> 3 at the first event once Cain's in (+0x91), 3 -> 4 the next.
// The frame, from its opening, it reaches mode 4, Cain in at `entered`.
auto CainQuest::portal_closed_frame(int entered) -> int {
    constexpr int kStep = 25;
    return std::max(2 * kStep, (entered / kStep + 1) * kStep) + kStep;
}

// The portal's mode at `frame` from its opening, mode 4 at `closed`
// (portal_closed_frame; 0 not due): 1 (OP) till the first event 7, 2
// (ON), 3 (S1, the closing loop) the 25 frames before mode 4, then 4
// (S2, one frame held for good: nothing removes it).
auto CainQuest::portal_mode(int frame, int closed) -> int {
    constexpr int kStep = 25;
    if (closed > 0 && frame >= closed) return 4;
    if (closed > 0 && frame >= closed - kStep) return 3;
    return frame < kStep ? 1 : 2;
}

// The Cube's Cow Portal (output kind 1, FUN_00594140): none opened this
// game yet (record 0xb's flag, FUN_00544760), the Cow King alive (bit
// 10), the last act's boss down in this difficulty (expansion: quest
// 0x28 bit 0, else 0x1a bit 0), in the Rogue Encampment. Then object 60
// to the Moo Moo Farm by the player (FUN_00545340, FUN_0056d130) and the
// flag set (FUN_00544720); else the inputs stay. The Pandemonium and
// Finale portals (kinds 2 / 3: FUN_00594270 / FUN_00594280) are a bare
// `return 0` in 1.14d, so their keys and organs stay too.
auto CainQuest::cow_portal_opens(const QuestBits& quest_bits, bool opened, bool expansion, int level_id) -> bool {
    return !opened && !quest::bit(quest_bits, kQuest, 10) && quest::bit(quest_bits, expansion ? 0x28 : 0x1a, 0) && level_id == level_ids::kRogueEncampment;
}

// The player joins (LAB_00595860).
auto TowerQuest::join(const QuestBits& quest_bits) -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) return;
    if (quest::bit(quest_bits, kQuest, 4)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 6)) { state = 3; log = 4; }
    else if (quest::bit(quest_bits, kQuest, 5)) { state = 2; log = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// The Moldy Tome read (OperateFn 6, FUN_00594e70), then its message
// 127 heard.
auto TowerQuest::read_tome(QuestBits& quest_bits, int player) -> void {
    if (state < 2) { state = 2; if (log == 0) tome_early = true; }
    said(quest_bits, -1, kTome, player);
}

auto TowerQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0), b13 = quest::bit(quest_bits, kQuest, 13);
    if (bit0 && !b13) return {};
    if (state > 3 && !due.has(player) && !told.has(player)) return {};
    int block = -1;
    if (due.has(player)) block = 2;
    else if (bit0) { if (!told.has(player)) return {}; block = 3; }
    else if (state >= 2 && state <= 5) block = state - 2;
    std::vector<QuestMsg> out;
    if (block >= 0)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The balloon over `npc` (FUN_005952c0): the success talk's due, not
// with Warriv or Gheed.
auto TowerQuest::alert(const QuestBits&, int npc, int player) const -> bool {
    return due.has(player) && (npc == monster_ids::kKashya || npc == monster_ids::kCharsi || npc == monster_ids::kAkara || npc == monster_ids::kCampCain);
}

// The player went from level `from` to `dest` (FUN_00595010). True: the
// state moved, every player's bits to mark ("mark all").
auto TowerQuest::enter(const QuestBits& quest_bits, int from, int dest, int player) -> bool {
    if (dest == kTower) {
        if (state == 0) { state = 2; log = 3; }                  // FUN_00595010: each sets the log with flag 1
        else if (state <= 3 && log == 1) log = 4;
        else return false;
        news = true;
        return true;
    }
    if (dest == kCellar) {
        if (state > 3 || log == 2) return false;
        state = 3; log = 2;
        news = true;
        return true;
    }
    if (from == 1) {                               // out of town
        if (state == 2) { if (!quest::bit(quest_bits, kQuest, 0)) state = 3; }
        else if (state == 5) told.remove(player);        // FUN_00594740 (then rec+0xa = 0 once both lists are empty)
    }
    return false;
}

// The Countess died (FUN_00595710, once: rec+0xc0 is cleared). True:
// the player's kill for the quest (FUN_00594f10: in cellar 5 — bits 13
// and 0, the voice); elsewhere bit 14. The 7-tick timer starts.
// Called for each player in turn (FUN_00594f10's pass); the record's
// part once.
auto TowerQuest::killed(QuestBits& quest_bits, bool in_cellar, int player) -> bool {
    if (!dead) {
        dead = true;
        state = 5;
        first_talk = true;
        log_in = quest_timer_steps(7);
    }
    if (quest::bit(quest_bits, kQuest, 0)) return false;
    if (!in_cellar) { quest::set(quest_bits, kQuest, 14); return false; }
    quest::set(quest_bits, kQuest, 13);
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
    due.add(player);
    return true;
}

// A later player joins (+0xd8, LAB_005919d0): holding the malus, with
// +9 and not returned, a carrier more; the first, the stand used: log 2.
auto ToolsQuest::join_more(bool holding) -> void {
    if (holding && active && state != 5 && ++carriers == 1 && dropped) log = 2;
}

// The chain reached it (+0xf0, FUN_00591e40): open (Charsi's "!").
// True: it passes on to the next (+0x10 = 6).
auto ToolsQuest::open() -> bool {
    if (state == 0 && active) { state = 1; return false; }
    return state == 5 || !active;
}

auto ToolsQuest::talk(const QuestBits& quest_bits, int npc, bool holding, int clvl) const -> std::vector<QuestMsg> {
    const bool bit0 = quest::bit(quest_bits, kQuest, 0);
    if (bit0 && !quest::bit(quest_bits, kQuest, 13)) return {};
    int block = -1;
    if (holding) { if (clvl >= kClvl && !bit0) block = 3; }
    else if (!bit0 && state != 0 && state != 4) block = state - 1;
    std::vector<QuestMsg> out;
    if (block >= 0 && block <= 4)
        for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// The balloon over `npc` (FUN_00591c30): Charsi, while it's open or
// the malus comes back at clvl 8+.
auto ToolsQuest::alert(const QuestBits& quest_bits, int npc, bool holding, int clvl) const -> bool {
    if (npc != kCharsi || quest::bit(quest_bits, kQuest, 0)) return false;
    if (state == 1 && !quest::bit(quest_bits, kQuest, 1)) return true;
    return clvl >= kClvl && holding;
}

// The player heard `string` from `npc` (FUN_00591490). True: Charsi
// took the malus back (the caller removes it, FUN_00544160) — the
// imbue's due (bits 13 and 1) and "quest complete" (0xca7). The state
// going 4 → 5 runs the chain (+0xf0); else, the malus carried into the
// game, quest 6's (+0x10) runs directly.
auto ToolsQuest::said(QuestBits& quest_bits, int npc, int string, bool holding) -> bool {
    if (npc != kCharsi) return false;
    if (string == 146) { state = 2; given = true; return false; }
    if (string != 163 || quest::bit(quest_bits, kQuest, 0) || !holding) return false;
    quest::set(quest_bits, kQuest, 13);
    quest::set(quest_bits, kQuest, 1);
    if (active && state == 4) { state = 5; returned = true; game_returned = true; }
    return true;
}

// Another party member's share when the malus comes back (FUN_00540510,
// 0x591430): clvl 8+, neither done nor due: bits 13 and 1.
auto ToolsQuest::party_credit(QuestBits& quest_bits, int clvl) -> void {
    if (clvl < kClvl || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    quest::set(quest_bits, kQuest, 13);
    quest::set(quest_bits, kQuest, 1);
}

// The talk with `npc` closed (0x5913c0).
auto ToolsQuest::talk_closed(QuestBits& quest_bits, int npc) -> void {
    if (npc != kCharsi) return;
    if (given) { mark(quest_bits); log = 1; given = false; news = true; }   // flag 1 (0x5913ff)
    else if (returned) { log = 0xd; returned = false; news = true; }        // flag 1 (0x591421)
}

// The player left level `from` (LAB_00591810): out of town after
// Charsi gave it.
auto ToolsQuest::enter(QuestBits& quest_bits, int from) -> void {
    if (!active || from != 1 || state != 2 || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    if (log != 1) news = true;                            // flag 1 (0x591891), only for a change
    log = 1;
    state = 3;
    mark(quest_bits);
}

auto ToolsQuest::operate(QuestBits& quest_bits, int clvl) -> Stand {
    if (!active) return Stand::refuse;
    if (dropped || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return Stand::none;
    if (clvl < kClvl) return Stand::refuse;
    dropped = true;
    if (state != 4) { state = 4; mark(quest_bits); }
    log = 1;
    return Stand::drop;
}

// The malus went into the inventory (FUN_00591960). True: the first
// time — bit 6, and the player's line (FUN_00553380).
auto ToolsQuest::picked_up(QuestBits& quest_bits) -> bool {
    log = 2; news = true;                                // flag 1 (0x5919b4)
    if (quest::bit(quest_bits, kQuest, 6)) return false;
    quest::set(quest_bits, kQuest, 6);
    return true;
}

// Charsi imbued an item (FUN_00591790): done, the reward's used.
auto ToolsQuest::imbued(QuestBits& quest_bits) -> void {
    quest::set(quest_bits, kQuest, 0);
    quest::set(quest_bits, kQuest, 1, false);
}

// The quest log's state (FUN_00591d30).
auto ToolsQuest::log_state(const QuestBits& quest_bits, bool holding, int clvl) const -> int {
    if (quest::bit(quest_bits, kQuest, 1)) return 10;
    if (holding) return quest::bit(quest_bits, kQuest, 0) ? 0 : 2;
    if (!active) return 0;
    if (quest::bit(quest_bits, kQuest, 13)) return 0xd;
    if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
    if (state < 5) return log;
    return game_returned ? (clvl >= kClvl ? 12 : 4) : 0;
}

}
