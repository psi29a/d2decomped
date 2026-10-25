// SPDX-License-Identifier: GPL-3.0-or-later
// Act 3's quests — game.exe 1.14d, quests a3q0..a3q6.cpp (records 0xe..0x14,
// quest words 0x10..0x16; docs/research/re/quests-act3.md). Each holds one
// game's record; the player's bits are the QuestBits of their difficulty.
#pragma once

#include <monster_ids.hpp>
#include <quests.hpp>

#include <array>
#include <vector>

namespace d2d::rules {

// Act 3's opening (a3q0.cpp, record 0xe, word 0x10): Hratli greets (465;
// a sorceress hears 466, FUN_005b6f20: the player's class 1) with his "!"
// (FUN_005b6fa0) till bit 0; hearing it sets bit 0 and the game's bit 13
// (LAB_005b6ed0), as a join with bit 0 does (LAB_005b6fd0).
struct HratliGreeting {
    static constexpr int kQuest = 0x10, kHratli = monster_ids::kHratli, kGreeting = 465, kSorceress = 466;
    static void join(const QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, kQuest, 0)) quest::set(game_bits, kQuest, 13);
    }
    [[nodiscard]] static std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player_class) {
        if (npc != kHratli || quest::bit(quest_bits, kQuest, 0)) return {};
        return { { player_class == 1 ? kSorceress : kGreeting, true } };
    }
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) { return npc == kHratli && !quest::bit(quest_bits, kQuest, 0); }
    static void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string) {
        if (npc != kHratli || (string != kGreeting && string != kSorceress)) return;
        quest::set(quest_bits, kQuest, 0);
        quest::set(game_bits, kQuest, 13);
    }
};

// Lam Esen's Tome (a3q1.cpp, record 0xf, word 0x11): Alkor (254) gives it
// out once someone reaches Lower Kurast (79: state 0 -> 1, LAB_005b7620).
// The tome ("bbb ") lies on its stand in the Ruined Temple (object 193,
// InitFn 23 / OperateFn 28). Bits: 0 done, 1 reward due, 2 told, 3 under
// way, 13 done here, 14 done by others, 15 closed. The reward: 5 stat
// points (FUN_006272b0 stat 4) the moment Alkor takes it.
// N-player gap: one player: the party's bits on hand-in (FUN_005b74e0 /
// 005b75d0: everyone in act 3), the holders' count is this player's.
struct LamEsenQuest {
    static constexpr int kQuest = 0x11, kAlkor = monster_ids::kAlkor, kInit = 549, kSuccess = 564;
    static constexpr int kLowerKurast = 79, kDocks = 75, kStatPoints = 5;
    static constexpr const char* kTome = "bbb";
    bool open = true;              // +9
    int state = 0;                 // +0xc: 0 not yet, 1 given out, 2 told, 3 under way, 4 tome carried, 5 done
    bool first = true;             // data+0: the first hand-in pays everyone in act 3
    bool stand_taken = false;      // data+0x18 2: the stand's mode (InitFn 23)
    bool held = false;             // data+2: a tome was taken
    int holders = 0;               // data+8: tomes carried
    bool flourish = false;         // data+0xc: Alkor's thanks plays on the talk's close (LAB_005b76e0)
    bool rewarded = false;         // in the record's +0x1c list (FUN_005452c0): handed in this game

    // The player joins (FUN_005b7c10).
    void join(QuestBits& quest_bits, QuestBits& game_bits, bool has_tome) {
        if (quest::bit(quest_bits, kQuest, 0)) { quest::set(game_bits, kQuest, 13); return; }
        if (has_tome) { state = 4; ++holders; stand_taken = true; held = true; return; }
        if (quest::bit(quest_bits, kQuest, 3)) state = 3;
        else if (quest::bit(quest_bits, kQuest, 2)) state = 2;
    }
    // A level change (LAB_005b7620): into Lower Kurast opens it; out of the
    // docks, told -> under way, the told bit for those not done.
    void enter(QuestBits& quest_bits, int from, int into) {
        if (into == kLowerKurast && open && state == 0) state = 1;
        if (from == kDocks && state == 2 && !quest::bit(quest_bits, kQuest, 0)) { state = 3; told(quest_bits); }
    }
    // FUN_005b73a0: told, while it's told or under way.
    void told(QuestBits& quest_bits) const {
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && (state == 2 || state == 3)) quest::set(quest_bits, kQuest, 2);
    }
    // The tome taken from its stand (OperateFn 28, FUN_005b7a60): none for
    // one done (the "done" flourish, unit event 0x23), else it drops there,
    // the stand to mode 2; under way unless it was. Returns whether it drops.
    bool take(const QuestBits& quest_bits) {
        if (!open || stand_taken || quest::bit(quest_bits, kQuest, 0)) return false;
        stand_taken = held = true;
        ++holders;
        if (state != 3) state = 3;
        return true;
    }
    // The tome picked up (FUN_005b7970) / dropped (FUN_005b7a00).
    void picked_up() { if (open) state = 4; }
    void dropped() { if (open) state = 3; }
    // What `npc` says (FUN_005b7400; blocks 0x73ecf0, state -> 0x73f188).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { 254, 549, true } },
        { { 254, 550, false }, { 255, 551, false }, { 264, 552, false }, { 252, 553, false }, { 253, 554, false }, { 245, 555, false }, { 297, 556, false } },
        { { 254, 557, false }, { 255, 558, false }, { 264, 559, false }, { 252, 560, false }, { 253, 561, false }, { 245, 562, false }, { 297, 563, false } },
        { { 254, 564, true }, { 255, 565, false }, { 264, 566, false }, { 252, 567, false }, { 253, 568, false }, { 245, 569, false }, { 297, 570, false } },
        { { 245, 569, false }, { 297, 570, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool has_tome) const {
        const bool done = quest::bit(quest_bits, kQuest, 0);
        if (done && !quest::bit(quest_bits, kQuest, 13)) return {};
        int block = -1;
        if (has_tome) block = 3;
        else if (done) block = rewarded ? 4 : -1;
        else if (open && state >= 1 && state <= 3) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // Alkor's "!" (FUN_005b72b0): given out and not done or closed, or the tome carried.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool has_tome) const {
        if (npc != kAlkor) return false;
        return (state == 1 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15)) || has_tome;
    }
    // The player heard `string` from `npc` (FUN_005b7740). Alkor's thanks
    // with the tome: it's taken (`gives_tome`), and the first time everyone in
    // act 3 not done gets bits 13, 0, 1, the reward (bit 1 -> 5 stat points),
    // others bit 14; done here: the game's bit 13, state 5.
    struct Heard { bool takes_tome = false; int stat_points = 0; };
    Heard said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool has_tome, bool in_act3) {
        if (npc != kAlkor || quest::bit(quest_bits, kQuest, 0)) return {};
        if (string == kInit) { state = 2; told(quest_bits); return {}; }
        if (string != kSuccess) return {};
        Heard out;
        if (has_tome) { out.takes_tome = true; flourish = true; rewarded = true; if (holders > 0) --holders; }
        if (first) {
            if (in_act3 && !quest::bit(quest_bits, kQuest, 1)) for (const int bit : { 13, 0, 1 }) quest::set(quest_bits, kQuest, bit);
            first = false;
            if (quest::bit(quest_bits, kQuest, 1)) { out.stat_points = kStatPoints; quest::set(quest_bits, kQuest, 1, false); }
            if (!quest::bit(quest_bits, kQuest, 0)) quest::set(quest_bits, kQuest, 14);
        }
        if (quest::bit(quest_bits, kQuest, 13) && open) { quest::set(game_bits, kQuest, 13); state = 5; }
        return out;
    }
    // Its log (FUN_005b7d70): hidden till Act 2's last (word 0xf bit 0);
    // done 0xb (0xd here); the tome carried 2; under way 1, lost (taken, none
    // carried) 8 in single player (9 else); found 0xc (1 in act 3 with one held).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool has_tome, bool single_player, bool in_act3) const {
        if (!quest::bit(quest_bits, 0xf, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 0)) return quest::bit(quest_bits, kQuest, 13) ? 0xd : 0xb;
        if (has_tome) return 2;
        if (!open) return 0;
        if (state < 4) return held && holders == 0 ? (single_player ? 8 : 9) : 1;
        return in_act3 && holders != 0 ? 1 : 0xc;
    }
};

// The Golden Bird (a3q4.cpp, record 0x12, word 0x14). The first unique or
// superunique placed in act 3 (FUN_005a09e0 -> FUN_00544e80: not an NPC,
// MonStats +0xd & 0x40, nor class 0x197) while the record's armed (data+1,
// +0xc at init) carries the quest (FUN_005bac70); its death (FUN_005bab60)
// drops the jade figurine ("j34 ") unless its killer's done. Cain hears of it
// (527: bit 2), Meshif (264) swaps it for the golden bird ("g34 ", 529), Cain
// on the bird (531: bit 4), Alkor takes the bird (534: bit 1) and brews the
// Potion of Life ("xyz ", 538: bits 0, 13, 5; 2..11 cleared, FUN_0065c3e0):
// drunk with bit 5, +20 max life (0x55e44e: stat 7 0x1400) and bit 5 clears.
// N-player gap: one player: the carried count and the rewarded list.
struct GoldenBirdQuest {
    static constexpr int kQuest = 0x14, kCain = monster_ids::kCainAct3, kAsheara = monster_ids::kAsheara, kMeshif = monster_ids::kMeshifAct3,
                         kAlkor = monster_ids::kAlkor;
    static constexpr int kCainFigurine = 527, kMeshifSwap = 529, kCainBird = 531, kAlkorTakes = 534, kAlkorPotion = 538;
    static constexpr const char *kFigurine = "j34", *kBird = "g34", *kPotion = "xyz";
    static constexpr int kLifeBonus = 20;
    bool open = true;              // +9
    int state = 0;                 // +0xc: 1 figurine, 2 Cain told, 3 the bird, 4 Alkor has it, 5 done
    int log = 0;                   // +0xb
    bool armed = true;             // data+1: the next boss made in act 3 carries it
    bool due = true;               // data+0xc: the figurine's still to drop
    int bound = -1;                // data+4: its carrier's unit id
    bool flourished = false;       // bit 6's flourish on the figurine's first pickup
    bool rewarded = false;         // +0x1c: given the potion this game

    // The player joins (FUN_005ba870).
    void join(const QuestBits& quest_bits, bool has_bird, bool has_figurine) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) { open = false; due = armed = false; return; }
        if (has_bird) { due = armed = false; state = 3; log = quest::bit(quest_bits, kQuest, 4) ? 4 : 3; return; }
        if (has_figurine) { due = armed = false; state = log = quest::bit(quest_bits, kQuest, 2) ? 2 : 1; }
    }
    // A boss made in act 3 (FUN_00544e80 -> FUN_005bac70).
    void made(int unit_id) {
        if (!open || !due || !armed) return;
        bound = unit_id;
        armed = false;
    }
    // Its carrier died (FUN_005bab60): the figurine drops unless the killer's done.
    bool killed(const QuestBits& quest_bits, int unit_id) {
        if (!open || !due || unit_id != bound) return false;
        bound = -1;
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        due = false;
        if (state == 0) state = 1;
        return true;
    }
    // A quest item picked up (FUN_005ba6a0): the bird, state 3; the figurine,
    // state 1 and bit 6 (act3_find_jade, unit event 0x48, the first time).
    // Returns whether to play the flourish.
    bool picked_up(QuestBits& quest_bits, bool bird) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
        state = bird ? 3 : 1;
        if (bird || quest::bit(quest_bits, kQuest, 6)) return false;
        quest::set(quest_bits, kQuest, 6);
        return true;
    }
    // What `npc` says (FUN_005b9ed0; blocks 0x7401d0).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[8] = {
        { { 245, 527, true }, { 252, 528, false } },
        { { 264, 529, true } },
        { { 245, 531, false }, { 254, 534, true }, { 297, 530, false }, { 253, 532, false }, { 252, 533, false } },
        { { 245, 531, true }, { 253, 532, false }, { 252, 533, false } },
        { { 254, 534, false }, { 255, 535, false }, { 253, 536, false }, { 297, 537, false } },
        { { 254, 538, true }, { 264, 539, false }, { 245, 540, false }, { 255, 541, false } },
        { { 254, 538, false }, { 264, 539, false }, { 245, 540, false }, { 255, 541, false }, { 297, 542, false } },
        { { 245, 527, false }, { 252, 528, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool has_figurine, bool has_bird) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 0)) block = rewarded ? 6 : -1;
        else if (quest::bit(quest_bits, kQuest, 1)) block = 5;
        else if (has_bird) block = npc == kCain && !quest::bit(quest_bits, kQuest, 4) ? 3 : 2;
        else if (has_figurine) {
            const bool told = quest::bit(quest_bits, kQuest, 2), asks = npc == kCain || npc == kAsheara;
            block = asks ? (told ? 7 : 0) : 1;
        }
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_005b9d90).
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc, bool has_figurine, bool has_bird) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return false;
        if (npc == kAlkor) return has_bird;
        if (npc == kMeshif) return has_figurine;
        if (npc == kCain) return (has_figurine && !quest::bit(quest_bits, kQuest, 2)) || (has_bird && !quest::bit(quest_bits, kQuest, 4));
        return false;
    }
    // The player heard `string` from `npc` (FUN_005ba320): what changes hands.
    struct Heard { bool takes_figurine = false, gives_bird = false, takes_bird = false, gives_potion = false; };
    Heard said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool has_figurine, bool has_bird) {
        Heard out;
        if (quest::bit(quest_bits, kQuest, 0)) return out;
        if (npc == kCain && string == kCainFigurine) { quest::set(quest_bits, kQuest, 2); if (state == 1) state = 2; }
        if (npc == kCain && string == kCainBird) quest::set(quest_bits, kQuest, 4);
        if (npc == kMeshif && string == kMeshifSwap && has_figurine) { out.takes_figurine = out.gives_bird = true; state = 3; }
        if (npc == kAlkor && string == kAlkorTakes && has_bird && !quest::bit(quest_bits, kQuest, 1)) {
            out.takes_bird = true;
            if (open && state != 4) state = 4;
            quest::set(quest_bits, kQuest, 1);
        }
        if (npc == kAlkor && string == kAlkorPotion && quest::bit(quest_bits, kQuest, 1)) {
            quest::set(quest_bits, kQuest, 1, false);
            quest::set(quest_bits, kQuest, 0);
            quest::set(quest_bits, kQuest, 13);
            for (int bit = 2; bit < 12; ++bit) quest::set(quest_bits, kQuest, bit, false);
            quest::set(quest_bits, kQuest, 5);
            if (open) state = 5;
            out.gives_potion = rewarded = true;
            quest::set(game_bits, kQuest, 13);
        }
        return out;
    }
    // The Potion of Life drunk (0x55e44e): bit 5 clears, +20 max life.
    static bool drink(QuestBits& quest_bits) {
        if (!quest::bit(quest_bits, kQuest, 5)) return false;
        quest::set(quest_bits, kQuest, 5, false);
        return true;
    }
    // Its log (FUN_005ba170).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool has_figurine, bool has_bird, bool single_player) const {
        if (!quest::bit(quest_bits, 0xf, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 1)) return 5;
        if (quest::bit(quest_bits, kQuest, 0)) return quest::bit(quest_bits, kQuest, 13) ? 0xd : 0xb;
        if (has_bird) return quest::bit(quest_bits, kQuest, 4) ? 4 : 3;
        if (has_figurine) return quest::bit(quest_bits, kQuest, 2) ? 2 : 1;
        if (!open) return 0;
        return log > 5 ? (single_player ? 6 : 7) : log;
    }
};

// The Blade of the Old Religion (a3q3.cpp, record 0x11, word 0x13): given
// out once someone reaches the Flayer Jungle (78: state 0 -> 1); Hratli
// (253) tells (571: state 2). The Gidbinn's altar (object 252 "gidbinn
// decoy", InitFn 25 / OperateFn 31; the stand 251, InitFn 39) used: 7 ticks
// on, with a player in its room, a unique fetish pack (fetish11, class
// 0x197; FUN_005b9930) comes; its death (FUN_005b9980) drops the Gidbinn
// ("g33 "). Ormus (255) takes it (587: bit 6; state 5, the chain) and gives
// a rare ring (593: bit 8; item level 21 / 35 / 75); Asheara (252) a free
// mercenary (589: bit 7, FUN_00579180). Both: bits 13 and 0. Bits: 2 told,
// 3 Hratli's told, 4 the Gidbinn found, 5 carried, 9 its first pickup, 14
// others'. N-player gap: one player: the party's bits (FUN_005b91b0).
struct BladeQuest {
    static constexpr int kQuest = 0x13, kHratli = monster_ids::kHratli, kOrmus = monster_ids::kOrmus, kAsheara = monster_ids::kAsheara;
    static constexpr int kHratliTells = 571, kOrmusTakes = 587, kAshearaMerc = 589, kOrmusRing = 593;
    static constexpr int kFlayerJungle = 78, kDocks = 75, kGuardian = 407, kTimerTicks = 7;
    static constexpr const char *kGidbinn = "g33", *kRing = "rin";
    static constexpr std::array<int, 3> kRingLevel{ 21, 35, 75 };
    bool open = true;              // +9
    int state = 0;                 // +0xc: 1 given out, 2 told, 3 under way, 4 carried, 5 handed in
    int log = 0;                   // +0xb
    bool found = false;            // data+0: the Gidbinn dropped
    bool told_pending = false;     // data+1: Hratli's told, marks on the talk's close
    bool spawned = false;          // data+2: the guardians came
    bool used = false;             // data+3: the altar used
    bool altar_done = false;       // data+0x2c 2: the altar's mode
    int guardian = -1;             // data+0x24: the unique's id
    bool rewarded = false;         // +0x1c

    // FUN_005b8b20: a player's bits as it stands.
    void mark(QuestBits& quest_bits, bool has_gidbinn) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 6)) return;
        if (has_gidbinn) { quest::set(quest_bits, kQuest, 5); return; }
        if (!found) { if (state == 2) quest::set(quest_bits, kQuest, 3); return; }
        quest::set(quest_bits, kQuest, 4);
        if (!quest::bit(quest_bits, kQuest, 3)) quest::set(quest_bits, kQuest, 2);
    }
    // The player joins (FUN_005b9700).
    void join(QuestBits& quest_bits, bool has_gidbinn) {
        if (quest::bit(quest_bits, kQuest, 0)) { altar_done = true; return; }
        if (has_gidbinn) { state = log = 4; return; }
        if (quest::bit(quest_bits, kQuest, 6)) {
            altar_done = true; state = 5;
            if (!quest::bit(quest_bits, kQuest, 7)) { log = 5; return; }
            if (quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 0); open = false; return; }
            log = 6;
            return;
        }
        if (quest::bit(quest_bits, kQuest, 5)) { state = 3; log = 2; }
        if (quest::bit(quest_bits, kQuest, 4) || quest::bit(quest_bits, kQuest, 3)) { state = 2; log = 2; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 3; log = 1; }
    }
    // A level change (LAB_005b9090).
    void enter(QuestBits& quest_bits, int from, int into, bool has_gidbinn) {
        if (into == kFlayerJungle && open && state == 0) state = 1;
        if (from == kDocks && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15)) { state = 3; mark(quest_bits, has_gidbinn); }
    }
    // The altar used (OperateFn 31, FUN_005b9b40): one with bits 0, 7 or 8
    // gets the flourish; else the first use starts the 7-tick timer.
    // Returns whether the timer starts.
    bool use_altar(const QuestBits& quest_bits) {
        if (altar_done || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 7) || quest::bit(quest_bits, kQuest, 8) || !open) return false;
        if (state == 0) state = 1;
        const bool first = !used;
        used = true;
        return first;
    }
    // The guardian killed (FUN_005b9980): the Gidbinn drops.
    bool killed(int unit_id) {
        if (!spawned || !open || unit_id != guardian) return false;
        found = true;
        guardian = -1;
        return true;
    }
    // The Gidbinn picked up (FUN_005b9520): state 4, bit 9's flourish once.
    bool picked_up(QuestBits& quest_bits) {
        if (!open) return false;
        state = 4;
        mark(quest_bits, true);
        if (quest::bit(quest_bits, kQuest, 9)) return false;
        quest::set(quest_bits, kQuest, 9);
        return true;
    }
    // What `npc` says (FUN_005b8ec0; blocks 0x73fb98, state -> 0x7401b8).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[7] = {
        { { 253, 571, true } },
        { { 254, 572, false }, { 255, 573, false }, { 264, 574, false }, { 252, 575, false }, { 253, 576, false }, { 245, 577, false }, { 297, 578, false } },
        { { 254, 579, false }, { 255, 580, false }, { 264, 581, false }, { 252, 582, false }, { 253, 583, false }, { 245, 584, false }, { 297, 585, false } },
        { { 254, 586, false }, { 255, 587, true }, { 264, 588, false }, { 253, 590, false }, { 245, 591, false }, { 297, 592, false } },
        { { 264, 588, false }, { 254, 586, false }, { 245, 591, false }, { 297, 592, false } },
        { { 264, 588, false }, { 252, 589, true }, { 254, 586, false }, { 245, 591, false }, { 297, 592, false } },
        { { 255, 593, true } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool has_gidbinn) const {
        int block = -1;
        const bool carried = quest::bit(quest_bits, kQuest, 5), given = quest::bit(quest_bits, kQuest, 6), merc = quest::bit(quest_bits, kQuest, 7), ring = quest::bit(quest_bits, kQuest, 8);
        if (quest::bit(quest_bits, kQuest, 0)) block = rewarded ? 4 : -1;
        else if (has_gidbinn && carried && !ring) block = 3;
        else if (given) {
            if (npc == kAsheara) block = !merc ? 5 : ring ? -1 : 6;
            else if (npc == kOrmus) block = !ring ? 6 : !merc ? 5 : -1;
            else block = !merc ? 5 : ring ? -1 : 6;
        } else if (carried) block = 4;
        else if (state >= 1 && state <= 3 && !has_gidbinn) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_005b8e10).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool has_gidbinn) const {
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        if (npc == kHratli) return !has_gidbinn && state == 1;
        if (npc == kOrmus) return has_gidbinn || (quest::bit(quest_bits, kQuest, 6) && !quest::bit(quest_bits, kQuest, 8));
        if (npc == kAsheara) return quest::bit(quest_bits, kQuest, 6) && !quest::bit(quest_bits, kQuest, 7);
        return false;
    }
    // The player heard `string` from `npc` (FUN_005b9240).
    struct Heard { bool takes_gidbinn = false, ring = false, merc = false; };
    Heard said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool has_gidbinn) {
        Heard out;
        if (quest::bit(quest_bits, kQuest, 0)) return out;
        if (npc == kHratli && string == kHratliTells) { state = 2; told_pending = true; }
        if (npc == kOrmus && string == kOrmusTakes) {
            if (has_gidbinn && quest::bit(quest_bits, kQuest, 5) && !quest::bit(quest_bits, kQuest, 8)) { out.takes_gidbinn = true; quest::set(quest_bits, kQuest, 6); }
            if (open) { quest::set(game_bits, kQuest, 13); state = 5; }
        }
        auto both = [&] { if (quest::bit(quest_bits, kQuest, 7) && quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0); rewarded = true; } };
        if (npc == kOrmus && string == kOrmusRing && quest::bit(quest_bits, kQuest, 6) && !quest::bit(quest_bits, kQuest, 8)) { quest::set(quest_bits, kQuest, 8); out.ring = true; both(); }
        if (npc == kAsheara && string == kAshearaMerc && !quest::bit(quest_bits, kQuest, 7)) { quest::set(quest_bits, kQuest, 7); out.merc = true; both(); }
        return out;
    }
    // The talk closed (LAB_005b8c50): Hratli's told marks.
    void talk_closed(QuestBits& quest_bits, int npc, bool has_gidbinn) {
        if (npc == kHratli && told_pending) { told_pending = false; mark(quest_bits, has_gidbinn); }
    }
    // Its log (FUN_005b8cc0).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool has_gidbinn) const {
        if (!quest::bit(quest_bits, 0xf, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 0)) return quest::bit(quest_bits, kQuest, 13) ? 0xd : 0xb;
        if (has_gidbinn) return 4;
        if (!quest::bit(quest_bits, kQuest, 6)) {
            if (quest::bit(quest_bits, kQuest, 4)) return found ? 3 : 2;
            if (quest::bit(quest_bits, kQuest, 3)) return 2;
            return quest::bit(quest_bits, kQuest, 2) ? 1 : 0;
        }
        if (!quest::bit(quest_bits, kQuest, 7)) return 5;
        return quest::bit(quest_bits, kQuest, 8) ? 0 : 6;
    }
};

// Khalim's Will (a3q2.cpp, record 0x10, word 0x12): Cain's (245) all. The
// Great Marsh (77) opens it (state 1); Cain tells (543: state 2, bit 2). The
// parts: Khalim's eye ("qey "), brain ("qbr "), heart ("qhr ") from three
// chests (objects 407 / 406 / 405, OperateFns 58 / 59 / 57: gold, the part,
// a chest's drops) and his flail ("qf1 ") from the Council (a3q5's); the
// Cube makes his Will ("qf2 ", FUN_005b86e0). Shown to Cain each sets a bit
// (545 eye 3, 546 brain 4, 544 heart 6, 547 flail 5, 548 the Will 7). The
// Will breaks the compelling orb (a3q5's): bit 0. The sewers' stairs
// (object 366, InitFn 41 / OperateFn 44) open by the lever (367, InitFn 42
// / OperateFn 45, FUN_005b8530): mode 2 (data+8) lets the way down to
// sewers 2 be taken (FUN_0059d9d0 -> FUN_005550b0).
struct KhalimQuest {
    static constexpr int kQuest = 0x12, kCain = monster_ids::kCainAct3, kGreatMarsh = 77;
    static constexpr int kTells = 543, kHeart = 544, kEye = 545, kBrain = 546, kFlail = 547, kWill = 548;
    static constexpr const char *kEyeCode = "qey", *kBrainCode = "qbr", *kHeartCode = "qhr", *kFlailCode = "qf1", *kWillCode = "qf2";
    struct Parts { bool eye = false, brain = false, heart = false, flail = false, will = false; };
    bool open = true;              // +9
    int state = 0;                 // +0xc: 1 given out, 2 told
    int log = 0;                   // +0xb
    bool told_pending = false;     // data+1
    bool stairs_open = false;      // data+8 2: the sewers' stairs

    // The player joins (LAB_005b8470).
    void join(const QuestBits& quest_bits, const Parts& parts) {
        if (quest::bit(quest_bits, kQuest, 0)) { open = false; stairs_open = true; return; }
        const bool any = parts.eye || parts.brain || parts.heart || parts.flail || parts.will;
        if (quest::bit(quest_bits, kQuest, 7) || quest::bit(quest_bits, kQuest, 2) || any) { state = 2; log = 7; }
    }
    // A level change (LAB_005b7ff0): into the Great Marsh, given out.
    void enter(int into) { if (into == kGreatMarsh && open && state < 3) state = 1; }
    // The player heard `string` from Cain (FUN_005b8060).
    void said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kCain) return;
        switch (string) {
        case kTells: state = 2; quest::set(quest_bits, kQuest, 2); told_pending = true; break;
        case kHeart: quest::set(quest_bits, kQuest, 6); break;
        case kEye: quest::set(quest_bits, kQuest, 3); break;
        case kBrain: quest::set(quest_bits, kQuest, 4); break;
        case kFlail: quest::set(quest_bits, kQuest, 5); break;
        case kWill: quest::set(quest_bits, kQuest, 7); break;
        default: break;
        }
    }
    // What Cain says (FUN_005bd630; blocks 0x73f1a0: 0..5 his greetings on
    // the quest and each part, 6..11 the same as topics).
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, const Parts& parts, bool orb_whole) const {
        if (npc != kCain) return {};
        static constexpr std::array<int, 6> kStrings{ kTells, kEye, kHeart, kBrain, kFlail, kWill };
        auto block = [&](int index) { return std::vector<QuestMsg>{ { kStrings[std::size_t(index % 6)], index < 6 } }; };
        if (state != 0 && !quest::bit(quest_bits, kQuest, 2)) return block(0);
        if (parts.will && !quest::bit(quest_bits, kQuest, 7) && orb_whole) return block(5);
        if (parts.flail && !quest::bit(quest_bits, kQuest, 5)) return block(4);
        if (parts.eye && !quest::bit(quest_bits, kQuest, 3)) return block(1);
        if (parts.heart && !quest::bit(quest_bits, kQuest, 6)) return block(2);
        if (parts.brain && !quest::bit(quest_bits, kQuest, 4)) return block(3);
        const int count = int(parts.flail) + int(parts.eye) + int(parts.heart) + int(parts.brain);
        if (count == 0) {
            if (parts.will) return block(11);
            return quest::bit(quest_bits, kQuest, 2) ? block(6) : std::vector<QuestMsg>{};
        }
        if (parts.flail) return block(10);
        if (parts.heart) return block(8);
        if (parts.eye) return block(7);
        if (parts.brain) return block(9);
        return block(6);
    }
    // Cain's "!" (FUN_005bd500).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, const Parts& parts, bool orb_whole) const {
        if (npc != kCain) return false;
        if (state != 0 && !quest::bit(quest_bits, kQuest, 2)) return true;
        if (parts.flail && !quest::bit(quest_bits, kQuest, 5)) return true;
        if (parts.eye && !quest::bit(quest_bits, kQuest, 3)) return true;
        if (parts.heart && !quest::bit(quest_bits, kQuest, 6)) return true;
        if (parts.brain && !quest::bit(quest_bits, kQuest, 4)) return true;
        if (parts.will && !quest::bit(quest_bits, kQuest, 7) && orb_whole) return true;
        return parts.flail && parts.eye && parts.heart && parts.brain && !quest::bit(quest_bits, kQuest, 5);
    }
    // The talk closed (LAB_005b8020): the told marks.
    void talk_closed(int npc) { if (npc == kCain) told_pending = false; }
    // The lever pulled (FUN_005b8530): the stairs open.
    bool pull_lever() {
        if (stairs_open) return false;
        stairs_open = true;
        return true;
    }
    // Its log (FUN_005bd850).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, const Parts& parts, bool orb_smashed) const {
        if (!quest::bit(quest_bits, 0xf, 0)) return 0;
        // unverified (source: the other records' logs): FUN_005bd850 has no done case.
        if (quest::bit(quest_bits, kQuest, 0)) return quest::bit(quest_bits, kQuest, 13) ? 0xd : 0xb;
        const bool told = quest::bit(quest_bits, kQuest, 2);
        if (parts.will) return orb_smashed ? 0xc : 6;
        const int count = int(parts.flail) + int(parts.eye) + int(parts.heart) + int(parts.brain);
        if (count == 4) return quest::bit(quest_bits, kQuest, 5) ? 5 : 7;
        if (count != 0) {
            if (!parts.eye) return 1;
            if (!parts.brain) return 2;
            if (!parts.heart) return 4;
            return parts.flail ? 7 : 3;
        }
        return told || (open && state > 1) ? 1 : 0;
    }
};

// The Blackened Temple (a3q5.cpp, record 0x13, word 0x15): the Kurast
// Causeway (82) opens it (state 1); Ormus (255) tells (594: state 3, 2 if
// the game's bit 13's set); leaving the docks, 5 (4). The Council in
// Travincal (superuniques 26 / 27 / 29: Ismail, Geleb, Toorc) carry the
// record (FUN_005a49b0 binds 0x13); each death (FUN_005bbc00): the first
// drops Khalim's flail ("qf1 ") for each player in act 3 who needs one, the
// second a Horadric Cube ("box ") for each without; the last: state 6 (7 with
// the orb broken), everyone in act 3 by the killer gets bit 13 and bit 4 (bit
// 0 with Khalim's Will done, FUN_005bae20), the rest bit 14. The compelling
// orb (object 404, OperateFn 53, FUN_005bb980) hit twice with Khalim's Will
// held: word 0x12 bits 0, 13 (Khalim's Will done), the Will gone, bit 0 for
// one with bit 4, the orb broken. Cain (245) hears it (626) with bit 4: done
// (bit 0; Khalim's parts gone once his Will's done). Bits: 2 told, 3 under
// way, 4 the Council dead (Cain's due), 13 / 14 here / others'.
// N-player gap: one player: the party's and the room's credit, the counts of
// players needing a flail or a Cube.
struct BlackenedTempleQuest {
    static constexpr int kQuest = 0x15, kOrmus = monster_ids::kOrmus, kCain = monster_ids::kCainAct3, kTells = 594, kDone = 626;
    static constexpr int kCauseway = 82, kDocks = 75;
    static constexpr std::array<int, 3> kCouncil{ 26, 27, 29 };   // SuperUniques rows
    static constexpr const char *kFlail = "qf1", *kCube = "box";
    bool open = true;              // +9
    int state = 0;                 // +0xc
    int log = 0;                   // +0xb
    int council = 3;               // data+0x34: Council members left
    bool flail_dropped = false;    // data+0xd
    bool cube_dropped = false;     // data+0xe
    int orb_hits = 0;              // data+0x38
    bool orb_broken = false;       // data+0xc
    bool rewarded = false;         // +0x1c

    // The player joins (LAB_005bb480).
    void join(QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, KhalimQuest::kQuest, 0)) orb_broken = true;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 4)) { quest::set(game_bits, kQuest, 13); open = false; return; }
        const bool tome = quest::bit(quest_bits, LamEsenQuest::kQuest, 0);
        if (quest::bit(quest_bits, kQuest, 3)) { log = 3; state = tome ? 4 : 5; }
        else if (quest::bit(quest_bits, kQuest, 2)) { log = 2; state = tome ? 2 : 3; }
    }
    // A level change (LAB_005bb120).
    void enter(const QuestBits& quest_bits, int from, int into, bool game_done) {
        if (into == kCauseway && open && state == 0) state = 1;
        if (from != kDocks || state < 2 || state > 3 || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 4)) return;
        state = game_done ? 4 : 5;   // LAB_005badc0 after it marks none: the state's past 2 / 3
    }
    // A Council member died (FUN_005bbc00). What it drops.
    struct Drops { bool flail = false, cube = false, last = false; };
    Drops council_died(QuestBits& quest_bits, QuestBits& game_bits, bool in_act3, bool khalim_done, bool has_flail_or_will, bool has_cube) {
        Drops out;
        if (!flail_dropped) {
            if (in_act3 && !khalim_done && !has_flail_or_will) { out.flail = true; flail_dropped = true; }
        } else if (!cube_dropped) {
            cube_dropped = true;
            out.cube = in_act3 && !has_cube;
        }
        if (!open || council <= 0 || --council != 0) return out;
        out.last = true;
        state = orb_broken ? 7 : 6;
        if (in_act3 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 4)) {
            quest::set(quest_bits, kQuest, khalim_done ? 0 : 4);
            quest::set(quest_bits, kQuest, 13);
        }
        quest::set(game_bits, kQuest, 13);
        return out;
    }
    // The orb hit with Khalim's Will held (FUN_005bb980): broken on the second.
    bool hit_orb(QuestBits& quest_bits) {
        if (++orb_hits < 2 || orb_broken) return false;
        quest::set(quest_bits, KhalimQuest::kQuest, 0);
        quest::set(quest_bits, KhalimQuest::kQuest, 13);
        if (quest::bit(quest_bits, kQuest, 4) && !quest::bit(quest_bits, kQuest, 0)) quest::set(quest_bits, kQuest, 0);
        orb_broken = true;
        return true;
    }
    // The player heard `string` (FUN_005bb210). Returns whether Khalim's
    // parts go (Cain's, with his Will done).
    bool said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool game_done) {
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        if (npc == kOrmus && string == kTells) { state = game_done ? 2 : 3; return false; }
        if (npc != kCain || string != kDone || !quest::bit(quest_bits, kQuest, 4)) return false;
        const bool parts_go = quest::bit(quest_bits, KhalimQuest::kQuest, 0);
        if (quest::bit(quest_bits, kQuest, 13) && state != 7) { quest::set(game_bits, kQuest, 13); state = 7; }
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 4, false);
        rewarded = true;
        return parts_go;
    }
    // What `npc` says (FUN_005bafd0; blocks 0x7408b8, state -> 0x740ed8).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[7] = {
        { { 255, 594, true } },
        { { 254, 595, false }, { 255, 597, false }, { 264, 599, false }, { 252, 601, false }, { 253, 603, false }, { 245, 605, false }, { 297, 607, false } },
        { { 254, 596, false }, { 255, 598, false }, { 264, 600, false }, { 252, 602, false }, { 253, 604, false }, { 245, 606, false }, { 297, 608, false } },
        { { 254, 609, false }, { 255, 611, false }, { 264, 612, false }, { 252, 614, false }, { 253, 616, false }, { 245, 618, false }, { 297, 619, false } },
        { { 254, 610, false }, { 264, 613, false }, { 252, 615, false }, { 253, 617, false }, { 297, 620, false } },
        { { 254, 621, false }, { 255, 622, false }, { 264, 623, false }, { 252, 624, false }, { 253, 625, false }, { 245, 626, true }, { 297, 627, false } },
        { { 254, 621, false }, { 255, 622, false }, { 264, 623, false }, { 252, 624, false }, { 253, 625, false }, { 245, 626, false }, { 297, 627, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 4)) block = orb_broken ? -1 : 5;
        else if (quest::bit(quest_bits, kQuest, 0) && !rewarded) block = -1;
        else if (state > 5) block = quest::bit(quest_bits, kQuest, 13) ? 6 : -1;
        else if (open && state >= 1) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_005bad60).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        if (npc == kOrmus) return state == 1;
        if (npc == kCain) return quest::bit(quest_bits, kQuest, 4) && !orb_broken;
        return false;
    }
    // No log function of its own (+0xe8 0): the records' usual one.
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 6); }
};

// The Guardian (a3q6.cpp, record 0x14, word 0x16): opened by the Blackened
// Temple's chain (+0xf0, LAB_005bcc60: state 1) or a level past 97 (the
// temples and the Durance; FUN_005bc3c0); Ormus (255) tells (628: state 3, 2
// with the game's bit 13; marks on the talk's close). Mephisto's death
// (World::mephisto_died: bits 13, 0, 0xb) and then any of the docks' NPCs'
// report (657..663: block 5, greeting) clears bit 0xb (state 7 when done
// here). A join with it done (bit 0, bit 0xb, or Act 4's word 0x17 bit 0)
// finds Mephisto dead (data+0xc 2: the hellgate open, the bridge up).
struct GuardianQuest {
    static constexpr int kQuest = 0x16, kOrmus = monster_ids::kOrmus, kTells = 628, kReportFirst = 657, kReportLast = 663;
    static constexpr int kDocks = 75, kDuranceOfHate1 = 100, kDuranceOfHate3 = 102;
    bool open = true;              // +9
    int state = 0;                 // +0xc
    int log = 0;                   // +0xb
    bool dead = false;             // data+0xc 2: Mephisto's dead
    bool rewarded = false;         // +0x1c

    // The player joins (LAB_005bcc80).
    void join(const QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 0xb) || quest::bit(quest_bits, 0x17, 0)) { quest::set(game_bits, kQuest, 13); dead = true; return; }
        static constexpr std::array<std::array<int, 3>, 8> kByBit{ { { 9, 5, 4 }, { 8, 5, 3 }, { 7, 5, 2 }, { 6, 4, 4 }, { 5, 4, 3 }, { 4, 4, 2 }, { 3, 2, 2 }, { 2, 3, 2 } } };
        for (const auto& [bit, stage, logged] : kByBit)
            if (quest::bit(quest_bits, kQuest, bit)) { state = stage; log = logged; return; }
    }
    // The Blackened Temple done (its +0xf0 chain).
    void chain() { if (state == 0 && open) state = 1; }
    // A level change (FUN_005bc3c0).
    void enter(const QuestBits& quest_bits, int from, int into, bool game_done) {
        const int later = game_done ? 4 : 5;
        if (from == kDocks && state >= 2 && state <= 3 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 0xb)) state = later;
        if (into > 97 && open && state == 0) state = 1;
        if (into == kDuranceOfHate1 && state == 1) state = later;
        if (into == kDuranceOfHate3 && state != 4 && state != 5) state = later;
    }
    // Mephisto died (World::mephisto_died).
    void killed() { dead = true; rewarded = true; }
    // The player heard `string` from `npc` (FUN_005bc5a0).
    void said(QuestBits& quest_bits, int npc, int string, bool game_done) {
        if (!quest::bit(quest_bits, kQuest, 0xb) && quest::bit(quest_bits, kQuest, 0)) return;
        if (npc == kOrmus && string == kTells) { state = game_done ? 2 : 3; return; }
        if (string < kReportFirst || string > kReportLast || !quest::bit(quest_bits, kQuest, 0xb)) return;
        if (quest::bit(quest_bits, kQuest, 13)) state = 7;
        quest::set(quest_bits, kQuest, 0xb, false);
    }
    // What `npc` says (FUN_005bc270; blocks 0x740ef8, state -> 0x741518).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[7] = {
        { { 255, 628, true } },
        { { 254, 629, false }, { 255, 631, false }, { 264, 633, false }, { 252, 635, false }, { 253, 637, false }, { 245, 639, false }, { 297, 641, false } },
        { { 254, 630, false }, { 255, 632, false }, { 264, 634, false }, { 253, 638, false }, { 245, 640, false }, { 297, 642, false } },
        { { 254, 643, false }, { 255, 645, false }, { 264, 647, false }, { 252, 649, false }, { 253, 651, false }, { 245, 653, false }, { 297, 655, false } },
        { { 254, 644, false }, { 255, 646, false }, { 264, 648, false }, { 252, 650, false }, { 253, 652, false }, { 245, 654, false }, { 297, 656, false } },
        { { 254, 657, true }, { 255, 658, true }, { 264, 659, true }, { 252, 660, true }, { 253, 661, true }, { 245, 662, true }, { 297, 663, true } },
        { { 254, 657, false }, { 255, 658, false }, { 264, 659, false }, { 252, 660, false }, { 253, 661, false }, { 245, 662, false }, { 297, 663, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 0xb)) block = 5;
        else if (quest::bit(quest_bits, kQuest, 0)) block = rewarded ? 6 : -1;
        else if (state > 5) block = quest::bit(quest_bits, kQuest, 13) ? 6 : -1;
        else if (state >= 1) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_005bbfe0): Ormus while given out, everyone with the report due.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (!quest::bit(quest_bits, kQuest, 0) && npc == kOrmus && state == 1) return true;
        return quest::bit(quest_bits, kQuest, 0xb);
    }
    // No log function of its own (+0xe8 0): the records' usual one.
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 6); }
};

}  // namespace d2d::rules
