// SPDX-License-Identifier: GPL-3.0-or-later
// Act 4's quests (docs/research/re/quests-act4.md): the opening (a4q0),
// The Fallen Angel (a4q1, Izual), Terror's End (a4q2, the seals and
// Diablo) and The Hellforge (a4q3). One player's view of each record;
// World (world/quests/act4.cpp) runs them at its quest hooks. The log
// (+0xb) moves where the records call FUN_00544300; `news` is its flag 1,
// the log sent again to each player (FUN_005b3940 / 4e40 / 59c0: told()).
#pragma once

#include "quests.hpp"

#include <array>
#include <utility>
#include <vector>

namespace d2d::rules {

namespace act4_detail {
inline constexpr int kTyrael = 367, kCain = 246, kIzualGhost = 406, kFortress = 103, kChaosSanctuary = 108;
// FUN_0065c3e0: bits 2..11 cleared.
inline void clear_steps(QuestBits& quest_bits, int quest) { for (int bit = 2; bit <= 11; ++bit) quest::set(quest_bits, quest, bit, false); }
// FUN_005b3940 / FUN_005b4e40 / FUN_005b59c0: the log goes to a player
// unless done (bit 0 or 15) without 13 / 14.
bool told(const QuestBits& quest_bits, int quest);
struct Entry { int npc, string; bool greet; };
std::vector<QuestMsg> block_for(const std::vector<Entry>& block, int npc);
}  // namespace act4_detail

// a4q0 (word 0x18, FUN_005b3780): Tyrael's welcome (664) and "!" till bit 0;
// hearing it sets bit 0 (LAB_005b36a0).
struct Act4Opening {
    static constexpr int kQuest = 0x18, kTyrael = act4_detail::kTyrael, kWelcome = 664;
    [[nodiscard]] static std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) {
        if (npc == kTyrael && !quest::bit(quest_bits, kQuest, 0)) return { { kWelcome, true } };
        return {};
    }
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) { return npc == kTyrael && !quest::bit(quest_bits, kQuest, 0); }
    static void said(QuestBits& quest_bits, int npc, int string) { if (npc == kTyrael && string == kWelcome) quest::set(quest_bits, kQuest, 0); }
};

// The Fallen Angel (a4q1.cpp, word 0x19, FUN_005b42b0). State (+0xc): 1 open
// (its init), 2 Tyrael told (670), 3 in the fortress after (0x23b), 4 Izual
// dead (0x201), 5 rewarded (676). Bits: 0 done, 1 reward due, 2 / 3 / 4 its
// steps, 5 the ghost heard, 13 in this game, 14 someone else's.
struct IzualQuest {
    static constexpr int kQuest = 0x19, kIzual = 0x100, kGhost = act4_detail::kIzualGhost, kTyrael = act4_detail::kTyrael;
    static constexpr int kTold = 670, kReward = 676, kGhostSpeech = 675, kSkillPoints = 2;
    bool open = true;              // +9
    int state = 1;                 // +0xc
    int log = 0;                   // +0xb
    bool ghost_told = false;       // data +0xc
    bool ghost_due = false;        // data +0x10: Izual's spot kept for his ghost
    bool heard = false;            // data +0x11: Tyrael's 670 heard
    bool news = false;             // the log sent again (FUN_00544300 flag 1)
    quest::Players rewarded;         // +0x1c (FUN_005452c0)
    float spot_x = 0, spot_y = 0;  // data +4 / +8: where he died

    // The player joins.
    void join(const QuestBits& quest_bits);
    // Izual's first think (FUN_005b4390, from his AI): open with the log
    // under 2, log 2 and sent again (FUN_005b3940 -> FUN_00544190: S->C
    // 0x5d, the Quest Log button).
    void greet() { if (open && log < 2) { log = 2; news = true; } }
    // Tyrael's talk closes (LAB_005b41d0) with 670 heard: log 1, sent.
    void talk_closed(int npc) { if (npc == kTyrael && std::exchange(heard, false)) { log = 1; news = true; } }
    // The 3-tick timer after his death (FUN_005b3e60): at state 4, log 3 unless 4 / 13.
    void ghost_timer() { if (state == 4 && log != 4 && log != 13) { log = 3; news = true; } }
    // A player's bits for the state (LAB_005b3860).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, ghost_told ? 4 : 3);
    }
    // The fortress entered: state 2 → 3.
    void enter(QuestBits& quest_bits, int into);
    // Izual died: state 4; the player's bits 13 and 1 (reward due) unless done or due; his ghost due 3 ticks on.
    void killed(QuestBits& quest_bits, float x, float y);
    // Heard (FUN_005b39a0): the skill points it pays, and whether the chain
    // (the Hellforge) opens.
    struct Heard { int skill_points = 0; bool chain = false; };
    Heard said(QuestBits& quest_bits, int npc, int string, int player);
    // Blocks (0x73d708; FUN_005b3bb0, by state 0x73dba0).
    static inline const std::vector<act4_detail::Entry> kBlocks[5] = {
        { { 367, 670, true } },
        { { 367, 671, false }, { 246, 672, false } },
        { { 367, 673, false }, { 246, 674, false } },
        { { 367, 676, true }, { 246, 677, false }, { 406, 675, true } },
        { { 367, 676, false }, { 246, 677, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 5); }
};

// The Hellforge (a4q3.cpp, word 0x1b, FUN_005b6610). State: 0 shut, 1 open
// (the chain from a4q1, or the soulstone picked up), 2 Cain told (678 / 679),
// 3 in the fortress after, 4 the forge struck thrice (0x125), 5 rewarded
// (680). Hephasto (0x199) drops the hammer; the forge (object 376) takes the
// soulstone, then three blows of the hammer in hand, and drops the gems.
struct HellforgeQuest {
    static constexpr int kQuest = 0x1b, kCain = act4_detail::kCain, kHephasto = 0x199;
    static constexpr int kTold = 678, kGivesStone = 679, kReward = 680, kBlows = 3;
    static constexpr const char* kStone = "mss";
    static constexpr const char* kHammer = "hfh";
    bool open = true;              // +9
    int state = 0;                 // +0xc
    int log = 0;                   // +0xb
    bool stone_given = false;      // data +0x1c
    quest::Players rewarded;         // +0x1c
    int forge_mode = 0;            // data +0: 0 shut, 2 the stone in, 3 struck
    int blows = 0;                 // data +0xc
    bool heard = false;            // data +2: Cain's 678 / 679 heard
    bool news = false;             // the log sent again (FUN_00544300 flag 1)

    void join(const QuestBits& quest_bits, bool stone);
    // a4q1's chain (LAB_005b5f40): 0 → 1.
    void opened() { if (open && state == 0) state = 1; }
    void hammer_picked(const QuestBits& quest_bits);
    // The forge's InitFn (48, FUN_005b5a20): open, the log 2 unless 2 / 3 / 13, sent.
    void forge_seen() { if (open && log != 13 && log != 2 && log != 3) { log = 2; news = true; } }
    // Cain's talk closes (FUN_005b6440) with 678 / 679 heard: log 4 with
    // the stone given, else 1; sent.
    void talk_closed(int npc) { if (npc == kCain && std::exchange(heard, false)) { log = stone_given ? 4 : 1; news = true; } }
    // The fortress entered.
    void enter(const QuestBits& quest_bits, int into);
    struct Heard { bool gives_stone = false, chain = false; };
    // Heard.
    Heard said(QuestBits& quest_bits, int npc, int string, int player);
    // The forge used (OperateFn 49, FUN_005b5c10).
    struct Struck { bool takes_stone = false, takes_hammer = false, done = false; };
    Struck operate(QuestBits& quest_bits, bool stone, bool hammer_in_hand);
    // The gems (event 7, FUN_005b6710): one a round, rand(7) of the round's
    // table on the quest seed, round 4 first; then, LoD, a rune by difficulty.
    static constexpr std::array<std::array<const char*, 7>, 3> kGems{ { { "gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku" },     // round 1 (0x73e5a4)
                                                                        { "gzv", "glr", "glb", "gly", "glg", "glw", "skl" },     // rounds 2, 3 (0x73e588)
                                                                        { "gpv", "gpr", "gpb", "gpy", "gpg", "gpw", "skz" } } }; // round 4 (0x73e56c)
    static constexpr std::array<int, 3> kRuneFirst{ 1, 12, 15 };
    // The forge's event 7 at FrameCnt3 (22: objects.txt 376, +0xe4 >> 8),
    // then a round every 20 frames (+8 rounds, from 4).
    static constexpr int kStrikeFrames = 22, kRoundFrames = 20, kRounds = 4;                          // 0x73e114 / 0x73e514 / 0x73e540: rand(11) on
    // Blocks.
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool stone, int player) const;
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 5); }
};

// Terror's End (a4q2.cpp, word 0x1a, FUN_005b54c0). State: 0 shut, 1 open
// (a4q3's chain), 2 Tyrael told (681), 3 in the fortress after or in the
// Chaos Sanctuary. The five seals (objects 392..396) each open once; three
// make their superunique (0x24 Infector of Souls, 0x25 Lord De Seis, 0x26
// Grand Vizier of Chaos); all five open and those three dead release Diablo:
// the sanctuary's other monsters die, Diablo comes on the release timer's 10th run at his start
// point (object 255). His death: bits 13 and 0; classic bits 6 / 7 (Cain's
// and Tyrael's last words) and the next difficulty; LoD Tyrael's portal.
struct TerrorQuest {
    static constexpr int kQuest = 0x1a, kTyrael = act4_detail::kTyrael, kCain = act4_detail::kCain, kDiablo = 0xf3;
    static constexpr int kTold = 681, kTyraelLast = 684, kCainLast = 685, kTyraelPortal = 20000, kCainPortal = 20001;
    static constexpr std::array<int, 3> kSealBosses{ 0x24, 0x25, 0x26 };   // [0x744304]+0xb28.., by seal op 54 / 55 / 56
    static constexpr int kStartPoint = 255;   // object 255
    // The release's timer (FUN_005b4be0): 1 tick, run again till Diablo
    // comes; he comes once its run count (+0x18) is past 9.
    static constexpr int kReleaseTicks = 1, kReleaseRuns = 10;
    bool open = true;
    int state = 0, log = 0;
    std::array<bool, 5> seals{};   // data +0xc..+0x10 (objects 392..396)
    int bosses_dead = 0;           // data +0x48
    bool released = false;         // data +0x13
    bool armed = false;            // data +2: the release's timer runs
    int release_runs = 0;          // data +0x18: its runs
    int release_steps = 0;         // quest steps since it last ran
    bool diablo_due = false, diablo_made = false, dead = false;
    bool portal_due = false, portal_made = false;   // data +0x44 / +0x45
    quest::Players rewarded;
    bool heard = false;            // data +0: Tyrael's 681 heard
    bool news = false;             // the log sent again (FUN_00544300 flag 1)

    // Its +0xd8 (LAB_005b5030), and the first part of its +0xd4: in LoD, a
    // player who heard Tyrael's way on (bit 9) without word 0x1c bit 0
    // (0x5b50a2..0x5b50c3) hears it again: bit 9 cleared.
    static void join_more(QuestBits& quest_bits, bool expansion) {
        if (expansion && quest::bit(quest_bits, kQuest, 9) && !quest::bit(quest_bits, 0x1c, 0)) quest::set(quest_bits, kQuest, 9, false);
    }
    // The player joins.
    void join(QuestBits& quest_bits, bool expansion);
    void mark(QuestBits& quest_bits) const {   // FUN_005b4560
        if (quest::bit(quest_bits, kQuest, 0)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    void opened() { if (open && state == 0) state = 1; }   // LAB_005b4530
    void enter(QuestBits& quest_bits, int into);
    // Tyrael's talk closes (FUN_005b4f90) with 681 heard: log 1, sent.
    void talk_closed(int npc) { if (npc == kTyrael && std::exchange(heard, false)) { log = 1; news = true; } }
    // A seal opened: whether all five are open and the three bosses dead.
    bool seal(int object_id);
    bool boss_died() { ++bosses_dead; return ready(); }     // FUN_005b52e0, a bound non-Diablo
    [[nodiscard]] bool ready() const {
        for (const bool open_seal : seals) if (!open_seal) return false;
        return bosses_dead >= 3 && !released;
    }
    // Released (FUN_005b52e0 / FUN_005b5630): +0x18 0, +2 1, the 1-tick timer.
    void arm() { armed = true; release_runs = 0; release_steps = 0; }
    // A quest-clock step.
    bool release_step();
    void diablo_came() { armed = false; diablo_made = true; }   // +2 0, returns 1
    // Diablo died: whether the classic ending (bits 6 / 7 and the next difficulty) applies.
    void diablo_died(QuestBits& quest_bits, bool expansion, int player);
    struct Heard { bool portal = false; };
    Heard said(QuestBits& quest_bits, int npc, int string, bool expansion);
    // Blocks (0x73dbb8; FUN_005b4790, by state 0x73d56c).
    static inline const std::vector<act4_detail::Entry> kBlocks[6] = {
        { { 367, 681, true } },
        { { 367, 683, false }, { 246, 682, false } },
        { { 367, 684, true }, { 246, 685, true } },
        { { 367, 684, false }, { 246, 685, false } },
        { { 367, 20000, true }, { 246, 20001, true } },
        { { 367, 20000, false }, { 246, 20001, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool expansion) const;
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool expansion) const;
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
};

}  // namespace d2d::rules
