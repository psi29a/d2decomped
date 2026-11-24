// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quests_act5.hpp's functions, with the research behind each.
#include "quests_act5.hpp"

#include "quest_clock.hpp"
#include "quests.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::rules {
namespace act5_detail {

bool told(const QuestBits& quest_bits, int quest) {
    return !(quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 15)) || quest::bit(quest_bits, quest, 13) || quest::bit(quest_bits, quest, 14);
}

std::vector<QuestMsg> block_for(const std::vector<Entry>& block, int npc) {
    std::vector<QuestMsg> out;
    for (const auto& entry : block) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

// Each record's per-player marks of its state (LAB_00586de0 and kin): 2 →
// bit 2, 3 → bit 3 (a5q1: bit 4 with the log at 2).
void mark(QuestBits& quest_bits, int quest, int state, bool four) {
    if (quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 1)) return;
    if (state == 2) quest::set(quest_bits, quest, 2);
    if (state == 3) quest::set(quest_bits, quest, four ? 4 : 3);
}

}


auto Act5Opening::talk(std::uint64_t intros, int npc, int player_class) -> std::vector<QuestMsg> {
    if (bit_of(npc) < 0 || heard(intros, npc)) return {};
    int first = 7, second = 7;
    if (npc == act5_detail::kMalah) { first = 4; second = 1; }
    if (npc == act5_detail::kNihlathak) { first = 6; second = 2; }
    if (npc == act5_detail::kQualKehk) { first = 5; second = 3; }
    return act5_detail::block_for(kBlocks[player_class == first ? 1 : player_class == second ? 2 : 0], npc);
}

// Heard: true when it was Malah's (a5q1 opens).
auto Act5Opening::said(std::uint64_t& intros, int npc, int string) -> bool {
    const auto is_intro = [&](const auto& block) { return std::ranges::any_of(block, [&](const auto& entry) { return entry.npc == npc && entry.string == string; }); };
    if (bit_of(npc) < 0 || !std::ranges::any_of(kBlocks, is_intro)) return false;
    intros |= std::uint64_t{ 1 } << bit_of(npc);
    return npc == kMalah;
}

// The player joins (FUN_005875f0).
auto SiegeQuest::join(const QuestBits& quest_bits, bool malah_heard) -> void {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open || quest::bit(quest_bits, kQuest, 1)) { log = 5; return; }
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { log = 1; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
    else if (malah_heard && state == 0) state = 1;
}

// Larzuk's talk closes (FUN_00586d80): log 1 after 20077, 4 after 20090.
auto SiegeQuest::talk_closed(int npc) -> void {
    if (npc != kLarzuk) return;
    if (std::exchange(told_heard, false)) { log = 1; news = true; }
    if (std::exchange(reward_heard, false)) { log = 4; news = true; }
}

// A level change (FUN_005873e0): into the barricades (110..112) at 1 / 2
// → 3; out of Harrogath at 2 → 3 (the log 1 if 0).
auto SiegeQuest::enter(QuestBits& quest_bits, int from, int into) -> void {
    if (into >= act5_detail::kBloodyFoothills && into <= act5_detail::kArreatPlateau) {
        if (open && (state == 1 || state == 2)) { state = 3; act5_detail::mark(quest_bits, kQuest, state, log == 2); }
    } else if (from == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
        state = 3;
        if (log == 0) { log = 1; news = true; }
        act5_detail::mark(quest_bits, kQuest, state, log == 2);
    }
}

// Shenk died (FUN_00587330): about him (his room or one near it) bits 1
// and 13, else 14; the log 3 if under.
auto SiegeQuest::killed(QuestBits& quest_bits, bool about) -> void {
    if (!open) return;
    if (about && !quest::bit(quest_bits, kQuest, 0)) { quest::set(quest_bits, kQuest, 1); quest::set(quest_bits, kQuest, 13); }
    else if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
    if (log < 3) { log = 3; news = true; }
}

// Heard (FUN_00586e70): whether the chain (a5q2) opens.
auto SiegeQuest::said(QuestBits& quest_bits, int npc, int string) -> bool {
    if (npc != kLarzuk) return false;
    if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state, log == 2); return false; }
    if (string != kReward || !quest::bit(quest_bits, kQuest, 1)) return false;
    reward_heard = true;
    act5_detail::clear_steps(quest_bits, kQuest);
    quest::set(quest_bits, kQuest, 5);
    if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 13; news = true; return true; }
    return false;
}

// The "!" (FUN_005874e0).
auto SiegeQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc != kLarzuk) return false;
    if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 5)) return true;
    return open && state == 1 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
}

// The log (FUN_00586ce0).
auto SiegeQuest::log_state(const QuestBits& quest_bits) const -> int {
    if (quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 13)) return quest::bit(quest_bits, kQuest, 5) ? 4 : 3;
    if (!open || quest::bit(quest_bits, kQuest, 0)) return 0;
    if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
    return state > 4 ? 0 : log;
}

// A cage's portal (FUN_005888d0): the dead door's subtile + 2 on x, as
// is (an object's make, FUN_00555230 at 0x588a13, tests no collision);
// cells, the subtile's centre.
auto RescueQuest::portal_spot(int door_x, int door_y) -> std::pair<float, float> {
    return { (float(door_x + 2) + 0.5f) / 5, (float(door_y) + 0.5f) / 5 };
}

// The player joins (LAB_00588470): bit 4 cleared first.
auto RescueQuest::join(QuestBits& quest_bits) -> void {
    quest::set(quest_bits, kQuest, 4, false);
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open || quest::bit(quest_bits, kQuest, 1)) return;
    if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// A cage comes up (InitFn 62, FUN_005886a0) at (x, y) subtiles: state 2
// if under, the log 1 at 0 (under five dead); a slot of its own (none
// when three are up or one's at that spot). The World then makes its
// prisoners (FUN_00588600: up to 25 tries for five within 5 of it) and
// calls prisoner_made.
// FUN_00588c50, the count FUN_00544190 puts in its S->C 0x5d for word 0x24
// (flags 0x20, +4): five for each cage not up (+0x86..+0x88), and the made
// (+0xa4) neither freed (+0xac) nor dead (+0xa8); never under 0.
auto RescueQuest::left() const -> int {
    int count = 0;
    for (const auto& held : cages) count += held.up ? 0 : kPerCage;
    return std::max(count + made - freed - dead, 0);
}

auto RescueQuest::cage(int x, int y) -> int {
    if (!open) return -1;
    if (state < 2) state = 2;
    if (log == 0 && dead < kFail) { log = 1; news = true; }
    int slot = 0;
    while (slot < kCages && cages[std::size_t(slot)].up) {
        if (cages[std::size_t(slot)].x == x && cages[std::size_t(slot)].y == y) return -1;
        ++slot;
    }
    if (slot == kCages) return -1;
    auto& made_cage = cages[std::size_t(slot)];
    made_cage.up = true; made_cage.x = x; made_cage.y = y;
    return slot;
}

// A level change (FUN_00588200): into the Highlands / Plateau, or out of
// Harrogath at 2.
auto RescueQuest::enter(QuestBits& quest_bits, int from, int into) -> void {
    if ((into == act5_detail::kFrigidHighlands || into == act5_detail::kArreatPlateau) && open && dead < kFail) {
        const bool moved = state == 1 || state == 2;
        if (moved) state = 3;
        if (log == 0) { log = 1; news = true; closed_hook = false; }
        else if (!moved) return;
        act5_detail::mark(quest_bits, kQuest, state);
        return;
    }
    if (from == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
        state = 3;
        act5_detail::mark(quest_bits, kQuest, state);
        if (log == 0 && open && dead < kFail) { log = 1; news = true; closed_hook = false; }
    }
}

// A door broke (FUN_00588040's 0x1b2 branch): these living prisoners
// within 15 of it freed.
auto RescueQuest::door_broken(QuestBits& quest_bits, const std::vector<int>& near_ids) -> void {
    if (!open) return;
    for (const int id : near_ids) { freed_ids.push_back(id); ++freed; }
    failed(quest_bits);
}

// A prisoner died (FUN_00588040): one freed counts gone from its cage;
// one caged is dead (five fail it), and gone too.
auto RescueQuest::prisoner_died(QuestBits& quest_bits, int id) -> void {
    if (!open) return;
    if (std::ranges::contains(freed_ids, id)) { went(cage_of(id)); return; }
    ++dead;
    failed(quest_bits);
    went(cage_of(id));
}

auto RescueQuest::portal_opened(QuestBits& quest_bits, int slot) -> void {
    cages[std::size_t(slot)].portal = true;
    if (made != dead + freed || !std::ranges::all_of(cages, &Cage::up)) {
        quest::set(quest_bits, kQuest, 4);
        if (dead < kFail) { log = 2; news = true; }
        told_heard = false;
        return;
    }
    if (freed < 12) { someone_else(quest_bits); return; }
    log = 3; news = true;
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
    quest::set(quest_bits, kQuest, freed == 15 ? 5 : freed == 14 ? 6 : 7);
}

// Qual-Kehk's talk closes (LAB_00587a30, while the record keeps it): log 1 after 20096.
auto RescueQuest::talk_closed(int npc) -> void {
    if (npc == kQualKehk && closed_hook && dead < kFail && std::exchange(told_heard, false)) { log = 1; news = true; closed_hook = false; }
}

auto RescueQuest::said(QuestBits& quest_bits, int npc, int string) -> Heard {
    Heard out;
    if (npc != kQualKehk) return out;
    if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state); return out; }
    if (string != kReward || !quest::bit(quest_bits, kQuest, 1)) return out;
    if (quest::bit(quest_bits, kQuest, 13)) {
        if (state != 5) { state = 5; log = 13; out.chain = true; }
        closed_hook = false;
    }
    out.runes = quest::bit(quest_bits, kQuest, 6) ? 2 : quest::bit(quest_bits, kQuest, 7) ? 1 : 3;
    quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false);
    act5_detail::clear_steps(quest_bits, kQuest);
    return out;
}

auto RescueQuest::talk(const QuestBits& quest_bits, int npc) const -> std::vector<QuestMsg> {
    if (quest::bit(quest_bits, kQuest, 1)) return act5_detail::block_for(kBlocks[3], npc);
    if (quest::bit(quest_bits, kQuest, 0) || (state > 3 && !quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
    if (state == 3 && npc == kQualKehk && quest::bit(quest_bits, kQuest, 4)) return act5_detail::block_for(kBlocks[5], npc);
    return act5_detail::block_for(kBlocks[state - 1], npc);
}

// The "!" (FUN_00588340): Qual-Kehk, and every prisoner till it's done.
auto RescueQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc == kPrisoner) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    if (npc != kQualKehk) return false;
    if (state == 1) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    return quest::bit(quest_bits, kQuest, 1);
}

// A party member's share of it, 12 or more freed (FUN_00587e60): in act 5,
// neither done nor due: bits 13, 1 and 5 / 6 / 7 by the count.
auto RescueQuest::party_share(QuestBits& quest_bits) const -> void {
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
    quest::set(quest_bits, kQuest, freed == 15 ? 5 : freed == 14 ? 6 : 7);
}

// The player joins (FUN_0058a110).
// A later player joins (+0xd8, LAB_0058a1e0): the potion they carry
// counts (data +0xd8, not kept); with this one shut or past state 4 and
// a5q4 open (record 0x22 +9, 0x58a212), a joiner with none of bits 1, 6
// and 0 gets a5q4's bit 14 (0x58a262).
auto PrisonQuest::join_more(QuestBits& quest_bits, bool betrayal_open) const -> void {
    if ((!open || state >= 5) && betrayal_open && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 6) && !quest::bit(quest_bits, kQuest, 0))
        quest::set(quest_bits, kQuest + 1, 14);
}

auto PrisonQuest::join(const QuestBits& quest_bits, bool carries_potion) -> void {
    if (carries_potion) potions = 1;
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) { anya = 2; town_left = true; return; }
    if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    if (potions) { state = 3; log = 4; }
}

// A level change (FUN_00589d80).
auto PrisonQuest::enter(QuestBits& quest_bits, int from, int into) -> void {
    if (into == act5_detail::kArreatPlateau) {
        if (open && state == 0) { state = 1; leave_town(); }
    } else if ((into == act5_detail::kCrystallinePassage || into == act5_detail::kFrozenRiver) && open) {
        const bool moved = state <= 2;
        if (moved) state = 3;
        if (log == 0) { log = 1; news = true; act5_detail::mark(quest_bits, kQuest, state); }
        else if (moved) act5_detail::mark(quest_bits, kQuest, state);
        leave_town();
    }
    if (from == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
        state = 3;
        if (log == 0) { log = 1; news = true; }
        act5_detail::mark(quest_bits, kQuest, state);
    }
    if (into >= act5_detail::kNihlathaksTemple && into <= act5_detail::kHallsOfVaught && open && state < 5) {
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
        state = 6; anya = 2; town_left = true;
    }
}

// Fana used (OperateFn 67, FUN_0058abc0). Without the potion she says
// 20131 (FUN_005456a0: heard, below; the log 3 from 1); with it, it's
// taken: state 5, bits 13 and 1, log 5, her thaw (once). Returns whether
// the potion was used.
auto PrisonQuest::thaw(QuestBits& quest_bits, bool carries_potion) -> bool {
    if (!carries_potion) { heard_no_potion(); if (log == 1) { log = 3; news = true; } return false; }
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
    --potions;
    state = 5; anya = 1;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
    log = 5; news = true;
    if (!thawing) { thawing = true; thaw_step = 0; thaw_steps = 0; }
    return true;
}

// A quest-clock step. The thaw's timer (1 tick, queued by FUN_0058abc0
// at 0x58ad52) returns 0 at its first run, so it runs every 2 steps:
// true when it runs.
auto PrisonQuest::thaw_quest_step() -> bool {
    if (!thawing || ++thaw_steps < quest_timer_steps(1)) return false;
    thaw_steps = 0;
    return true;
}

// The thaw's timer (FUN_0058aab0), a run each: 1 fana's mode 2; 2 she's
// drehyaiced, the chain on. Returns the step done (0 none).
auto PrisonQuest::thaw_tick() -> int {
    if (!thawing) return 0;
    if (++thaw_step >= 2) thawing = false;
    return thaw_step;
}

// Malah's talk closes (FUN_00588f80).
auto PrisonQuest::talk_closed(int npc) -> void {
    if (npc != kMalah) return;
    if (std::exchange(told_heard, false)) { log = 1; news = true; }
    if (open && potion_heard && log < 4) { log = 4; news = true; potion_heard = false; }
}

auto PrisonQuest::talk(const QuestBits& quest_bits, int npc, bool carries_potion) const -> std::vector<QuestMsg> {
    if (npc == kFrozen) return {};
    if (npc == kMalah && state == 4) return !carries_potion && potions == 0 ? act5_detail::block_for(kBlocks[3], npc) : std::vector<QuestMsg>{};
    if (quest::bit(quest_bits, kQuest, 1)) {
        if ((npc == kAnya && quest::bit(quest_bits, kQuest, 9)) || (npc == kMalah && quest::bit(quest_bits, kQuest, 8))) return {};
        return act5_detail::block_for(kBlocks[5], npc);
    }
    if (quest::bit(quest_bits, kQuest, 0) || (state > 4 && !quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 6) return {};
    return act5_detail::block_for(kBlocks[state - 1], npc);
}

// The "!" (FUN_00589f10).
auto PrisonQuest::alert(const QuestBits& quest_bits, int npc, bool carries_potion) const -> bool {
    if (npc == kMalah) {
        if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 8)) return true;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
        return state == 1 || (state == 4 && potions == 0);
    }
    if (npc == kFrozen) return open && state <= 4 && !carries_potion;
    return npc == kAnya && quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 9);
}

// The log (FUN_0058a300).
auto PrisonQuest::log_state(const QuestBits& quest_bits, bool carries_potion) const -> int {
    if (quest::bit(quest_bits, kQuest, 0)) return 0;
    if (quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 13)) {
        if (quest::bit(quest_bits, kQuest, 8)) return quest::bit(quest_bits, kQuest, 9) ? 0xd : 6;
        return 5;
    }
    if (!open) return 0;
    if (carries_potion) return 4;
    if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
    return state < 5 ? log : 0;
}

// The player joins (FUN_0058b990).
auto BetrayalQuest::join(const QuestBits& quest_bits) -> void {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) { state = 5; return; }
    if (quest::bit(quest_bits, kQuest, 1)) return;
    if (quest::bit(quest_bits, kQuest, 3)) { portal_due = true; log = 2; state = 3; news = true; }
    else if (quest::bit(quest_bits, kQuest, 2)) { portal_due = true; log = 1; state = 2; news = true; }
}

// A level change (LAB_0058bab0): out of Harrogath at 2 → 3; into the
// temple (121) with the log 1 → 2.
auto BetrayalQuest::enter(QuestBits& quest_bits, int from, int into) -> void {
    if (from == act5_detail::kHarrogath && state == 2) { state = 3; act5_detail::mark(quest_bits, kQuest, state); }
    if (into == act5_detail::kNihlathaksTemple && log == 1) { log = 2; news = true; told_heard = false; }
}

auto BetrayalQuest::killed(QuestBits& quest_bits, bool about) -> bool {
    if (!open) return false;
    const bool prison = quest::bit(quest_bits, 0x25, 0) || quest::bit(quest_bits, 0x25, 1);
    if (about && prison && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 14)) { quest::set(quest_bits, kQuest, 1); quest::set(quest_bits, kQuest, 13); }
    if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
    state = 4;
    return !std::exchange(timer_set, true);
}

// Anya's talk closes (FUN_0058b050): the log 1 after 20137; true when her
// portal to the temple is to open (FUN_0058af90: object 60 at her spot +10,
// +5 subtiles, to level 121).
auto BetrayalQuest::talk_closed(int npc) -> bool {
    if (npc != kAnya) return false;
    if (std::exchange(told_heard, false)) { log = 1; news = true; }
    if (!portal_due || portal_made) return false;
    portal_made = true; portal_due = false;
    return true;
}

// Heard (FUN_0058b1c0): whether the chain (a5q5) opens.
auto BetrayalQuest::said(QuestBits& quest_bits, int npc, int string) -> bool {
    if (npc != kAnya) return false;
    if (string == kTold && open) { state = 2; told_heard = portal_due = true; act5_detail::mark(quest_bits, kQuest, state); return false; }
    if (string != kReward) return false;
    news = true;
    quest::set(quest_bits, kQuest, 4);
    if (quest::bit(quest_bits, kQuest, 13)) { state = 5; return true; }
    return false;
}

auto BetrayalQuest::talk(const QuestBits& quest_bits, int npc) const -> std::vector<QuestMsg> {
    if (!quest::bit(quest_bits, 0x25, 0) && !quest::bit(quest_bits, 0x25, 1)) return {};
    if (npc == kAnya && state == 1) return !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0) ? act5_detail::block_for(kBlocks[0], npc) : std::vector<QuestMsg>{};
    if (quest::bit(quest_bits, kQuest, 1)) return act5_detail::block_for(kBlocks[quest::bit(quest_bits, kQuest, 4) ? 4 : 3], npc);
    if (quest::bit(quest_bits, kQuest, 0) || !(state < 4 || quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
    return act5_detail::block_for(kBlocks[state - 1], npc);
}

// The "!" (FUN_0058b870).
auto BetrayalQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (npc != kAnya) return false;
    if (state == 1) return (quest::bit(quest_bits, 0x25, 0) || quest::bit(quest_bits, 0x25, 1)) && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    return quest::bit(quest_bits, kQuest, 13) && !quest::bit(quest_bits, kQuest, 4);
}

// The log (FUN_0058af00).
auto BetrayalQuest::log_state(const QuestBits& quest_bits) const -> int {
    if (quest::bit(quest_bits, kQuest, 0)) return 0;
    if (quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 13)) return quest::bit(quest_bits, kQuest, 4) ? 5 : 4;
    if (!open) return 0;
    if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
    return state < 4 ? log : 0;
}

// The player joins (LAB_0058cda0).
auto RiteQuest::join(const QuestBits& quest_bits) -> void {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) { stairs_open = true; return; }
    if (quest::bit(quest_bits, kQuest, 3)) { log = 1; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
}

// A level change (FUN_0058cba0).
auto RiteQuest::enter(QuestBits& quest_bits, int from, int into, int clvl, int difficulty) -> void {
    if (from == act5_detail::kHarrogath && open && state == 2 && !quest::bit(quest_bits, kQuest, 0)) {
        state = 3;
        if (log != 1) { log = 1; news = true; }
        mark(quest_bits, clvl, difficulty);
    }
    if (into != kSummit) return;
    bool moved = false;
    if (log < 2 && state > 2) { log = 2; news = true; moved = true; }
    if (state < 3) { state = 3; moved = true; }
    if (moved) mark(quest_bits, clvl, difficulty);
}

// The altar used (OperateFn 65, FUN_0058d310) at mode 0: the Ancients
// speak (FUN_005456a0(20002): heard); state 2 from under, log 3. True
// when they spoke.
auto RiteQuest::altar(QuestBits& quest_bits) -> bool {
    if (altar_mode != 0) return false;
    if (open) {
        if (state >= 4) return false;
        if (state < 2) state = 2;
        if (log != 3) { log = 3; news = true; }
    }
    altar_mode = 2;
    said(quest_bits, -1, kSpeech, 0, 0);
    return true;
}

// A statue's event 7 (FUN_0058c0e0): its Ancient's superunique to make
// (-1 none).
auto RiteQuest::statue_wakes(int which) -> int {
    if (made >= 3 || statue_mode[std::size_t(which)] == 4) return -1;
    statue_mode[std::size_t(which)] = 4;
    ++made; ++alive; fighting = true;
    return kSuperuniques[std::size_t(which)];
}

// An Ancient fell while they're up (FUN_0058c9a0, before the count):
// its statue's due back, the return timer on (0x58c9da: FUN_00543f10,
// 2 ticks). True: its death missile goes to that statue.
// None counts while a town portal is open there (data +0xc).
auto RiteQuest::ancient_fell(int statue) -> bool {
    if (!heard || !awake || portals != 0) return false;
    returning = true;
    statue_dead[std::size_t(statue)] = true;
    return true;
}

// The return timer's tick (FUN_0058bd50): the statues whose Ancient fell
// go to mode 1 (their event 1 to mode 2 after FrameCnt1), +0x30 2; the
// timer stops. Returns them.
auto RiteQuest::statues_back() -> std::array<bool, 3> {
    returning = false;
    if (!awake) return {};
    const auto back = std::exchange(statue_dead, {});
    for (std::size_t which = 0; which < back.size(); ++which) if (back[which]) statue_mode[which] = 2;
    return back;
}

// An Ancient died (FUN_0058c9a0): true when it was the last, the Rite
// won (state 5, the log 13, the chain); then each player's reward().
auto RiteQuest::ancient_died() -> bool {
    if (!heard || !awake || portals != 0 || alive <= 0) return false;
    if (--alive != 0) return false;
    done = true;
    if (!open) return false;
    state = 5;
    if (log != 0xd) { log = 0xd; news = true; }
    stairs_open = true;
    return true;
}

// A player's share (FUN_0058c7e0 / FUN_0058c780): on the Summit, of the
// level, not done: bits 13 and 0 and the experience; else bit 14.
// Returns whether the experience is theirs.
auto RiteQuest::reward(QuestBits& quest_bits, bool on_summit, int clvl, int difficulty) const -> bool {
    if (on_summit && level_enough(clvl, difficulty) && !quest::bit(quest_bits, kQuest, 0)) {
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0);
        return true;
    }
    if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
    return false;
}

// A town portal opened on the Summit (the Town Portal skill's
// FUN_005be290 -> FUN_0058cf00, each end): not won, one more open and
// reset. True when it reset.
auto RiteQuest::portal_opened() -> bool {
    if (done) return false;
    ++portals;
    return reset();
}

// One closed there (FUN_0058cf50: the pair unlinked by FUN_00535430, or
// its owner back through it, FUN_00584870): the last, heard again, wakes them.
auto RiteQuest::portal_closed() -> void {
    --portals;
    if (portals == 0 && heard) awake = true;
}

// The stairs (OperateFn 66, FUN_0058d400): whether they let the player on.
auto RiteQuest::stairs_pass(const QuestBits& quest_bits) const -> bool {
    if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) return false;
    return !((open && !done) || (fighting && !done));
}

auto RiteQuest::talk(const QuestBits& quest_bits, int npc) const -> std::vector<QuestMsg> {
    if (!open) return {};
    if (!quest::bit(quest_bits, kQuest, 0)) return state >= 1 && state <= 5 ? act5_detail::block_for(kBlocks[state - 1], npc) : std::vector<QuestMsg>{};
    for (const auto& congrats : kCongrats)
        if (npc == congrats.npc) {
            if (!quest::bit(quest_bits, kQuest, congrats.bit)) return act5_detail::block_for(kBlocks[5], npc);
            if (quest::bit(quest_bits, kQuest, 13)) return act5_detail::block_for(kBlocks[3], npc);
        }
    return {};
}

// The player joins (FUN_0058e430): done, bit 10 and the next difficulty.
auto EveQuest::join(QuestBits& quest_bits) -> bool {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) { quest::set(quest_bits, kQuest, 10); return true; }
    if (quest::bit(quest_bits, kQuest, 1)) return false;
    if (quest::bit(quest_bits, kQuest, 3) || quest::bit(quest_bits, kQuest, 2)) {
        if (!quest::bit(quest_bits, kQuest, 3)) told = true;
        log = 1; state = 3;
    }
    return false;
}

// a5q5's chain (LAB_0058e390).
auto EveQuest::opened(QuestBits& quest_bits, bool in_act5) -> void {
    if (!open || state != 0) return;
    told = true; state = 2; log = 1; news = true;
    mark(quest_bits, in_act5);
}

// A level change (FUN_0058e190).
auto EveQuest::enter(QuestBits& quest_bits, int from, int into) -> void {
    if (from == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
        state = 3;
        if (log != 1) { log = 1; news = true; }
        mark(quest_bits, true);
    }
    if (into > 0x82) {
        if (open && log < 2) { log = 2; news = true; }
        mark(quest_bits, true);
    }
}

auto EveQuest::reward(QuestBits& quest_bits, bool in_chamber) -> bool {
    if (quest::bit(quest_bits, kQuest, 0)) return false;
    if (in_chamber && !quest::bit(quest_bits, kQuest, 1)) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0); return true; }
    quest::set(quest_bits, kQuest, 14);
    return false;
}

// Heard (LAB_0058d940): the town's congratulations.
auto EveQuest::said(QuestBits& quest_bits, int npc, int string) -> void {
    for (const auto& congrats : kCongrats) if (npc == congrats.npc && string == congrats.string) quest::set(quest_bits, kQuest, congrats.bit);
}

auto EveQuest::talk(const QuestBits& quest_bits, int npc) const -> std::vector<QuestMsg> {
    if (!quest::bit(quest_bits, kQuest, 0)) return state == 3 ? act5_detail::block_for(kBlocks[0], npc) : std::vector<QuestMsg>{};
    const bool here = quest::bit(quest_bits, kQuest, 13);
    const auto by_bit = [&](int bit) -> std::vector<QuestMsg> { return !quest::bit(quest_bits, kQuest, bit) ? act5_detail::block_for(kBlocks[2], npc) : here ? act5_detail::block_for(kBlocks[3], npc) : std::vector<QuestMsg>{}; };
    switch (npc) {
    case 511: return by_bit(4);
    case 512: return quest::bit(quest_bits, kQuest, 9) ? (here ? act5_detail::block_for(kBlocks[3], npc) : std::vector<QuestMsg>{}) : act5_detail::block_for(kBlocks[2], npc);
    case 513: return by_bit(6);
    case 515: return by_bit(8);
    case 520:
        if (quest::bit(quest_bits, kQuest, 10)) return {};
        if (!quest::bit(quest_bits, kQuest, 5)) return act5_detail::block_for(kBlocks[4], npc);
        return here ? act5_detail::block_for(kBlocks[5], npc) : std::vector<QuestMsg>{};
    case kTyrael: return here ? act5_detail::block_for(kBlocks[2], npc) : std::vector<QuestMsg>{};
    default: return {};
    }
}

// The "!" (FUN_0058e2b0): once done, each who's yet to say theirs.
auto EveQuest::alert(const QuestBits& quest_bits, int npc) -> bool {
    if (!quest::bit(quest_bits, kQuest, 0)) return false;
    switch (npc) {
    case 511: return !quest::bit(quest_bits, kQuest, 4);
    case 512: return !quest::bit(quest_bits, kQuest, 9);
    case 513: return !quest::bit(quest_bits, kQuest, 6);
    case 515: return !quest::bit(quest_bits, kQuest, 8);
    case 520: return !quest::bit(quest_bits, kQuest, 10) && !quest::bit(quest_bits, kQuest, 5);
    case kTyrael: return !quest::bit(quest_bits, kQuest, 7);
    default: return false;
    }
}

}
