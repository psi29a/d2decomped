// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's quests (docs/research/re/quests-act5.md): Siege on Harrogath
// (a5q1, Shenk), Rescue on Mount Arreat (a5q2, the prisoners), Prison of
// Ice (a5q3, Anya), Betrayal of Harrogath (a5q4, Nihlathak). One player's
// view of each record; World (world_quests_act5.cpp) runs them at its quest
// hooks. The log (+0xb) moves where the records call FUN_00544300; `news`
// is its flag 1 (the log sent again).
#pragma once

#include "quests.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::rules {

namespace act5_detail {
// The town's NPCs (MonStats hcIdx): Larzuk, Drehya (Anya), Malah,
// Nihlathak, Qual-Kehk, Cain; frozen Anya (drehyaiced); the prisoners
// (act5pow) and the cages' doors (prisondoor).
inline constexpr int kLarzuk = 0x1ff, kAnya = 0x200, kMalah = 0x201, kNihlathak = 0x202, kQualKehk = 0x203, kCain = 520;
inline constexpr int kFrozenAnya = 0x20f, kPrisoner = 0x216, kPrisonDoor = 0x1b2;
inline constexpr int kHarrogath = 109, kBloodyFoothills = 110, kFrigidHighlands = 111, kArreatPlateau = 112;
inline constexpr int kCrystallinePassage = 113, kFrozenRiver = 114, kNihlathaksTemple = 121, kHallsOfVaught = 124;
// The records' log callbacks (FUN_00586c80 and kin): the log goes to a
// player unless done (bit 0 or 15) without 13 / 14.
inline bool told(const QuestBits& quest_bits, int quest) {
    return !(quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 15)) || quest::bit(quest_bits, quest, 13) || quest::bit(quest_bits, quest, 14);
}
// FUN_0065c3e0: bits 2..11 cleared.
inline void clear_steps(QuestBits& quest_bits, int quest) { for (int bit = 2; bit <= 11; ++bit) quest::set(quest_bits, quest, bit, false); }
struct Entry { int npc, string; bool greet; };
inline std::vector<QuestMsg> block_for(const std::vector<Entry>& block, int npc) {
    std::vector<QuestMsg> out;
    for (const auto& entry : block) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}
// Each record's per-player marks of its state (LAB_00586de0 and kin): 2 →
// bit 2, 3 → bit 3 (a5q1: bit 4 with the log at 2).
inline void mark(QuestBits& quest_bits, int quest, int state, bool four = false) {
    if (quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 1)) return;
    if (state == 2) quest::set(quest_bits, quest, 2);
    if (state == 3) quest::set(quest_bits, quest, four ? 4 : 3);
}
}  // namespace act5_detail

// a5q0 (FUN_0058ea50, word 0x2a): the town's introductions, kept in the
// player's intro bits (the save's NPC section; FUN_005723c0 tests,
// FUN_00572360 sets; bit by the 0x732738 table). Until heard (FUN_00586b50):
// Anya and Cain block 0; Malah, Nihlathak and Qual-Kehk block 1 to a
// barbarian / assassin / druid, 2 to a sorceress / necromancer / paladin,
// else 0. Hearing one (FUN_0058e990) sets its bit; Malah's opens a5q1.
// Malah's "!" till hers is heard (FUN_00586c50).
struct Act5Opening {
    static constexpr int kMalah = act5_detail::kMalah;
    static inline const std::vector<act5_detail::Entry> kBlocks[3] = {   // 0x732ff8
        { { 512, 20014, true }, { 513, 20037, true }, { 514, 20053, true }, { 515, 20065, true }, { 520, 20003, true } },
        { { 512, 20014, true }, { 513, 20039, true }, { 514, 20054, true }, { 515, 20067, true }, { 520, 20003, true } },
        { { 512, 20014, true }, { 513, 20038, true }, { 514, 20055, true }, { 515, 20066, true }, { 520, 20003, true } },
    };
    // 0x732738's bits: Anya 30, Malah 31, Nihlathak 32, Qual-Kehk 33, Cain 34.
    [[nodiscard]] static int bit_of(int npc) {
        switch (npc) {
        case act5_detail::kAnya: return 30;
        case act5_detail::kMalah: return 31;
        case act5_detail::kNihlathak: return 32;
        case act5_detail::kQualKehk: return 33;
        case act5_detail::kCain: return 34;
        default: return -1;
        }
    }
    [[nodiscard]] static bool heard(std::uint64_t intros, int npc) { return bit_of(npc) >= 0 && (intros >> bit_of(npc) & 1); }
    [[nodiscard]] static std::vector<QuestMsg> talk(std::uint64_t intros, int npc, int player_class) {
        if (bit_of(npc) < 0 || heard(intros, npc)) return {};
        int first = 7, second = 7;
        if (npc == act5_detail::kMalah) { first = 4; second = 1; }
        if (npc == act5_detail::kNihlathak) { first = 6; second = 2; }
        if (npc == act5_detail::kQualKehk) { first = 5; second = 3; }
        return act5_detail::block_for(kBlocks[player_class == first ? 1 : player_class == second ? 2 : 0], npc);
    }
    // Heard: true when it was Malah's (a5q1 opens).
    static bool said(std::uint64_t& intros, int npc, int string) {
        const auto is_intro = [&](const auto& block) { return std::ranges::any_of(block, [&](const auto& entry) { return entry.npc == npc && entry.string == string; }); };
        if (bit_of(npc) < 0 || !std::ranges::any_of(kBlocks, is_intro)) return false;
        intros |= std::uint64_t{ 1 } << bit_of(npc);
        return npc == kMalah;
    }
    [[nodiscard]] static bool alert(std::uint64_t intros, int npc) { return npc == kMalah && !heard(intros, npc); }
};

// Siege on Harrogath (a5q1.cpp, word 0x23, record 0x1f, FUN_005876e0). State:
// 0 shut, 1 open (Malah's introduction heard, FUN_005723c0(0x201)), 2 Larzuk
// told (20077), 3 the barricades or Harrogath after, 5 rewarded (20090).
// Shenk (superunique 0x2a) dies: those about him get bits 13 and 1, the rest
// 14. Bit 5: Larzuk's reward heard (his socketing then due).
struct SiegeQuest {
    static constexpr int kQuest = 0x23, kLarzuk = act5_detail::kLarzuk, kShenk = 0x2a;
    static constexpr int kTold = 20077, kReward = 20090;
    bool open = true;              // +9
    int state = 0;                 // +0xc
    int log = 0;                   // +0xb
    bool told_heard = false;       // data +8
    bool reward_heard = false;     // data +0x16
    bool news = false;

    // The player joins (FUN_005875f0).
    void join(const QuestBits& quest_bits, bool malah_heard) {
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open || quest::bit(quest_bits, kQuest, 1)) { log = 5; return; }
        if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { log = 1; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
        else if (malah_heard && state == 0) state = 1;
    }
    // Malah's introduction heard (a5q0's FUN_0058e990).
    void malah_heard() { if (open && state == 0) state = 1; }
    // Shenk's think, the record open with the log under 2 (FUN_00587900): log 2.
    void shenk_seen() { if (open && log < 2) { log = 2; news = true; } }
    // Larzuk's talk closes (FUN_00586d80): log 1 after 20077, 4 after 20090.
    void talk_closed(int npc) {
        if (npc != kLarzuk) return;
        if (std::exchange(told_heard, false)) { log = 1; news = true; }
        if (std::exchange(reward_heard, false)) { log = 4; news = true; }
    }
    // A level change (FUN_005873e0): into the barricades (110..112) at 1 / 2
    // → 3; out of Harrogath at 2 → 3 (the log 1 if 0).
    void enter(QuestBits& quest_bits, int from, int into) {
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
    void killed(QuestBits& quest_bits, bool about) {
        if (!open) return;
        if (about && !quest::bit(quest_bits, kQuest, 0)) { quest::set(quest_bits, kQuest, 1); quest::set(quest_bits, kQuest, 13); }
        else if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
        if (log < 3) { log = 3; news = true; }
    }
    // Heard (FUN_00586e70): whether the chain (a5q2) opens.
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kLarzuk) return false;
        if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state, log == 2); return false; }
        if (string != kReward || !quest::bit(quest_bits, kQuest, 1)) return false;
        reward_heard = true;
        act5_detail::clear_steps(quest_bits, kQuest);
        quest::set(quest_bits, kQuest, 5);
        if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 13; news = true; return true; }
        return false;
    }
    // Larzuk's socketing used (FUN_005877c0): done.
    void socketed(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false); }
    // Blocks (0x733308 by 0x733928; FUN_00586ff0).
    static inline const std::vector<act5_detail::Entry> kBlocks[5] = {
        { { 511, 20077, true } },
        { { 511, 20078, false }, { 520, 20079, false }, { 512, 20080, false }, { 513, 20081, false }, { 514, 20082, false }, { 515, 20083, false } },
        { { 511, 20084, false }, { 520, 20085, false }, { 512, 20086, false }, { 513, 20087, false }, { 514, 20088, false }, { 515, 20089, false } },
        { { 511, 20090, true }, { 520, 20091, false }, { 512, 20092, false }, { 513, 20093, false }, { 514, 20094, false }, { 515, 20095, false } },
        { { 511, 20090, false }, { 520, 20091, false }, { 512, 20092, false }, { 513, 20093, false }, { 514, 20094, false }, { 515, 20095, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        if (quest::bit(quest_bits, kQuest, 1)) return quest::bit(quest_bits, kQuest, 5) ? std::vector<QuestMsg>{} : act5_detail::block_for(kBlocks[3], npc);
        if (quest::bit(quest_bits, kQuest, 0) || !(state < 4 || quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
        return act5_detail::block_for(kBlocks[state - 1], npc);
    }
    // The "!" (FUN_005874e0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc != kLarzuk) return false;
        if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 5)) return true;
        return open && state == 1 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
    }
    // The log (FUN_00586ce0).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 13)) return quest::bit(quest_bits, kQuest, 5) ? 4 : 3;
        if (!open || quest::bit(quest_bits, kQuest, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
        return state > 4 ? 0 : log;
    }
};

// Rescue on Mount Arreat (a5q2.cpp, word 0x24, record 0x20, FUN_00588510).
// State: 0 shut, 1 open (a5q1's chain), 2 Qual-Kehk told (20096) or a cage
// up, 3 the Highlands / Plateau, or out of Harrogath at 2; 5 rewarded
// (20110). Three cages (object 473, InitFn 62: FUN_005886a0) hold five
// prisoners each (act5pow 0x216, the Wussie think FUN_005ee3c0). A cage's
// door (prisondoor 0x1b2) broken frees the living prisoners within 15 of
// it (FUN_00588040); a player by them then has the cage's portal
// (object 189) open (FUN_005888d0), and they walk into it. Five prisoners dead
// fail it (log 12). The portal's opener, with every cage up and every
// prisoner dead or freed: 12 or more freed of the 15, bits 13 and 1, and 5
// / 6 / 7 for 15 / 14 / 12..13; Qual-Kehk pays three runes (two with bit 6,
// one with 7). Log: 1 told, 5 a caged prisoner by the player, 2 a portal
// with some still to free, 3 done, 12 failed, 13 rewarded.
struct RescueQuest {
    static constexpr int kQuest = 0x24, kQualKehk = act5_detail::kQualKehk, kPrisoner = act5_detail::kPrisoner, kDoor = act5_detail::kPrisonDoor;
    static constexpr int kTold = 20096, kReward = 20110, kCages = 3, kPerCage = 5, kFreeReach = 15, kFail = 5;
    static constexpr std::array<const char*, 3> kRunes{ "r07", "r08", "r09" };   // 0x732fec: Ral, Ort, Tal
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0x84
    bool closed_hook = true;       // record +0xa8 (LAB_00587a30), cleared as a level change moves the log from 0
    struct Cage {
        bool up = false;           // data +0x86 + n
        int x = 0, y = 0;          // data +0x8c + n * 8: the cage's spot, subtiles
        std::array<int, kPerCage> ids{ -1, -1, -1, -1, -1 };   // data +0xb0 + n * 20: its prisoners' unit ids
        int gone = 0;              // data +0x114 + n * 4: its prisoners dead or through the portal
        bool emptied = false;      // data +0x108 + n: five gone
        bool portal = false;       // data +0x10e + n: its portal opened (+0xf8 + n: made)
    };
    std::array<Cage, kCages> cages{};
    int made = 0;                  // data +0xa4: prisoners made
    int dead = 0;                  // data +0xa8: prisoners killed caged
    int freed = 0;                 // data +0xac: freed by their door
    std::vector<int> freed_ids;    // data +0x120, count +0x15c
    bool news = false;

    // The player joins (LAB_00588470): bit 4 cleared first.
    void join(QuestBits& quest_bits) {
        quest::set(quest_bits, kQuest, 4, false);
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open || quest::bit(quest_bits, kQuest, 1)) return;
        if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
    // a5q1's chain (FUN_005883e0): 0 → 1.
    void opened() { if (open && state == 0) state = 1; }
    // A cage comes up (InitFn 62, FUN_005886a0) at (x, y) subtiles: state 2
    // if under, the log 1 at 0 (under five dead); a slot of its own (none
    // when three are up or one's at that spot). The World then makes its
    // prisoners (FUN_00588600: up to 25 tries for five within 5 of it) and
    // calls prisoner_made.
    int cage(int x, int y) {
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
    void prisoner_made(int slot, int which, int id) { cages[std::size_t(slot)].ids[std::size_t(which)] = id; ++made; }
    // FUN_00587fc0: the cage holding the prisoner, -1 none.
    [[nodiscard]] int cage_of(int id) const {
        for (int slot = 0; slot < kCages; ++slot)
            if (std::ranges::contains(cages[std::size_t(slot)].ids, id)) return slot;
        return -1;
    }
    // A level change (FUN_00588200): into the Highlands / Plateau, or out of
    // Harrogath at 2.
    void enter(QuestBits& quest_bits, int from, int into) {
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
    void door_broken(QuestBits& quest_bits, const std::vector<int>& near_ids) {
        if (!open) return;
        for (const int id : near_ids) { freed_ids.push_back(id); ++freed; }
        failed(quest_bits);
    }
    // A prisoner died (FUN_00588040): one freed counts gone from its cage;
    // one caged is dead (five fail it), and gone too.
    void prisoner_died(QuestBits& quest_bits, int id) {
        if (!open) return;
        if (std::ranges::contains(freed_ids, id)) { went(cage_of(id)); return; }
        ++dead;
        failed(quest_bits);
        went(cage_of(id));
    }
    // A freed prisoner went through its cage's portal (FUN_00588880).
    void went(int slot) {
        if (slot < 0) return;
        if (++cages[std::size_t(slot)].gone >= kPerCage) cages[std::size_t(slot)].emptied = true;
    }
    // A caged prisoner with a player by it (FUN_00588dd0): log 5.
    void cried() { if (open && log != 5 && dead < kFail) { log = 5; news = true; } }
    // Its door broken, a player by it (FUN_005888d0): true when the cage's
    // portal is to open now; then the opener's bits.
    bool portal_due(int slot) const { return slot >= 0 && !cages[std::size_t(slot)].portal; }
    void portal_opened(QuestBits& quest_bits, int slot) {
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
    void talk_closed(int npc) {
        if (npc == kQualKehk && closed_hook && dead < kFail && std::exchange(told_heard, false)) { log = 1; news = true; closed_hook = false; }
    }
    // Heard (FUN_00587b00): the runes paid (their count) and whether the chain opens.
    struct Heard { int runes = 0; bool chain = false; };
    Heard said(QuestBits& quest_bits, int npc, int string) {
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
    // Blocks (0x733940 by 0x734270; FUN_00587d30).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 515, 20096, true } },
        { { 511, 20100, false }, { 520, 20098, false }, { 512, 20099, false }, { 513, 20101, false }, { 514, 20102, false }, { 515, 20097, false } },
        { { 511, 20107, false }, { 520, 20105, false }, { 512, 20106, false }, { 513, 20108, false }, { 514, 20109, false }, { 515, 20103, false } },
        { { 511, 20113, false }, { 520, 20111, false }, { 512, 20112, false }, { 513, 20114, false }, { 514, 20115, false }, { 515, 20110, true } },
        { { 511, 20113, false }, { 520, 20111, false }, { 512, 20112, false }, { 513, 20114, false }, { 514, 20115, false }, { 515, 20110, false } },
        { { 515, 20104, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        if (quest::bit(quest_bits, kQuest, 1)) return act5_detail::block_for(kBlocks[3], npc);
        if (quest::bit(quest_bits, kQuest, 0) || (state > 3 && !quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
        if (state == 3 && npc == kQualKehk && quest::bit(quest_bits, kQuest, 4)) return act5_detail::block_for(kBlocks[5], npc);
        return act5_detail::block_for(kBlocks[state - 1], npc);
    }
    // The "!" (FUN_00588340): Qual-Kehk, and every prisoner till it's done.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc == kPrisoner) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (npc != kQualKehk) return false;
        if (state == 1) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        return quest::bit(quest_bits, kQuest, 1);
    }
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }

private:
    // Five dead (FUN_00588040's tail): log 12, the others' bit 14 (FUN_00587f60).
    void failed(QuestBits& quest_bits) {
        if (dead < kFail || log == 0xc) return;
        log = 0xc; news = true; told_heard = false;
        someone_else(quest_bits);
    }
    static void someone_else(QuestBits& quest_bits) {
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
    }
};

// Prison of Ice (a5q3.cpp, word 0x25, record 0x21, FUN_0058a410). State: 0
// shut, 1 open (a5q2's chain, or the Arreat Plateau), 2 Malah told (20116),
// 3 the Crystalline Passage / Frozen River, or out of Harrogath at 2; 4
// frozen Anya's 20131 (no potion), 5 thawed (OperateFn 67), 6 Nihlathak's
// Temple first. The Frozen River's marker (object 460) makes frozen Anya
// (fana, object 558) 25 frames on (FUN_0058a500); Malah gives the Thawing
// Potion ("ice", 20127); used on her, she thaws (FUN_0058aab0: mode 2, then
// drehyaiced at her spot, the chain). Malah's 20132 then gives the Scroll of
// Resistance ("tr2", bit 8) and sends Anya home (record +0x84: 0 frozen, 1
// thawed, 2 in town); Anya's 20136 a rare class item (bit 9). Both: bit 0.
// Record +0x88: Nihlathak's left town (FUN_005893e0), and the Halls of
// Vaught's marker (462, InitFn 69) has him there. Log: 1 told, 2 she's
// found, 3 no potion, 4 the potion carried, 5 thawed, 6 the scroll, 13
// both rewards.
struct PrisonQuest {
    static constexpr int kQuest = 0x25, kMalah = act5_detail::kMalah, kAnya = act5_detail::kAnya, kFrozen = act5_detail::kFrozenAnya;
    static constexpr int kTold = 20116, kPotion = 20127, kNoPotion = 20131, kScroll = 20132, kAnyaReward = 20136;
    static constexpr int kFana = 558, kRiverMarker = 460, kAnyaMarker = 459, kNihlathakMarker = 462, kNihlathakBoss = 60;   // objects.txt; SuperUniques
    static constexpr int kFanaFrames = 0x19;
    static constexpr const char* kPotionCode = "ice";
    static constexpr const char* kScrollCode = "tr2";
    // Anya's item (0x735f6c normal, 0x735efc nightmare, 0x735f34 hell; by
    // class, the seventh the assassin's): rare, at the player's level.
    static inline const std::array<std::array<std::vector<const char*>, 7>, 3> kAnyaItems{ {
        { { { "am1", "am2", "am3", "am4", "am5" }, { "ob1", "ob2", "ob3", "ob4", "ob5" }, { "ne1", "ne2", "ne3", "ne4", "ne5" }, { "pa1", "pa2", "pa3", "pa4", "pa5" },
            { "ba1", "ba2", "ba3", "ba4", "ba5" }, { "dr1", "dr2", "dr3", "dr4", "dr5" }, { "ktr", "wrb", "axf", "ces", "clw", "btl", "skr" } } },
        { { { "am6", "am7", "am8", "am9", "ama" }, { "ob6", "ob7", "ob8", "ob9", "oba" }, { "ne6", "ne7", "ne8", "ne9", "nea" }, { "pa6", "pa7", "pa8", "pa9", "paa" },
            { "ba6", "ba7", "ba8", "ba9", "baa" }, { "dr6", "dr7", "dr8", "dr9", "dra" }, { "9ar", "9wb", "9xf", "9cs", "9lw", "9tw", "9qr" } } },
        { { { "amb", "amc", "amd", "ame", "amf" }, { "obb", "obc", "obd", "obe", "obf" }, { "neb", "nec", "ned", "nee", "nef" }, { "pab", "pac", "pad", "pae", "paf" },
            { "bab", "bac", "bad", "bae", "baf" }, { "drb", "drc", "drd", "dre", "drf" }, { "7ar", "7wb", "7xf", "7cs", "7lw", "7tw", "7qr" } } },
    } };
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0xac
    bool potion_heard = false;     // data +0x101
    bool scroll_again = false;     // data +0x102
    int potions = 0;               // data +0xd8: potions out
    int anya = 0;                  // data +0x84
    bool town_left = false;        // data +0x88
    bool fana_made = false;        // data +0xa4
    bool thawing = false;          // data +0x100: the thaw's timer
    int thaw_step = 0;             // data +0x8c
    bool news = false;

    // The player joins (FUN_0058a110).
    void join(const QuestBits& quest_bits, bool carries_potion) {
        if (carries_potion) potions = 1;
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open) { anya = 2; town_left = true; return; }
        if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
        if (potions) { state = 3; log = 4; }
    }
    // a5q2's chain (FUN_00589160).
    void opened() { if (open && state == 0) state = 1; }
    // FUN_005893e0's record part: Nihlathak leaves town.
    void leave_town() { town_left = true; }
    // A level change (FUN_00589d80).
    void enter(QuestBits& quest_bits, int from, int into) {
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
    // Fana's InitFn (74, FUN_0058aa50): the log 2.
    void fana_seen() { if (open && log < 2) { log = 2; news = true; } }
    // Fana used (OperateFn 67, FUN_0058abc0). Without the potion she says
    // 20131 (FUN_005456a0: heard, below; the log 3 from 1); with it, it's
    // taken: state 5, bits 13 and 1, log 5, her thaw (once). Returns whether
    // the potion was used.
    bool thaw(QuestBits& quest_bits, bool carries_potion) {
        if (!carries_potion) { heard_no_potion(); if (log == 1) { log = 3; news = true; } return false; }
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
        --potions;
        state = 5; anya = 1;
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
        log = 5; news = true;
        if (!thawing) { thawing = true; thaw_step = 0; }
        return true;
    }
    // The thaw's timer (FUN_0058aab0), a tick each: 1 fana's mode 2; 2 she's
    // drehyaiced, the chain on. Returns the step done (0 none).
    int thaw_tick() {
        if (!thawing) return 0;
        if (++thaw_step >= 2) thawing = false;
        return thaw_step;
    }
    // Malah's talk closes (FUN_00588f80).
    void talk_closed(int npc) {
        if (npc != kMalah) return;
        if (std::exchange(told_heard, false)) { log = 1; news = true; }
        if (open && potion_heard && log < 4) { log = 4; news = true; potion_heard = false; }
    }
    // Heard (FUN_00589580): the item to give and its quality / level (the
    // player's), and Anya sent home.
    struct Heard { const char* give = nullptr; bool rare = false, anya_home = false; };
    Heard said(QuestBits& quest_bits, int npc, int string, bool carries_potion, bool carries_scroll, int difficulty, int player_class, const auto& roll) {   // roll: drawn only for Anya's item
        Heard out;
        if (npc == kMalah) {
            if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state); return out; }
            if (string == kPotion) {
                if (carries_potion || potions != 0) return out;
                ++potions; potion_heard = true; out.give = kPotionCode;
                return out;
            }
            if (string == kScroll) {
                if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0)) {
                    out.give = kScrollCode;
                    quest::set(quest_bits, kQuest, 8);
                    if (!quest::bit(quest_bits, kQuest, 9)) { if (open && log < 6) { log = 6; news = true; } }
                    else { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
                    if (anya == 1) { anya = 2; out.anya_home = true; }
                    return out;
                }
                if ((quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) && !scroll_again && quest::bit(quest_bits, kQuest, 8)
                    && !quest::bit(quest_bits, kQuest, 7) && !carries_scroll) { scroll_again = true; out.give = kScrollCode; }
                return out;
            }
        } else if (npc == kAnya && string == kAnyaReward) {
            if (!quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 10)) return out;
            const auto& items = kAnyaItems[std::size_t(std::clamp(difficulty, 0, 2))][std::size_t(std::clamp(player_class, 0, 6))];
            out.give = items[roll() % items.size()]; out.rare = true;
            quest::set(quest_bits, kQuest, 9);
            if (quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
            quest::set(quest_bits, kQuest, 10);
            return out;
        }
        if (string == kNoPotion && open) { heard_no_potion(); if (log < 3) { log = 3; news = true; } }
        return out;
    }
    // Blocks (0x734320 by 0x734304; FUN_00589b00).
    static inline const std::vector<act5_detail::Entry> kBlocks[7] = {
        { { 513, 20116, true } },
        { { 511, 20119, false }, { 520, 20118, false }, { 513, 20117, false }, { 514, 20120, false }, { 515, 20121, false } },
        { { 511, 20124, false }, { 520, 20123, false }, { 513, 20122, false }, { 514, 20125, false }, { 515, 20126, false } },
        { { 511, 20129, false }, { 520, 20128, false }, { 527, 20131, true }, { 513, 20127, true }, { 515, 20130, false } },
        { { 511, 20129, false }, { 520, 20128, false }, { 515, 20130, false } },
        { { 511, 20134, false }, { 520, 20133, false }, { 512, 20136, true }, { 513, 20132, true }, { 515, 20135, false } },
        { { 511, 20134, false }, { 520, 20133, false }, { 512, 20136, false }, { 513, 20132, false }, { 515, 20135, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool carries_potion) const {
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
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool carries_potion) const {
        if (npc == kMalah) {
            if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 8)) return true;
            if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
            return state == 1 || (state == 4 && potions == 0);
        }
        if (npc == kFrozen) return open && state <= 4 && !carries_potion;
        return npc == kAnya && quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 9);
    }
    // The log (FUN_0058a300).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool carries_potion) const {
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

private:
    // 20131 heard (FUN_00589580's tail): state 4 from under, Nihlathak gone.
    void heard_no_potion() { if (open && state < 4) { state = 4; leave_town(); } }
};

// Betrayal of Harrogath (a5q4.cpp, word 0x26, record 0x22, FUN_0058bb20).
// State: 0 shut, 1 open (a5q3's chain), 2 Anya told (20137), 3 Harrogath
// after, 4 Nihlathak dead, 5 rewarded (20148, bit 4: her personalizing
// then due).
struct BetrayalQuest {
    static constexpr int kQuest = 0x26, kAnya = act5_detail::kAnya, kNihlathak = 0x20e;   // nihlathakboss
    static constexpr int kTold = 20137, kReward = 20148;
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0x86
    bool portal_due = false;       // data +0x87: Anya's portal at her talk's close
    bool portal_made = false;      // data +0x88
    bool news = false;

    // The player joins (FUN_0058b990).
    void join(const QuestBits& quest_bits) {
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open) { state = 5; return; }
        if (quest::bit(quest_bits, kQuest, 1)) return;
        if (quest::bit(quest_bits, kQuest, 3)) { portal_due = true; log = 2; state = 3; news = true; }
        else if (quest::bit(quest_bits, kQuest, 2)) { portal_due = true; log = 1; state = 2; news = true; }
    }
    // a5q3's chain (FUN_0058b140): 0 → 1.
    void opened() { if (open && state == 0) state = 1; }
    // A level change (LAB_0058bab0): out of Harrogath at 2 → 3; into the
    // temple (121) with the log 1 → 2.
    void enter(QuestBits& quest_bits, int from, int into) {
        if (from == act5_detail::kHarrogath && state == 2) { state = 3; act5_detail::mark(quest_bits, kQuest, state); }
        if (into == act5_detail::kNihlathaksTemple && log == 1) { log = 2; news = true; told_heard = false; }
    }
    // Nihlathak died (FUN_0058b7a0): bits 13 and 1 for those in his room or
    // one by it with Prison of Ice done or due (FUN_0058b4a0), 14 the rest
    // (FUN_0058b6d0); state 4; the log 4 eight ticks on (FUN_00543f10 ->
    // FUN_0058b770: log_timer), once.
    static constexpr int kLogTicks = 8;
    bool killed(QuestBits& quest_bits, bool about) {
        if (!open) return false;
        const bool prison = quest::bit(quest_bits, 0x25, 0) || quest::bit(quest_bits, 0x25, 1);
        if (about && prison && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 14)) { quest::set(quest_bits, kQuest, 1); quest::set(quest_bits, kQuest, 13); }
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
        state = 4;
        return !std::exchange(timer_set, true);
    }
    void log_timer() { if (log != 4) { log = 4; news = true; } }
    bool timer_set = false;        // data +0x85
    // Anya's talk closes (FUN_0058b050): the log 1 after 20137; true when her
    // portal to the temple is to open (FUN_0058af90: object 60 at her spot +10,
    // +5 subtiles, to level 121).
    bool talk_closed(int npc) {
        if (npc != kAnya) return false;
        if (std::exchange(told_heard, false)) { log = 1; news = true; }
        if (!portal_due || portal_made) return false;
        portal_made = true; portal_due = false;
        return true;
    }
    // Heard (FUN_0058b1c0): whether the chain (a5q5) opens.
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kAnya) return false;
        if (string == kTold && open) { state = 2; told_heard = portal_due = true; act5_detail::mark(quest_bits, kQuest, state); return false; }
        if (string != kReward) return false;
        news = true;
        quest::set(quest_bits, kQuest, 4);
        if (quest::bit(quest_bits, kQuest, 13)) { state = 5; return true; }
        return false;
    }
    // Anya's personalizing used (FUN_0058bc00): done.
    void personalized(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false); }
    // Blocks (0x734ac8 by 0x7350e8; FUN_0058b2c0): Prison of Ice done or due.
    static inline const std::vector<act5_detail::Entry> kBlocks[5] = {
        { { 512, 20137, true } },
        { { 511, 20141, false }, { 520, 20139, false }, { 513, 20140, false }, { 515, 20142, false } },
        { { 511, 20145, false }, { 520, 20144, false }, { 512, 20143, false }, { 513, 20146, false }, { 515, 20147, false } },
        { { 511, 20150, false }, { 520, 20149, false }, { 512, 20148, true }, { 513, 20151, false }, { 515, 20152, false } },
        { { 511, 20150, false }, { 520, 20149, false }, { 512, 20148, false }, { 513, 20151, false }, { 515, 20152, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        if (!quest::bit(quest_bits, 0x25, 0) && !quest::bit(quest_bits, 0x25, 1)) return {};
        if (npc == kAnya && state == 1) return !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0) ? act5_detail::block_for(kBlocks[0], npc) : std::vector<QuestMsg>{};
        if (quest::bit(quest_bits, kQuest, 1)) return act5_detail::block_for(kBlocks[quest::bit(quest_bits, kQuest, 4) ? 4 : 3], npc);
        if (quest::bit(quest_bits, kQuest, 0) || !(state < 4 || quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 5) return {};
        return act5_detail::block_for(kBlocks[state - 1], npc);
    }
    // The "!" (FUN_0058b870).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc != kAnya) return false;
        if (state == 1) return (quest::bit(quest_bits, 0x25, 0) || quest::bit(quest_bits, 0x25, 1)) && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        return quest::bit(quest_bits, kQuest, 13) && !quest::bit(quest_bits, kQuest, 4);
    }
    // The log (FUN_0058af00).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 1) || quest::bit(quest_bits, kQuest, 13)) return quest::bit(quest_bits, kQuest, 4) ? 5 : 4;
        if (!open) return 0;
        if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
        return state < 4 ? log : 0;
    }
};

// Rite of Passage (a5q5.cpp, word 0x27, record 0x23, FUN_0058ce30). State:
// 0 shut, 1 open (a5q4's chain), 2 Qual-Kehk told (20153) or the altar
// used, 3 the Arreat Summit, or out of Harrogath at 2; 5 the Ancients dead.
// The altar (object 546, OperateFn 65) has the Ancients speak (20002); that
// heard, the three statues (474..476) go to mode 3 and each, 20 frames on
// (event 7, FUN_0058c0e0), turns into its Ancient (superuniques 0x2d, 0x2b,
// 0x2c). They sleep (their AI, FUN_0058cf90) till then. All three dead: the
// players on the Summit of level 20 / 40 / 60 or more get bits 0 and 13 and
// experience (FUN_0058c5c0), the rest bit 14; the stairs (547, OperateFn 66)
// lead on to the Worldstone Keep. A player dying on the Summit puts them
// back (FUN_005be290 -> FUN_0058c000). Log: 1 told, 2 on the Summit, 3 the
// altar, 13 done.
struct RiteQuest {
    static constexpr int kQuest = 0x27, kQualKehk = act5_detail::kQualKehk, kSummit = 120;
    static constexpr int kTold = 20153, kSpeech = 20002, kAside = 20169, kWakeFrames = 20;
    static constexpr int kAltar = 546, kStairs = 547, kFarewell = 561;
    // The statues (objects) and their Ancients (FUN_0058c0e0: [0x744304] +0xb3a / +0xb36 / +0xb38).
    static constexpr std::array<int, 3> kStatues{ 475, 474, 476 }, kSuperuniques{ 0x2b, 0x2d, 0x2c };
    static constexpr std::array<int, 3> kAncients{ 0x21c, 0x21d, 0x21e };   // ancientbarb1..3
    static constexpr std::array<int, 3> kStatueOf{ 0, 2, 1 };                // each Ancient's statue (kStatues index: its superunique's class)
    // The town's congratulations (FUN_0058c290): their bits 5..9.
    struct Congrats { int npc, string, bit; };
    static constexpr std::array<Congrats, 5> kCongrats{ { { 511, 20167, 5 }, { 520, 20165, 6 }, { 512, 20166, 7 }, { 513, 20168, 8 }, { 515, 20164, 9 } } };
    bool open = true;
    int state = 0, log = 0;
    bool done = false;             // data +0: the three dead
    bool told_heard = false;       // data +1
    bool heard = false, awake = false;   // data +0x10 / +0x11
    std::array<int, 3> statue_mode{};    // data +0x30: 0, 3 waking, 4 an Ancient, 2 back
    std::array<bool, 3> statue_dead{};   // data +0x3c: its Ancient fell, its statue not back yet
    bool returning = false;              // data +4: the statues' return timer (FUN_0058bd50) runs
    int made = 0, alive = 0;       // data +0x40 / +0x44
    bool fighting = false;         // data +0x48
    bool stairs_open = false;      // data +0x4a
    int altar_mode = 0;            // data +0x4c
    bool news = false;

    static bool level_enough(int clvl, int difficulty) { return clvl >= (difficulty + 1) * 20; }   // (+0x6d + 1) x 20
    // FUN_0058c210: the steps marked, for a player of the level.
    void mark(QuestBits& quest_bits, int clvl, int difficulty) const {
        if (quest::bit(quest_bits, kQuest, 0) || !level_enough(clvl, difficulty)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
    }
    // The player joins (LAB_0058cda0).
    void join(const QuestBits& quest_bits) {
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open) { stairs_open = true; return; }
        if (quest::bit(quest_bits, kQuest, 3)) { log = 1; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
    }
    // a5q4's chain (FUN_0058cd30).
    void opened() { if (open && state == 0) state = 1; }
    // A level change (FUN_0058cba0).
    void enter(QuestBits& quest_bits, int from, int into, int clvl, int difficulty) {
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
    // Qual-Kehk's talk closes (LAB_0058be80).
    void talk_closed(int npc) { if (npc == kQualKehk && std::exchange(told_heard, false)) { log = 1; news = true; } }
    // The altar used (OperateFn 65, FUN_0058d310) at mode 0: the Ancients
    // speak (FUN_005456a0(20002): heard); state 2 from under, log 3. True
    // when they spoke.
    bool altar(QuestBits& quest_bits) {
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
    // Heard (FUN_0058c290): true when the statues wake (their event 7 due).
    bool said(QuestBits& quest_bits, int npc, int string, int clvl, int difficulty) {
        if (string == kTold) {
            if (npc != kQualKehk || !open || state != 1) return false;
            state = 2;
            if (log != 1) { log = 1; news = true; }
            mark(quest_bits, clvl, difficulty);
            told_heard = true;
            return false;
        }
        if (string == kSpeech) {
            if (open && state < 4 && log != 3) { log = 3; news = true; }
            if (std::ranges::any_of(statue_mode, [](int mode) { return mode != 0; })) return false;
            statue_mode = { 3, 3, 3 };
            made = alive = 0;
            heard = awake = true;
            return true;
        }
        if (string == kAside) { quest::set(quest_bits, kQuest, 4); return false; }
        for (const auto& congrats : kCongrats) if (npc == congrats.npc && string == congrats.string) quest::set(quest_bits, kQuest, congrats.bit);
        return false;
    }
    // A statue's event 7 (FUN_0058c0e0): its Ancient's superunique to make
    // (-1 none).
    int statue_wakes(int which) {
        if (made >= 3 || statue_mode[std::size_t(which)] == 4) return -1;
        statue_mode[std::size_t(which)] = 4;
        ++made; ++alive; fighting = true;
        return kSuperuniques[std::size_t(which)];
    }
    // An Ancient fell while they're up (FUN_0058c9a0, before the count):
    // its statue's due back, the return timer on (FUN_00543f10, every 2
    // frames). True: its death missile goes to that statue.
    // ponytail: the record's +0xc (deaths on the Summit) isn't kept; reset() covers it.
    bool ancient_fell(int statue) {
        if (!heard || !awake) return false;
        returning = true;
        statue_dead[std::size_t(statue)] = true;
        return true;
    }
    // The return timer's tick (FUN_0058bd50): the statues whose Ancient fell
    // go to mode 1 (their event 1 to mode 2 after FrameCnt1), +0x30 2; the
    // timer stops. Returns them.
    std::array<bool, 3> statues_back() {
        returning = false;
        if (!awake) return {};
        const auto back = std::exchange(statue_dead, {});
        for (std::size_t which = 0; which < back.size(); ++which) if (back[which]) statue_mode[which] = 2;
        return back;
    }
    // An Ancient died (FUN_0058c9a0): true when it was the last, the Rite
    // won (state 5, the log 13, the chain); then each player's reward().
    bool ancient_died() {
        if (!heard || !awake || alive <= 0) return false;
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
    bool reward(QuestBits& quest_bits, bool on_summit, int clvl, int difficulty) const {
        if (on_summit && level_enough(clvl, difficulty) && !quest::bit(quest_bits, kQuest, 0)) {
            quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0);
            return true;
        }
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
        return false;
    }
    // FUN_0058c5c0: 1.4M / 20M / 40M, at most what the next level takes.
    static constexpr std::array<std::int64_t, 3> kExperience{ 1400000, 20000000, 40000000 };
    // A player died on the Summit, the Rite not won (FUN_005be290): the
    // Ancients go, the statues and the altar back to mode 0 (FUN_0058c000).
    bool reset() {
        if (done) return false;
        heard = awake = fighting = returning = false;
        statue_mode = {};
        statue_dead = {};
        made = alive = 0;
        altar_mode = 0;
        return true;
    }
    // The stairs (OperateFn 66, FUN_0058d400): whether they let the player on.
    [[nodiscard]] bool stairs_pass(const QuestBits& quest_bits) const {
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) return false;
        return !((open && !done) || (fighting && !done));
    }
    // Blocks (0x735100 by 0x7357e4; FUN_0058c440).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 515, 20153, true } },
        { { 511, 20157, false }, { 520, 20155, false }, { 513, 20158, false }, { 515, 20154, false }, { 512, 20156, false } },
        { { 511, 20162, false }, { 520, 20160, false }, { 512, 20161, false }, { 513, 20163, false }, { 515, 20159, false } },
        { { 511, 20167, false }, { 520, 20165, false }, { 512, 20166, false }, { 513, 20168, false }, { 515, 20164, false } },
        { { 537, 20002, true }, { 538, 20002, true }, { 539, 20002, true } },
        { { 511, 20167, true }, { 520, 20165, true }, { 512, 20166, true }, { 513, 20168, true }, { 515, 20164, true } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        if (!open) return {};
        if (!quest::bit(quest_bits, kQuest, 0)) return state >= 1 && state <= 5 ? act5_detail::block_for(kBlocks[state - 1], npc) : std::vector<QuestMsg>{};
        for (const auto& congrats : kCongrats)
            if (npc == congrats.npc) {
                if (!quest::bit(quest_bits, kQuest, congrats.bit)) return act5_detail::block_for(kBlocks[5], npc);
                if (quest::bit(quest_bits, kQuest, 13)) return act5_detail::block_for(kBlocks[3], npc);
            }
        return {};
    }
    // The "!" (FUN_0058ccf0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const { return npc == kQualKehk && state == 1 && !quest::bit(quest_bits, kQuest, 0); }
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
};

// Eve of Destruction (a5q6.cpp, word 0x28, record 0x24, FUN_0058e500).
// State: 0 shut, 2 open (a5q5's chain, LAB_0058e390), 3 out of Harrogath
// after, 5 Baal dead. Baal on his throne (baalthrone, the BaalThrone think
// FUN_005ef320) sends his five Subjects (superuniques 61..65), each when the
// room's clear; then he's baalcrabstairs (0x22f), walking into the
// Worldstone Chamber's portal (object 563, the BaalToStairs think
// FUN_005ef620: record +0x86, the log 3), which then lets players through
// (OperateFn 70). Baal (baalcrab, bound to 0x24) dead (FUN_0058df20): those
// in the Chamber get bits 13 and 0 and the next difficulty (FUN_0058dd30),
// the rest bit 14; the log 4. Tyrael (0x209) comes as Baal's light fades
// (missile 625's end, 650 frames: FUN_0058e920); his talk closed, the last
// portal (object 565, OperateFn 72) home: bit 10. Log: 1 told, 2 the Throne,
// 3 Baal gone in, 4 dead.
struct EveQuest {
    static constexpr int kQuest = 0x28, kTyrael = 0x209, kCain = act5_detail::kCain, kThrone = 131, kChamber = 132;
    static constexpr int kChamberPortal = 563, kLastPortal = 565, kWaves = 5, kFirstSubject = 61;
    static constexpr int kBaalThrone = 543, kBaal = 544, kBaalStairs = 0x22f;
    static constexpr int kTyraelFrames = 650, kCryFrames = 250, kWaveFrames = 100;
    static constexpr int kEndMovie = 7;   // the last portal's cinematic (S->C 0x61 {7}: D2X_OUT)
    struct Congrats { int npc, string, bit; };
    static constexpr std::array<Congrats, 6> kCongrats{ { { 511, 20178, 4 }, { 520, 20177, 5 }, { 513, 20179, 6 }, { 521, 20175, 7 }, { 515, 20180, 8 }, { 512, 20176, 9 } } };
    bool open = true;
    int state = 0, log = 0;
    bool told = false;             // data +0x90
    bool baal_in = false;          // data +0x86
    bool tyrael_heard = false;     // data +0x98
    bool news = false;

    // The player joins (FUN_0058e430): done, bit 10 and the next difficulty.
    bool join(QuestBits& quest_bits) {
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
    void opened(QuestBits& quest_bits, bool in_act5) {
        if (!open || state != 0) return;
        told = true; state = 2; log = 1; news = true;
        mark(quest_bits, in_act5);
    }
    // FUN_0058d8b0: the steps marked, for a player in act 5.
    void mark(QuestBits& quest_bits, bool in_act5) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || !in_act5) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
    }
    // A level change (FUN_0058e190).
    void enter(QuestBits& quest_bits, int from, int into) {
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
    // Baal into the Chamber (FUN_0058e600).
    void baal_gone_in() { baal_in = true; if (open && log < 3) { log = 3; news = true; } }
    // Baal dead (FUN_0058df20). A player's share (FUN_0058dea0 / FUN_0058dde0):
    // in the Chamber, not done: bits 13 and 0, and true (the next
    // difficulty); else bit 14.
    void killed() { if (!open) return; log = 4; news = true; state = 5; }
    static bool reward(QuestBits& quest_bits, bool in_chamber) {
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        if (in_chamber && !quest::bit(quest_bits, kQuest, 1)) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0); return true; }
        quest::set(quest_bits, kQuest, 14);
        return false;
    }
    // Tyrael's talk closes (LAB_0058d870): his portals, once.
    bool talk_closed(int npc) { return npc == kTyrael && !std::exchange(tyrael_heard, true); }
    // Heard (LAB_0058d940): the town's congratulations.
    static void said(QuestBits& quest_bits, int npc, int string) {
        for (const auto& congrats : kCongrats) if (npc == congrats.npc && string == congrats.string) quest::set(quest_bits, kQuest, congrats.bit);
    }
    // Blocks (0x735800 by 0x735ee4; FUN_0058d9f0).
    static inline const std::vector<act5_detail::Entry> kBlocks[6] = {
        { { 511, 20171, false }, { 520, 20170, false }, { 513, 20172, false }, { 515, 20174, false }, { 512, 20173, false } },
        { { 511, 20178, true }, { 520, 20177, true }, { 513, 20179, true }, { 521, 20175, true }, { 515, 20180, true }, { 512, 20176, true } },
        { { 511, 20178, true }, { 513, 20179, true }, { 521, 20175, true }, { 515, 20180, true }, { 512, 20176, true } },
        { { 511, 20178, false }, { 513, 20179, false }, { 521, 20175, false }, { 515, 20180, false }, { 512, 20176, false } },
        { { 520, 20177, true } },
        { { 520, 20177, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
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
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) {
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
    // The log: the records' default (+0xe8 none; done at +0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
};

}  // namespace d2d::rules
