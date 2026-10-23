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
    // Larzuk's socketing used (his menu's entry): done.
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
// seen, 3 the Highlands / Plateau or Harrogath after, 5 rewarded (20110).
// Three cages (object 473, InitFn 62) hold five prisoners each (0x216); a
// door (0x1b2) broken frees those within 15, who walk into the portal it
// opens; five prisoners dead fails it (log 12). Freed (data +0xac) 12 or more
// of the 15: bits 13 and 1, and 5 / 6 / 7 for 15 / 14 / 12..13; Qual-Kehk
// pays three runes (two with bit 6, one with 7).
struct RescueQuest {
    static constexpr int kQuest = 0x24, kQualKehk = act5_detail::kQualKehk;
    static constexpr int kTold = 20096, kReward = 20110;
    static constexpr std::array<const char*, 3> kRunes{ "r07", "r08", "r09" };   // 0x732fec: Ral, Ort, Tal
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0x84
    int cages = 0;                 // data +0x86..: cages come up (3 at most)
    int made = 0;                  // data +0xa4: prisoners made
    int dead = 0;                  // data +0xa8: prisoners killed
    int freed = 0;                 // data +0xac: freed (their door broken by them)
    int home = 0;                  // prisoners through their portal (+0x114 summed)
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
    // A cage comes up (InitFn 62, FUN_005886a0): state 2 if under, the log
    // 1 while 0 (and under five dead); its five prisoners.
    void cage(int prisoners) {
        if (!open || cages >= 3) return;
        if (state < 2) state = 2;
        if (log == 0 && dead < 5) { log = 1; news = true; }
        ++cages;
        made += prisoners;
    }
    // A level change (FUN_00588200): into the Highlands / Plateau, or out of
    // Harrogath at 2.
    void enter(QuestBits& quest_bits, int from, int into) {
        if ((into == act5_detail::kFrigidHighlands || into == act5_detail::kArreatPlateau) && open && dead < 5) {
            const bool moved = state == 1 || state == 2;
            if (moved) state = 3;
            if (log == 0) { log = 1; news = true; }
            else if (!moved) return;
            act5_detail::mark(quest_bits, kQuest, state);
            return;
        }
        if (from == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
            state = 3;
            act5_detail::mark(quest_bits, kQuest, state);
            if (log == 0 && open && dead < 5) { log = 1; news = true; }
        }
    }
    // A door broken freed these (FUN_00588040's 0x1b2 branch).
    void door_broken(int prisoners_near) { freed += prisoners_near; }
    // A prisoner killed (not freed): five fail it, the others get bit 14.
    void prisoner_killed(QuestBits& quest_bits) {
        if (!open) return;
        if (++dead > 4 && log != 0xc) {
            log = 0xc; news = true; told_heard = false;
            if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
        }
    }
    // A cage's portal opened by the player (FUN_005888d0): once every cage is
    // up and each prisoner is dead or freed, 12 or more freed pays.
    void portal_opened(QuestBits& quest_bits) {
        if (made != dead + freed || cages < 3) {
            quest::set(quest_bits, kQuest, 4);
            if (dead < 5) { log = 2; news = true; }
            told_heard = false;
            return;
        }
        if (freed < 12) { if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14); return; }
        log = 3; news = true;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
        quest::set(quest_bits, kQuest, freed == 15 ? 5 : freed == 14 ? 6 : 7);
    }
    // Qual-Kehk's talk closes (LAB_00587a30): log 1 after 20096.
    void talk_closed(int npc) { if (npc == kQualKehk && std::exchange(told_heard, false) && dead < 5) { log = 1; news = true; } }
    // Heard (FUN_00587b00): the runes paid (their count) and whether the chain opens.
    struct Heard { int runes = 0; bool chain = false; };
    Heard said(QuestBits& quest_bits, int npc, int string) {
        Heard out;
        if (npc != kQualKehk) return out;
        if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state); return out; }
        if (string != kReward || !quest::bit(quest_bits, kQuest, 1)) return out;
        if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 13; news = true; out.chain = true; }
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
        if (npc == act5_detail::kPrisoner) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (npc != kQualKehk) return false;
        if (state == 1) return !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        return quest::bit(quest_bits, kQuest, 1);
    }
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 5); }
};

// Prison of Ice (a5q3.cpp, word 0x25, record 0x21, FUN_0058a410). State: 0
// shut, 1 open (a5q2's chain, or the Arreat Plateau entered), 2 Malah told
// (20116), 3 the Crystalline Passage / Frozen River or Harrogath after, 4 at
// frozen Anya without the potion (20131), 5 thawed (the potion used on her,
// OperateFn 67), 6 Nihlathak's Temple entered. Malah gives the Thawing Potion
// ("ice ", 20127) and, after, the Scroll of Resistance ("tr2 ", 20132, bit
// 8); Anya in town her reward (20136, bit 9). Both done: bit 0.
struct PrisonQuest {
    static constexpr int kQuest = 0x25, kMalah = act5_detail::kMalah, kAnya = act5_detail::kAnya, kFrozen = act5_detail::kFrozenAnya;
    static constexpr int kTold = 20116, kPotion = 20127, kNoPotion = 20131, kScroll = 20132, kAnyaReward = 20136;
    static constexpr const char* kPotionCode = "ice";
    static constexpr const char* kScrollCode = "tr2";
    bool open = true;
    int state = 0, log = 0;
    bool told_heard = false;       // data +0xac
    bool potion_heard = false;     // data +0x101
    bool scroll_again = false;     // data +0x102
    int potions = 0;               // data +0xd8: potions out
    int anya = 0;                  // data +0x84: 1 thawing, 2 back in town
    bool news = false;

    // The player joins (FUN_0058a110).
    void join(const QuestBits& quest_bits, bool carries_potion) {
        if (carries_potion) potions = 1;
        open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
        if (!open) { anya = 2; return; }
        if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
        if (potions) { state = 3; log = 4; }
    }
    // a5q2's chain (FUN_00589160).
    void opened() { if (open && state == 0) state = 1; }
    // A level entered (FUN_00589d80).
    void enter(QuestBits& quest_bits, int into) {
        if (into == act5_detail::kArreatPlateau) { if (open && state == 0) state = 1; }
        else if ((into == act5_detail::kCrystallinePassage || into == act5_detail::kFrozenRiver) && open) {
            const bool moved = state <= 2;
            if (moved) state = 3;
            if (log == 0) { log = 1; news = true; }
            if (moved || log == 1) act5_detail::mark(quest_bits, kQuest, state);
        }
        if (into == act5_detail::kHarrogath && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
            state = 3;
            if (log == 0) { log = 1; news = true; }
            act5_detail::mark(quest_bits, kQuest, state);
        }
        if (into >= act5_detail::kNihlathaksTemple && into <= act5_detail::kHallsOfVaught && open && state < 5) {
            if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 13)) quest::set(quest_bits, kQuest, 14);
            state = 6; anya = 2;
        }
    }
    // Frozen Anya used (OperateFn 67, FUN_0058abc0): without the potion she
    // says 20131 (the log 3 from 1); with it, it's taken: state 5, bits 13 and
    // 1, log 5, Anya thawing. Returns whether the potion was used.
    bool thaw(QuestBits& quest_bits, bool carries_potion) {
        if (!carries_potion) { if (open && state < 4) state = 4; if (log == 1) { log = 3; news = true; } return false; }
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
        --potions;
        state = 5; anya = 1;
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
        log = 5; news = true;
        return true;
    }
    // Malah's talk closes (FUN_00588f80).
    void talk_closed(int npc) {
        if (npc != kMalah) return;
        if (std::exchange(told_heard, false)) { log = 1; news = true; }
        if (open && std::exchange(potion_heard, false) && log < 4) { log = 4; news = true; }
    }
    // Heard (FUN_00589580): the item to give (a code, or null) and whether
    // the chain opens / Anya goes home.
    struct Heard { const char* give = nullptr; bool anya_home = false, anya_reward = false; };
    Heard said(QuestBits& quest_bits, int npc, int string, bool carries_potion, bool carries_scroll) {
        Heard out;
        if (npc == kMalah) {
            if (string == kTold) { told_heard = true; state = 2; act5_detail::mark(quest_bits, kQuest, state); }
            if (string == kPotion && !carries_potion && potions == 0) { ++potions; potion_heard = true; out.give = kPotionCode; }
            if (string == kScroll) {
                if (quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0)) {
                    out.give = kScrollCode;
                    quest::set(quest_bits, kQuest, 8);
                    if (!quest::bit(quest_bits, kQuest, 9)) { if (open && log < 6) { log = 6; news = true; } }
                    else { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
                    if (anya == 1) { anya = 2; out.anya_home = true; }
                } else if ((quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) && !scroll_again && quest::bit(quest_bits, kQuest, 8)
                           && !quest::bit(quest_bits, kQuest, 7) && !carries_scroll) {
                    scroll_again = true; out.give = kScrollCode;
                }
            }
        }
        if (npc == kAnya && string == kAnyaReward && quest::bit(quest_bits, kQuest, 1) && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 10)) {
            out.anya_reward = true;
            quest::set(quest_bits, kQuest, 9);
            if (quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 1, false); quest::set(quest_bits, kQuest, 0); }
            quest::set(quest_bits, kQuest, 10);
        }
        if (string == kNoPotion && open) {
            if (state < 4) state = 4;
            if (log < 3) { log = 3; news = true; }
        }
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
        if (npc == kFrozen) return {};                                     // she stands frozen (mode 12) at state 6
        if (npc == kMalah && state == 4) return !carries_potion && potions == 0 ? act5_detail::block_for(kBlocks[3], npc) : std::vector<QuestMsg>{};
        if (quest::bit(quest_bits, kQuest, 1)) {
            if ((npc == kAnya && quest::bit(quest_bits, kQuest, 9)) || (npc == kMalah && quest::bit(quest_bits, kQuest, 8))) return {};
            return act5_detail::block_for(kBlocks[5], npc);
        }
        if (quest::bit(quest_bits, kQuest, 0) || (state > 4 && !quest::bit(quest_bits, kQuest, 13)) || !open || state < 1 || state > 7) return {};
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
    // Anya's personalizing used: done.
    void personalized(QuestBits& quest_bits) { quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false); act5_detail::clear_steps(quest_bits, kQuest); }
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

}  // namespace d2d::rules
