// SPDX-License-Identifier: GPL-3.0-or-later
// Quests: the save's quest flags and the Den of Evil (game.exe 1.14d
// D2Game/Quests/a1q1.cpp, docs/research/re/quests.md).
#pragma once

#include "level_ids.hpp"
#include "monster_ids.hpp"
#include "object_ids.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::rules {

// A difficulty's quest flags (the d2s block's 96 bytes): quest q's bit b
// is bit q * 16 + b (FUN_0065c310 / 0065c360 / 0065c3a0).
using QuestBits = std::array<std::uint8_t, 96>;
// The original client's quest log has slots 0..40; its flag packet has
// all 48 quest words from the save. Act III already uses log slots 17..22.
inline constexpr std::size_t kQuestLogCount = 41;
inline constexpr std::size_t kQuestFlagCount = QuestBits{}.size() / 2;
using QuestLogStates = std::array<std::uint8_t, kQuestLogCount>;
using QuestGameFlags = std::array<std::uint16_t, kQuestFlagCount>;
// Quest word `quest`'s flag `bit`: tested, set or cleared.
namespace quest {
[[nodiscard]] inline bool bit(const QuestBits& quest_bits, int quest, int bit) {
    const int bit_index = quest * 16 + bit;
    return quest_bits[std::size_t(bit_index >> 3)] >> (bit_index & 7) & 1;
}
inline void set(QuestBits& quest_bits, int quest, int bit, bool set = true) {
    const int bit_index = quest * 16 + bit;
    const auto mask = std::uint8_t(1u << (bit_index & 7));
    auto& byte = quest_bits[std::size_t(bit_index >> 3)];
    byte = std::uint8_t(set ? byte | mask : byte & ~mask);
}
}  // namespace quest
// Akara's reset of the stat and skill points (the Den of Evil's reward):
// its flags are quest 41's (0 used, 1 open, 13 opened late).
inline constexpr int kRespecQuest = 41;

// A quest message an NPC has for the player (FUN_00543790): its string.tbl
// id; `greet` plays as the NPC is clicked (kind 0), else it's a talk topic
// (kind 2) under the quest's name.
struct QuestMsg { int string = 0; bool greet = false; };

// The quest log's state the server sends a player for a quest whose record
// has no log function of its own (+0xe8 0, FUN_00543f90): `done` is the
// record's +0xd, the state it's over from; 12 reads "another player's".
[[nodiscard]] inline int quest_log(const QuestBits& quest_bits, int quest, int state, int log, int done) {
    const bool b13 = quest::bit(quest_bits, quest, 13), b14 = quest::bit(quest_bits, quest, 14);
    if (state < done) return b14 || (quest == 4 && log == 6 && !b13) ? 12 : log;
    if (b13) return log;
    if (quest == 4 && b14) return state == 6 ? 12 : log;
    return quest == 10 ? (log != 4 ? 12 : 4) : 12;
}

// The Den of Evil, one game's (the quest record at FUN_00590720). Quest 1
// in the flags; bits: 0 done, 1 reward due, 2 given, 3 / 4 in the Den,
// 13 cleared in this game, 14 cleared by someone else, 15 closed.
// ponytail: one player — the party and late-joiner lists are left out.
struct DenQuest {
    static constexpr int kQuest = 1, kAkara = monster_ids::kAkara, kDen = level_ids::kDenOfEvil;
    int state = 1;      // +0xc: 1 not given, 2 given, 3 in the Den, 4 cleared, 5 rewarded
    int log = 0;        // +0xb
    bool active = true; // +9: off when the player joins with it done or closed (FUN_00546270)

    // A player's bits for the state (LAB_0058fc90).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins: the state from their flags (LAB_00590690).
    void join(const QuestBits& quest_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
        if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
    // The chain reached it (+0xf0, FUN_00590620). True: it passes on to
    // quest 2's (+0x10).
    [[nodiscard]] bool chain() const { return state == 5 || !active; }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // Into the Den (FUN_00590470, level 8).
    void enter_den(QuestBits& quest_bits) {
        const bool bumped = state == 1 || state == 2;
        if (bumped) state = 3;
        if (log < 2) log = 2;
        else if (!bumped) return;
        mark(quest_bits);
    }
    // What `npc` (MonStats hcIdx) says about it (FUN_0058ff90): the block
    // for the state (0x736cd0), or "successful" while the reward is due;
    // the blocks at 0x7366b0.
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { 148, 64, true } },
        { { 148, 65, false }, { 155, 70, false }, { 147, 69, false }, { 150, 66, false }, { 154, 67, false } },
        { { 150, 72, false }, { 155, 75, false }, { 154, 73, false }, { 148, 71, false }, { 147, 74, false } },
        { { 150, 77, false }, { 155, 80, false }, { 154, 78, false }, { 148, 76, true }, { 147, 79, false } },
        { { 155, 80, false }, { 154, 78, false }, { 147, 79, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
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
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc != kAkara || quest::bit(quest_bits, kQuest, 0)) return false;
        return (state == 1 && !quest::bit(quest_bits, kQuest, 1)) || quest::bit(quest_bits, kQuest, 1);
    }
    // A monster died; `remaining` are the Den's left (level +0x2cc - +0x2d0)
    // (FUN_00590260). 0: cleared — the reward's due (FUN_005455f0, bits 13
    // and 1) unless it's been had.
    enum class Kill { none, few, cleared };
    Kill killed(QuestBits& quest_bits, int remaining) {
        if (state >= 4) return Kill::none;        // its callback's gone once cleared
        if (remaining > 0) {
            if (remaining < 6) log = 4;            // "Monsters remaining: N"
            return remaining < 6 ? Kill::few : Kill::none;
        }
        state = 4;
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); }
        return Kill::cleared;
    }
    // The player heard `string` from `npc` (FUN_0058fdd0, packet 0x31).
    // True: Akara's reward — a skill point (stat 5 + 1), done; quest 41
    // (her respec) opens; bits 2..11 cleared (FUN_0065c3e0). The state
    // going to 5 runs the chain (+0xf0), then log 13 (FUN_0058fbe0).
    bool said(QuestBits& quest_bits, int npc, int string) {
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
};

// Sisters' Burial Grounds, one game's (a1q2.cpp, the record at
// FUN_00591210). Quest 2; bits: 0 done, 1 reward due, 2 given, 3 / 4 in
// the Burial Grounds, 13 took part in the kill, 14 killed by someone
// else, 15 closed.
// ponytail: one player — the rewarded list (rec+0x1c) is a bool, the
// party share of the kill (LAB_00590e70) is left out.
struct BurialQuest {
    static constexpr int kQuest = 2, kKashya = monster_ids::kKashya, kBurial = level_ids::kBurialGrounds, kBloodRaven = monster_ids::kBloodRaven;
    bool open = true;   // +9: closed in this game when the player's done it (FUN_00546270)
    int state = 0;      // +0xc: 0 closed, 1 given out, 2 Kashya spoke, 3 Burial Grounds, 4 Blood Raven dead, 5 done
    int log = 0;        // +0xb
    int log_in = 0;     // ticks to the kill's "return to Kashya" (timer 0xf, LAB_00590bf0), 0 not running
    bool pending = false;    // data+3: Kashya gave it, the log waits for the talk to close
    bool rewarded = false;   // in the +0x1c list: took the reward, still in town

    // A player's bits for the state (LAB_00590890).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins (LAB_00591180); a done or closed quest is shut for
    // the game (FUN_00546270 → FUN_00544410).
    void join(const QuestBits& quest_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
        if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
    // The chain reaches it (FUN_005910f0): the Den's done or closed. True:
    // it passes on to quest 4's (+0x10).
    bool chain() {
        if (state == 0 && open) state = 1;
        return state == 5 || !open;
    }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A tick (40 ms).
    void tick() {
        if (log_in > 0 && --log_in == 0) log = 3;
    }
    // What `npc` says about it (FUN_00590b10): blocks at 0x736ce8 by the
    // state (0x737180: -1, 0, 1, 2, 3, 4).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { kKashya, 81, true } },
        { { kKashya, 82, false }, { 155, 86, false }, { 154, 83, false }, { 148, 85, false }, { 147, 84, false } },
        { { kKashya, 87, false }, { 155, 91, false }, { 154, 89, false }, { 148, 88, false }, { 147, 90, false } },
        { { kKashya, 92, true }, { 155, 96, false }, { 154, 94, false }, { 148, 93, false }, { 147, 95, false } },
        { { 155, 96, false }, { kKashya, 92, false }, { 148, 93, false }, { 147, 95, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 1)) block = 3;
        else if (rewarded) block = 4;
        else if (state >= 1 && state <= 3 && !quest::bit(quest_bits, kQuest, 0)) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_00591080).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc != kKashya || quest::bit(quest_bits, kQuest, 0)) return false;
        return (state == 1 && !quest::bit(quest_bits, kQuest, 1)) || quest::bit(quest_bits, kQuest, 1);
    }
    // The player heard `string` from `npc` (FUN_00590980). True: Kashya's
    // reward — done, and a free hireling (FUN_00579180). The state going
    // to 5 runs the chain (+0xf0).
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kKashya) return false;
        if (string == 81) { pending = true; state = 2; mark(quest_bits); return false; }
        if (string != 92 || !quest::bit(quest_bits, kQuest, 1)) return false;
        if (quest::bit(quest_bits, kQuest, 13) && state != 5) { log = 0xd; state = 5; }
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 1, false);
        rewarded = true;
        return true;
    }
    // The talk with `npc` closed (LAB_00590920).
    void talk_closed(QuestBits& quest_bits, int npc) {
        if (npc != kKashya || !pending) return;
        log = 1; pending = false;
        mark(quest_bits);
    }
    // The player went from level `from` to `dest` (FUN_00590fa0).
    void enter(QuestBits& quest_bits, int from, int dest) {
        if (dest == kBurial && open) {
            const bool bumped = state < 3;
            if (bumped) state = 3;
            if (log <= 1) { log = 2; mark(quest_bits); return; }
            if (bumped) { mark(quest_bits); return; }
        }
        if (from != 1) return;
        rewarded = false;                         // FUN_00545310
        if (state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
    }
    // Blood Raven died (FUN_00590ec0). True: the player's kill for it
    // (FUN_00590c40, bits 13 and 1) — the class's act1_complete_burial
    // (event 0x22). `near`: the player's room is the killer's or next to it.
    bool killed(QuestBits& quest_bits, bool nearby) {
        if (!open) return false;
        state = 4;
        const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (first && nearby) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); }
        else if (first) quest::set(quest_bits, kQuest, 14);             // LAB_00590dd0
        log_in = 15;
        pending = false;                          // +0xa8 = 0
        return first && nearby;
    }
};

// Sisters to the Slaughter, one game's (a1q6.cpp, the record at
// FUN_00596990). Quest 6; bits: 0 done, 1 reward due, 2 given, 3 / 4 in
// the Catacombs, 13 took part, 14 cleared by someone else, 15 closed.
// ponytail: one player — the Cain / Akara / Kashya lists (+0x000 / +0x084
// / +0x108) and the rewarded list (rec+0x1c) are a bool each for them.
struct AndyQuest {
    static constexpr int kQuest = 6, kCain = monster_ids::kCampCain, kAkara = monster_ids::kAkara, kKashya = monster_ids::kKashya,
                         kWarriv = monster_ids::kWarriv, kAndariel = monster_ids::kAndariel;
    int state = 0;      // +0xc: 0 init, 1 available, 2 Cain gave it, 3 Catacombs, 4 Andariel dead, 5 done
    int log = 0;        // +0xb
    int start_in = 0;   // ticks to the start timer (0x14, then 0x596580), 0 not running
    bool active = true; // +9: off when the player joins with it done or closed (FUN_00546270)
    int after_kill = 0; // +0x192: ticks since Andariel died (timer 0x596500), 0 not running
    bool cain = false, akara = false, kashya = false, rewarded = false;
    bool cain_pending = false;   // +0x195: Cain gave it, the log waits for the talk to close

    // A player's bits for the state (0x595bd0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins (0x596900).
    void join(const QuestBits& quest_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
        if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
    // The chain reached it (+0xf0, FUN_005968e0): the start timer runs. It
    // passes on no further (+0x10 is 0x25, the next act's).
    void chain() { if (state == 0 && active) start_in = 20; }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A tick (40 ms). 1: the kill's town portal is due at the player, if
    // they're in Andariel's lair (tick 10, FUN_00596490).
    int tick() {
        if (start_in > 0 && --start_in == 0 && state == 0) state = 1;
        if (after_kill == 0) return 0;
        ++after_kill;
        if (after_kill == 12) {
            if (log != 3 && log != 0xd) log = 3;
            after_kill = 0;
        }
        return after_kill == 10;
    }
    // What `npc` says about it (FUN_00595e20): blocks at 0x7382e0 by the
    // state (0x7382c4: -1, 0, 1, 2, 3, 4).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { kCain, 166, true } },
        { { kAkara, 168, false }, { kKashya, 172, false }, { 154, 169, false }, { kCain, 167, false }, { 147, 170, false }, { kWarriv, 171, false } },
        { { kKashya, 178, false }, { kWarriv, 177, false }, { 147, 175, false }, { kCain, 173, false }, { 154, 176, false }, { kAkara, 174, false } },
        { { kKashya, 181, true }, { kCain, 184, true }, { 154, 180, false }, { 147, 182, false }, { kWarriv, 183, true }, { kAkara, 179, true } },
        { { kKashya, 181, false }, { kCain, 184, false }, { 154, 180, false }, { 147, 182, false }, { kWarriv, 183, false }, { kAkara, 179, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        const bool bit0 = quest::bit(quest_bits, kQuest, 0), b13 = quest::bit(quest_bits, kQuest, 13);
        const bool listed = (npc == kCain && cain) || (npc == kAkara && akara) || (npc == kKashya && kashya);
        const bool cak = npc == kCain || npc == kAkara || npc == kKashya;
        int block = -1;
        if (listed) block = 3;
        else if (quest::bit(quest_bits, kQuest, 1)) block = cak ? 4 : 3;
        else if (rewarded) block = 4;
        else if (state == 1 && npc == kCain && !bit0) block = 0;
        else if (state != 0 && !(bit0 && !b13) && !(state >= 4 && !b13) && !(bit0 && b13)) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block <= 4)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_005967f0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1);
        if (npc == kCain) return cain || (!bit0 && state == 1 && !bit1);
        if (npc == kWarriv) return !bit0 && bit1;
        return (npc == kAkara && akara) || (npc == kKashya && kashya);
    }
    // The player heard `string` from `npc` (FUN_00595c60). True: Warriv's
    // reward — done (bit 0), and the quest's complete message.
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc == kCain && string == 166) { state = 2; cain_pending = true; mark(quest_bits); }
        if (npc == kCain && string == 184) cain = false;
        if (npc == kAkara && string == 179) akara = false;
        if (npc == kKashya && string == 181) kashya = false;
        if (npc != kWarriv || string != 183 || !quest::bit(quest_bits, kQuest, 1)) return false;
        if (quest::bit(quest_bits, kQuest, 13)) { log = 0xd; state = 5; }
        quest::set(quest_bits, kQuest, 1, false);
        quest::set(quest_bits, kQuest, 0);
        rewarded = true;
        return true;
    }
    // The talk with `npc` closed (0x595b80).
    void talk_closed(int npc) {
        if (npc == kCain && cain_pending) { log = 1; cain_pending = false; }
    }
    // The player went from level `from` to `dest` (FUN_00596010).
    void enter(QuestBits& quest_bits, int from, int dest) {
        if (dest < 34 || dest > 37) {
            if (state == 4 && dest == level_ids::kLutGholein) state = 5;
            else if (from == 1 && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
            return;
        }
        if (state <= 2) state = 3;
        if (dest == level_ids::kCatacombsLevel4 && log < 2) log = 1;
        mark(quest_bits);
    }
    // Andariel died (FUN_005965a0). True: the player's kill for the quest
    // (bits 13 and 1, FUN_00596210) — Act 2 opens and her quest drop is
    // due: two chipped gems, one standard. `in_lair`: the player's in her
    // level (FUN_00596260: the lists).
    // ponytail: a merc's kill counts as the player's.
    bool killed(QuestBits& quest_bits, bool in_lair) {
        const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (first) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); }
        if (in_lair && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15)) cain = akara = kashya = true;
        after_kill = 1;
        state = 4;
        return first;
    }
    // Her quest drop's codes (0x7361dc chipped, 0x736444 standard), by lo % 7.
    static constexpr const char* kChipped[7] = { "gcv", "gcr", "gcb", "gcy", "gcg", "gcw", "skc" };
    static constexpr const char* kStandard[7] = { "gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku" };
};

// The Search for Cain, one game's (a1q4.cpp, the record at FUN_005971b0).
// Lut Gholein's palace (a2q4.cpp): two invisible objects in the town's DS1
// place Jerhyn and the gate guard as their rooms come up. Object 121
// (InitFn 18, FUN_0059f380) makes Jerhyn there when quest 8 and 12 lack
// bit 13 and the Seven Tombs record (id 13, quest 14) is open with state
// under 2. Object 122 (InitFn 19, FUN_0059f440) makes the guard (331) one
// subtile east unless quest 14 has bit 13, and Jerhyn in the palace
// (FUN_0059ef70: x - 10 with that record open and under 2, else x + 15;
// y - 3) unless quests 8 and 9 lack bit 13 and the record holds.
// Quests 8 and 9 are the game's flags (`game_bits`, game +0x10f4 +0xc).
// ponytail: quests 12 and 14 are the player's (one player; their records
// aren't ported), and the Seven Tombs record's state is "under 2" while
// quest 14 has none of bits 1..11.
struct PalaceSpots {
    bool outside = false, palace = false, guard = false;
    bool palace_west = false;          // the palace spot is x - 10 (else x + 15)
};
[[nodiscard]] inline PalaceSpots palace_spots(const QuestBits& quest_bits, const QuestBits& game_bits) {
    bool tombs_early = !quest::bit(quest_bits, 14, 0) && !quest::bit(quest_bits, 14, 15);
    for (int bit = 1; bit <= 11; ++bit) tombs_early = tombs_early && !quest::bit(quest_bits, 14, bit);
    PalaceSpots spots;
    spots.outside = !quest::bit(game_bits, 8, 13) && !quest::bit(quest_bits, 12, 13) && tombs_early;
    spots.guard = !quest::bit(quest_bits, 14, 13);
    spots.palace = !(!quest::bit(game_bits, 8, 13) && !quest::bit(game_bits, 9, 13) && tombs_early);
    spots.palace_west = tombs_early;
    return spots;
}

// Act 2's opening (a2q0.cpp, the record at FUN_00598810): quest 8. Jerhyn
// greets (253, FUN_005986b0) with his "!" (FUN_00598780) until the player
// has bit 0; hearing it sets that and the game's bit 13 (LAB_00598640), as
// a join with bit 0 does (LAB_005987d0).
// ponytail: Cain's 125 (quest 4 bit 14, the rescue missed; FUN_005940a0)
// isn't here.
struct ActTwoOpening {
    static constexpr int kQuest = 8, kJerhyn = monster_ids::kJerhyn, kGreeting = 253;
    static void join(const QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, kQuest, 0)) quest::set(game_bits, kQuest, 13);
    }
    [[nodiscard]] static std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) {
        if (npc == kJerhyn && !quest::bit(quest_bits, kQuest, 0)) return { { kGreeting, true } };
        return {};
    }
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) { return npc == kJerhyn && !quest::bit(quest_bits, kQuest, 0); }
    static void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string) {
        if (npc != kJerhyn || string != kGreeting) return;
        quest::set(quest_bits, kQuest, 0);
        quest::set(game_bits, kQuest, 13);
    }
};

// Radament's Lair (a2q1.cpp, the record at FUN_00599340): quest 9, given
// out from the start (state 1). Bits: 0 done, 1 reward due, 2 Atma told,
// 3 / 4 under way, 5 the Book of Skill due (cleared reading it), 13 took
// part, 14 killed by someone else, 15 closed.
// ponytail: one player — the rewarded list (rec+0x1c) is a bool, the party's
// bits (FUN_00598d40) and the chain on to the Seven Tombs (+0x10 13, its
// record not ported) are left out.
struct RadamentQuest {
    static constexpr int kQuest = 9, kAtma = monster_ids::kAtma, kLair = level_ids::kSewersLevel3, kRadament = monster_ids::kRadament;
    static constexpr int kInit = 304, kSuccess = 334;
    bool open = true;   // +9
    int state = 1;      // +0xc: 1 given out, 2 Atma spoke, 3 out of town, 4 Radament dead, 5 done
    int log = 0;        // +0xb
    int log_in = 0;     // ticks to the kill's "return to Atma" (timer 0xc, LAB_00598f70)
    bool pending = false;          // data+8: Atma spoke, the log waits for the talk to close
    bool rewarded = false;         // in the +0x1c list
    bool reward_closed = false;    // data+0x10: joined closed with the reward due

    // A player's bits for the state (LAB_005989e0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins (FUN_00599230). Done with the book due but gone: the
    // quest's bits go, to be done again in a later game.
    void join(QuestBits& quest_bits, QuestBits& game_bits, bool has_book) {
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
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    void tick() {
        if (log_in > 0 && --log_in == 0 && state == 4) log = 3;
    }
    // What `npc` says (FUN_00598c40): blocks at 0x739218 by the state
    // (0x7398fc: -1, 0, 1, 2, 3, 4); kind 2 a topic, else greets.
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { kAtma, 304, true } },
        { { kAtma, 310, false }, { 175, 307, false }, { 198, 305, false }, { 199, 306, false }, { 177, 312, false }, { 202, 313, false },
          { 244, 314, false }, { 210, 311, false }, { 200, 308, false }, { 178, 309, false } },
        { { kAtma, 317, false }, { 175, 315, false }, { 198, 318, false }, { 199, 320, false }, { 177, 322, false }, { 202, 321, false },
          { 244, 324, false }, { 210, 316, false }, { 200, 319, false }, { 178, 323, false } },
        { { kAtma, 334, true }, { 198, 325, false }, { 199, 330, false }, { 177, 326, false }, { 202, 327, false }, { 244, 333, false },
          { 210, 328, false }, { 200, 329, false }, { 178, 332, false } },
        { { 175, 331, false }, { kAtma, 334, false }, { 198, 325, false }, { 199, 330, false }, { 177, 326, false }, { 244, 333, false },
          { 210, 328, false }, { 200, 329, false }, { 178, 332, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 1)) block = 3;
        else if (rewarded) block = 4;
        else if (!quest::bit(quest_bits, kQuest, 0) && state != 0 && (state < 4 || quest::bit(quest_bits, kQuest, 13))) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block <= 4)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // Atma's "!" (FUN_00598910).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        if (npc != kAtma || quest::bit(quest_bits, kQuest, 0)) return false;
        return (open && state == 1 && !quest::bit(quest_bits, kQuest, 15)) || quest::bit(quest_bits, kQuest, 1);
    }
    // The player heard `string` from `npc` (FUN_00598a70). Atma's thanks with
    // the reward due: done (the book was the reward).
    void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string) {
        if (npc != kAtma) return;
        if (string == kInit) { pending = true; state = 2; mark(quest_bits); return; }
        if (string != kSuccess || !quest::bit(quest_bits, kQuest, 1)) return;
        if (quest::bit(quest_bits, kQuest, 13)) {
            if (state != 5) { log = 0xd; pending = false; state = 5; }
            if (!open) quest::set(game_bits, kQuest, 13);
        }
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 1, false);
        rewarded = true;
    }
    // The talk with `npc` closed (LAB_00598990).
    void talk_closed(QuestBits& quest_bits, int npc) {
        if (npc != kAtma || !pending) return;
        log = 1; pending = false;
        mark(quest_bits);
    }
    // The player went from level `from` (LAB_00599130): out of town.
    void enter(QuestBits& quest_bits, int from) {
        if (from != level_ids::kLutGholein) return;
        rewarded = false;                         // FUN_00545310
        if (state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
    }
    // Radament died (FUN_00599020). The books to drop at him (one each for a
    // player due one without one, LAB_00598fa0); `took_part` (FUN_00598e20:
    // the player's room is the killer's or next to it) also plays the
    // class's act2_complete_radament (voice 0x32).
    struct Kill { int books = 0; bool took_part = false; };
    Kill killed(QuestBits& quest_bits, QuestBits& game_bits, bool nearby, bool has_book) {
        if (!open) return {};
        state = 4;
        quest::set(game_bits, kQuest, 13);
        pending = false;                          // +0xa8 = 0
        const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (first && nearby) for (const int bit : { 13, 1, 5 }) quest::set(quest_bits, kQuest, bit);
        else if (first) quest::set(quest_bits, kQuest, 14);   // LAB_00598dc0
        if (log_in == 0) log_in = 12;
        return { quest::bit(quest_bits, kQuest, 5) && !has_book ? 1 : 0, quest::bit(quest_bits, kQuest, 13) };
    }
    // The Book of Skill read (0x55cd31 / 0x55e3d0): only while bit 5 is set;
    // it clears, and a skill point (stat 5).
    static bool read_book(QuestBits& quest_bits) {
        if (!quest::bit(quest_bits, kQuest, 5)) return false;
        quest::set(quest_bits, kQuest, 5, false);
        return true;
    }
};

// The Horadric Staff (a2q2.cpp, the record at FUN_0059eb00): quest 10, all
// Cain's (Act 2's, 244) and the items the player carries: the scroll
// (tr1), the shaft (msf), the Cube (box), the amulet (vip), the staff (hst).
// Bits: 0 done (the staff in the orifice: the Seven Tombs'), 1 reward due,
// 3 Cain's read the scroll (or seen any part), 4 / 5 / 6 the amulet /
// shaft / Cube shown, 10 the staff shown, 13 / 14 done here / by others,
// 15 closed. Its log is its own (+0xe8, FUN_0059e630).
// N-player gap: one player — the rewarded list is a bool and the counts of
// players carrying each part (data+0x18..0x24) are what this one carries.
struct HoradricStaffQuest {
    static constexpr int kQuest = 10, kCain = monster_ids::kCainAct2;
    static constexpr int kScrollChest = 40, kShaftChest = 41, kCubeChest = 39;   // OperateFns (objects.txt 355, 356, 354)
    struct Items { bool scroll = false, shaft = false, cube = false, amulet = false, staff = false; };
    bool open = true;              // +9
    bool rewarded = false;         // in the +0x1c list
    bool shaft_dropped = false;    // data+0x28: the shaft chest gave one
    bool cube_dropped = false;     // data+0x29: the Cube chest gave one
    bool staff_made = false;       // data+0x2a: transmuted (FUN_0059e5c0; the player's bit 11)

    // The player joins (FUN_0059e850): parts no longer carried are no
    // longer shown, unless all three are, the staff's made or placed.
    void join(QuestBits& quest_bits, const Items& items, bool staff_placed) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) open = false;
        if (quest::bit(quest_bits, kQuest, 0) || staff_placed || items.staff || (items.shaft && items.cube && items.amulet)) return;
        if (!items.shaft) quest::set(quest_bits, kQuest, 5, false);
        if (!items.cube) quest::set(quest_bits, kQuest, 6, false);
        if (!items.amulet) quest::set(quest_bits, kQuest, 4, false);
    }
    // What Cain has to say of what's carried (FUN_00599590): a part not
    // shown yet is news (blocks 0..4), else the last one shown (5..9).
    struct Pick { bool news = false; int block = -1; };
    [[nodiscard]] static Pick pick(const QuestBits& quest_bits, const Items& items) {
        if (items.staff) return quest::bit(quest_bits, kQuest, 10) ? Pick{ false, 5 } : Pick{ true, 4 };
        Pick out;
        if (items.cube) { if (!quest::bit(quest_bits, kQuest, 6)) return { true, 3 }; out.block = 9; }
        if (items.scroll) { if (!quest::bit(quest_bits, kQuest, 3)) return { true, 0 }; out.block = 6; }
        if (items.amulet) { if (!quest::bit(quest_bits, kQuest, 4)) return { true, 1 }; out.block = 7; }
        if (items.shaft) { if (!quest::bit(quest_bits, kQuest, 5)) return { true, 2 }; out.block = 8; }
        return out;
    }
    // Cain's lines (FUN_00599910): blocks at 0x739918, one each; 0..4
    // greet (kind 0), 5..9 are topics.
    static constexpr std::array<int, 10> kLines{ 335, 336, 337, 338, 339, 339, 335, 336, 337, 338 };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, const Items& items) const {
        if (npc != kCain) return {};
        int block = 4;
        if (!quest::bit(quest_bits, kQuest, 1)) {
            const auto found = pick(quest_bits, items);
            if (found.news && found.block >= 0) block = found.block;
            else if (rewarded) block = 5;
            else if (quest::bit(quest_bits, kQuest, 0)) return {};
            else if (found.block < 0) { if (!quest::bit(quest_bits, kQuest, 3)) return {}; block = 6; }
            else block = found.block;
        }
        return { { kLines[std::size_t(block)], block < 5 } };
    }
    // Cain's "!" (FUN_005996c0): the reward due, or a part to show.
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc, const Items& items) {
        return npc == kCain && (quest::bit(quest_bits, kQuest, 1) || pick(quest_bits, items).news);
    }
    // The player heard `string` from Cain (FUN_005997f0). True: he read the
    // scroll, which goes (FUN_00544160 -> FUN_005440a0).
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kCain) return false;
        switch (string) {
        case 335: quest::set(quest_bits, kQuest, 3); return true;
        case 336: quest::set(quest_bits, kQuest, 4); break;
        case 337: quest::set(quest_bits, kQuest, 5); break;
        case 338: quest::set(quest_bits, kQuest, 6); break;
        case 339:
            quest::set(quest_bits, kQuest, 1, false);
            rewarded = true;
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
    [[nodiscard]] int log_state(const QuestBits& quest_bits, const Items& items, bool altar_used) const {
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
    [[nodiscard]] std::string chest(int operate_fn, const QuestBits& quest_bits, const Items& items) {
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
};

// The Arcane Sanctuary (a2q4.cpp, the record at FUN_0059f2a0): quest 12.
// Open once the Tainted Sun's done (the chain) or the staff's made
// (FUN_0059b660); Drognan (373) sends the player to Jerhyn, Jerhyn (377)
// to the palace; the guard (331) stands aside then; Horazon's Journal in
// the Sanctuary (OperateFn 42) finishes it and its 396 opens the way to the
// Canyon of the Magi. Bits: 0 done, 1 reward due, 2..5 under way (the
// mark), 7 / 8 the guard's two lines heard, 13 took part, 14 by others,
// 15 closed.
// N-player gaps: one player (the rewarded bool, no party credit); the
// palace door's mode (data+0x38) and Jerhyn's moves are show_palace's.
struct ArcaneQuest {
    static constexpr int kQuest = 12, kDrognan = monster_ids::kDrognan, kJerhyn = monster_ids::kJerhyn, kGuard = monster_ids::kPalaceGuard;
    static constexpr int kSanctuary = level_ids::kArcaneSanctuary, kHarem = 50, kCanyon = level_ids::kCanyonOfTheMagi;
    static constexpr int kToJerhyn = 373, kToPalace = 377, kJournal = 396, kThanksFirst = 397, kThanksLast = 407;
    bool open = true;          // +9
    int state = 0;             // +0xc: 1 open, 2 Drognan told, 3 Jerhyn told, 4 in the Sanctuary, 5 the journal read
    int log = 0;               // +0xb
    bool guard_in = false;     // data+0xe: the guard lets the player by
    int guard_state = 0;       // data+0x40: 2 once he has
    bool guard_new = false;    // data+0x46: his second line is news
    bool pending = false;      // data+4: Drognan spoke, the log waits for the talk to close
    int journal_room = -1;     // data+8: the journal's room (the Sanctuary's)
    bool portal_made = false;  // data+0x16
    bool rewarded = false;

    // A player's bits for the state (LAB_0059adb0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
        if (state == 4 && (log == 2 || log == 3)) quest::set(quest_bits, kQuest, 4);
        if (state == 4 && log == 4) quest::set(quest_bits, kQuest, 5);
    }
    // The guard stands aside (FUN_0059aef0).
    void let_in(QuestBits& quest_bits) {
        guard_in = true;
        if (guard_state == 0) guard_new = true;
        guard_state = 2;
        mark(quest_bits);
    }
    // The chain reached it (LAB_0059b500): open; it passes on (+0x10 13).
    bool chain() { if (state == 0 && open) state = 1; return true; }
    // The Horadric Staff made (FUN_0059b660).
    void staff_made(QuestBits& quest_bits) {
        if (!open) { let_in(quest_bits); return; }
        if (state == 0) state = 1;
        if (log == 0) log = 1;
    }
    // The player joins (FUN_0059b530).
    void join(const QuestBits& quest_bits, QuestBits& game_bits, bool has_staff) {
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
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 5); }
    // What `npc` says (FUN_0059b1c0): blocks at 0x73a638 by the state
    // (0x738d44: -1, 0, 1, 2, 3, 4); the guard: 186 till he lets the player
    // by, then one of 187..189 on the quest seed.
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[6] = {
        { { kDrognan, 373, true } },
        { { kDrognan, 373, false }, { kJerhyn, 377, true } },
        { { 176, 380, false }, { 175, 381, false }, { 198, 375, false }, { 199, 376, false }, { kDrognan, 383, false }, { 202, 382, false },
          { 244, 378, false }, { kJerhyn, 377, false }, { 210, 384, false }, { 200, 379, false }, { 178, 374, false } },
        { { 176, 393, false }, { 175, 394, false }, { 198, 387, false }, { 199, 385, false }, { kDrognan, 388, false }, { kJerhyn, 386, false },
          { 202, 389, false }, { 244, 395, false }, { 210, 392, false }, { 200, 391, false }, { 178, 390, false } },
        { { 176, 406, true }, { 175, 403, true }, { 198, 397, true }, { 199, 400, true }, { kJerhyn, 398, true }, { kDrognan, 399, true },
          { 202, 405, true }, { 244, 407, true }, { 210, 402, true }, { 200, 401, true }, { 178, 404, true } },
        { { 175, 403, false }, { 198, 397, false }, { 199, 400, false }, { kJerhyn, 398, false }, { kDrognan, 399, false }, { 202, 405, false },
          { 244, 407, false }, { 210, 402, false }, { 200, 401, false }, { 178, 404, false } },
    };
    // `roll`: the quest seed's next step, drawn only for the guard's line.
    template <class Roll>
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, Roll&& roll) const {
        if (npc == kGuard) return { { guard_in ? 187 + int(roll() % 3) : 186, true } };
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 1)) block = 4;
        else if (rewarded) block = 5;
        else if (state != 0 && open && !quest::bit(quest_bits, kQuest, 0) && !(state > 4 && !quest::bit(quest_bits, kQuest, 13))) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block < 6)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_0059aca0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
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
    bool said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player_room) {
        if (npc == kGuard) {
            if (string == 186) quest::set(quest_bits, kQuest, 7);
            else if (string >= 187 && string <= 189) quest::set(quest_bits, kQuest, 8);
            return false;
        }
        if (string >= kThanksFirst && string <= kThanksLast && quest::bit(quest_bits, kQuest, 1)) {
            quest::set(quest_bits, kQuest, 1, false);
            rewarded = true;
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
    void talk_closed(QuestBits& quest_bits, int npc) {
        if (npc != kDrognan || !pending) return;
        log = 2; pending = false;
        mark(quest_bits);
    }
    // The player went from `from` to `dest` (FUN_0059f0c0).
    void enter(QuestBits& quest_bits, int from, int dest) {
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
            rewarded = false;                     // FUN_00545310
            if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && state == 3) { state = 4; mark(quest_bits); }
        }
    }
    // Horazon's Journal read (OperateFn 42, FUN_0059b970): its 396 to the
    // player (FUN_005456a0), its room kept; open and not done: state 5, and
    // a player in the Sanctuary without bit 0 / 1 gets 13, 1, 0 (their
    // 2..11 cleared, then 8, 7: FUN_0059b3f0); else bit 14 (LAB_0059b320).
    void journal(QuestBits& quest_bits, int room, bool in_sanctuary) {
        journal_room = room;
        if (!open || state == 5) return;
        state = 5;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (in_sanctuary) {
            for (const int bit : { 13, 1, 0 }) quest::set(quest_bits, kQuest, bit);
            for (int bit = 2; bit < 12; ++bit) quest::set(quest_bits, kQuest, bit, false);
            quest::set(quest_bits, kQuest, 8);
            quest::set(quest_bits, kQuest, 7);
        } else quest::set(quest_bits, kQuest, 14);
    }
};

// The Summoner (a2q5.cpp, the record at FUN_0059c280): quest 13. Opened
// when he first goes for a player (FUN_005f85c0 -> FUN_0059c330); his
// death credits the players by him; the town's thanks finish it. Bits: 0
// done, 1 reward due, 2 under way, 13 took part, 14 by others, 15 closed.
// N-player gaps: one player (the rewarded bool, no party credit).
struct SummonerQuest {
    static constexpr int kQuest = 13, kSummoner = monster_ids::kSummoner, kSanctuary = level_ids::kArcaneSanctuary;
    static constexpr int kThanksFirst = 419, kThanksLast = 429;
    bool open = true;
    int state = 0;             // +0xc: 1 under way, 2 he's dead, 3 done
    int log = 0;
    bool started = false;      // data+1
    int log_in = 0;            // data+8: the death's 3-tick timer (LAB_0059bfd0)
    bool rewarded = false;
    void mark(QuestBits& quest_bits) const {   // LAB_0059bd10
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && state == 1) quest::set(quest_bits, kQuest, 2);
    }
    void engaged(QuestBits& quest_bits) {      // FUN_0059c330
        if (!open) return;
        started = true;
        if (state == 0) state = 1;
        if (log < 2) { log = 2; mark(quest_bits); }
    }
    void join(const QuestBits& quest_bits) {   // LAB_0059c220
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
        if (quest::bit(quest_bits, kQuest, 2)) { log = 2; state = 1; }
    }
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 2); }
    // He died (FUN_0059c150). True: the player took part (FUN_0059be70:
    // their room is his or next to it): bits 13 and 1; else bit 14
    // (LAB_0059c0f0).
    bool killed(QuestBits& quest_bits, bool nearby) {
        if (!open) return false;
        state = 2;
        if (log_in == 0) log_in = 3;
        const bool first = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (first && nearby) { quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1); return true; }
        if (first) quest::set(quest_bits, kQuest, 14);
        return false;
    }
    // A tick. True: the timer's end, when a player with bit 13 in the
    // Sanctuary says act2_complete_summoner (voice 0x33, LAB_0059bf80).
    bool tick() {
        if (log_in == 0 || --log_in > 0) return false;
        if (open) log = 4;
        return true;
    }
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[3] = {
        { { 176, 414, false }, { 175, 413, false }, { 198, 408, false }, { 199, 417, false }, { 177, 410, false }, { 202, 411, false },
          { 244, 418, false }, { 210, 412, false }, { 201, 409, false }, { 200, 415, false }, { 178, 416, false } },
        { { 176, 427, true }, { 175, 424, true }, { 198, 419, true }, { 199, 423, true }, { 177, 422, true }, { 202, 426, true },
          { 244, 429, true }, { 210, 425, true }, { 201, 421, true }, { 200, 420, true }, { 178, 428, true } },
        { { 176, 427, false }, { 175, 424, false }, { 198, 419, false }, { 199, 423, false }, { 177, 422, false }, { 202, 426, false },
          { 244, 429, false }, { 210, 425, false }, { 201, 421, false }, { 200, 420, false }, { 178, 428, false } },
    };
    // FUN_0059bbc0: the reward due → block 1; rewarded → 2; else by the
    // state (0x73b278: -1, 0, 1, 2).
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        const bool b13 = quest::bit(quest_bits, kQuest, 13);
        if (quest::bit(quest_bits, kQuest, 1)) block = 1;
        else if (rewarded) block = 2;
        else if (open && state != 0 && !(quest::bit(quest_bits, kQuest, 0) && !b13) && !(state > 1 && !b13)) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block < 3)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) {   // FUN_0059bb40
        static constexpr std::array<int, 10> kTown{ 176, 175, 199, 177, 202, 244, 210, 201, 200, 178 };
        return std::ranges::find(kTown, npc) != kTown.end() && quest::bit(quest_bits, kQuest, 1);
    }
    void said(QuestBits& quest_bits, QuestBits& game_bits, int string) {     // FUN_0059bd70
        if (string < kThanksFirst || string > kThanksLast || !quest::bit(quest_bits, kQuest, 1)) return;
        if (quest::bit(quest_bits, kQuest, 13)) { log = 0xd; quest::set(game_bits, kQuest, 13); state = 3; }
        rewarded = true;
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 1, false);
    }
    void enter(int from, int dest) { if (open && dest >= level_ids::kLutGholein && from == level_ids::kLutGholein) rewarded = false; }   // LAB_0059c200
};

// The Seven Tombs (a2q6.cpp, the record at FUN_0059d5b0): quest 14. Open
// on the chain (Radament's done, the Arcane Sanctuary opened); the true
// tomb (the act's boss tomb, act +0x94) and the Canyon move it on; the
// staff in the orifice (OperateFn 25) finishes the Horadric Staff and
// opens the way down to Duriel (object 100); his death, Tyrael's 302 (a
// town portal; Act 3 opened), Jerhyn's 442, the town's lines (444..452:
// bits 6..11), Meshif's 450: done. Bits: 0 done, 2 under way, 3 Tyrael's
// heard, 4 Jerhyn's reward heard, 5 at Duriel's death, 6..11 the town's
// lines heard, 13 took part, 14 by others, 15 closed. Its log is its own
// (+0xe8, FUN_0059ca50).
// N-player gaps: one player (the rewarded bool, no party credit); the
// orifice's panel (the staff dragged in: C->S 0x44) is skipped — using it
// with the staff places it; the log override (data+0x10 / +0x14) isn't
// traced; Tyrael's prison (data+0x2c) isn't opened.
struct SevenTombsQuest {
    static constexpr int kQuest = 14, kJerhyn = monster_ids::kJerhyn, kTyrael = monster_ids::kTyrael, kMeshif = monster_ids::kMeshif;
    static constexpr int kLair = level_ids::kDurielsLair, kCanyon = level_ids::kCanyonOfTheMagi, kDuriel = monster_ids::kDuriel;
    static constexpr int kStart = 430, kReward = 442, kTyraelLine = 302, kMeshifLine = 450;
    bool open = true;
    int state = 0;             // +0xc: 1 open, 2 the true tomb / the Canyon, 3 Duriel dead, 4 Tyrael spoke, 5 rewarded
    int log = 0;
    int true_tomb = 0;         // data+0x34
    bool duriel_dead = false;  // data+1
    bool tyrael_talks = false; // data+0x18 2
    bool tyrael_portal = false;   // data+0xf
    bool tyrael_gone = false;     // Tyrael has left the Lair (his NpcStationary think, FUN_005e73a0)
    bool jerhyn_pending = false, reward_pending = false, tyrael_pending = false;   // data+8, +10, +9
    bool staff_placed = false; // data+0xd: placed, the way down not open yet
    bool lair_open = false;    // data+0xb
    bool later = false;        // data+0xc: placed in an earlier game
    bool timer_started = false;   // data+3
    int open_in = -1;          // the orifice's timer (FUN_0059d870)
    int log_in = 0;            // Duriel's death's 8-tick timer (LAB_0059cee0)
    bool rewarded = false;
    void mark(QuestBits& quest_bits) const {    // FUN_0059c6c0
        if (!quest::bit(quest_bits, kQuest, 0) && state > 1) quest::set(quest_bits, kQuest, 2);
    }
    bool chain() { if (state == 0 && open) state = 1; return true; }   // FUN_0059d450, on to +0x10's
    void join(QuestBits& quest_bits) {           // LAB_0059d4f0
        quest::set(quest_bits, 10, 9, false);
        if (quest::bit(quest_bits, 10, 0)) { later = true; staff_placed = true; }
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
        if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
        else if (quest::bit(quest_bits, kQuest, 3)) { log = 5; state = 5; }
        else if (quest::bit(quest_bits, kQuest, 4)) { log = 6; state = 5; }
    }
    // The log (+0xe8, FUN_0059ca50).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const {
        if (!quest::bit(quest_bits, 7, 0) || quest::bit(quest_bits, kQuest, 0)) return 0;
        if (quest::bit(quest_bits, kQuest, 3)) return 5;
        if (quest::bit(quest_bits, kQuest, 4)) return 6;
        if (!open) return 0;
        if (quest::bit(quest_bits, kQuest, 5) || state < 3) return log;
        return 0xc;
    }
    // The player went from `from` to `dest` (FUN_0059d1c0): levels 40..74.
    void enter(QuestBits& quest_bits, int from, int dest) {
        if (dest < level_ids::kLutGholein || dest > level_ids::kArcaneSanctuary) return;
        if (from == level_ids::kLutGholein) rewarded = false;
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
    // FUN_0059c3c0.
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[7] = {
        { { kJerhyn, 430, true } },
        { { 176, 434, false }, { 175, 433, false }, { 198, 441, false }, { 199, 432, false }, { kJerhyn, 431, false }, { 177, 439, false },
          { 202, 438, false }, { 244, 440, false }, { kMeshif, 436, false }, { 200, 435, false }, { 178, 437, false } },
        { { kTyrael, 302, true } },
        { { 176, 445, false }, { 175, 446, false }, { 198, 451, false }, { 199, 443, false }, { 177, 449, false }, { kJerhyn, 442, true },
          { kTyrael, 302, true }, { 202, 444, false }, { 244, 452, false }, { kMeshif, 450, false }, { 200, 448, false }, { 178, 447, false } },
        { { 176, 445, false }, { 175, 446, false }, { 198, 451, false }, { 199, 443, false }, { 177, 449, false }, { kJerhyn, 442, false },
          { kTyrael, 302, true }, { 202, 444, false }, { 244, 452, false }, { 200, 448, false }, { 178, 447, false } },
        { { kMeshif, 450, true } },
        { { 176, 445, true }, { 175, 446, true }, { 177, 449, true }, { 202, 444, true }, { 244, 452, true }, { 178, 447, true } },
    };
    // The town's first lines after Tyrael, by the bit each sets.
    static constexpr std::array<std::pair<int, int>, 6> kTownLines{ { { 176, 6 }, { 175, 7 }, { 177, 8 }, { 202, 9 }, { 244, 10 }, { 178, 11 } } };
    // `summoner_done`: the game's quest 13 bit 13 (Drognan stays quiet in block 1).
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool summoner_done) const {
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
        if (rewarded) return lines(4);
        if (state == 0) return {};
        const bool b13 = quest::bit(quest_bits, kQuest, 13);
        if ((quest::bit(quest_bits, kQuest, 0) && !b13) || (state > 3 && !b13)) return {};
        const int block = state - 1;
        if (block == 1 && npc == 177 && summoner_done) return {};
        if (block < 0 || block > 4) return {};
        return lines(block);
    }
    // FUN_0059d300.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
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
    // What hearing `string` from `npc` does (FUN_0059cb20).
    struct Heard { bool town_portal = false, act3 = false, take_staff = false; };
    Heard said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool in_lair) {
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
            rewarded = true;
            return out;
        }
        static constexpr std::array<std::pair<int, int>, 6> kLineBits{ { { 444, 9 }, { 445, 6 }, { 446, 7 }, { 447, 11 }, { 449, 8 }, { 452, 10 } } };
        for (const auto& [line, bit] : kLineBits) if (string == line) quest::set(quest_bits, kQuest, bit);
        return out;
    }
    void talk_closed(QuestBits& quest_bits, int npc) {   // LAB_0059c760
        if (npc == kTyrael) { if (tyrael_pending) { log = 4; tyrael_pending = false; } return; }
        if (npc != kJerhyn) return;
        if (jerhyn_pending) { log = 1; jerhyn_pending = false; }
        else if (reward_pending) { log = 6; reward_pending = false; }
        else return;
        mark(quest_bits);
    }
    // Duriel died (FUN_0059d050). `by_him`: the player at his death without
    // bit 0 / 3 / 4 / 5 gets bit 5 (FUN_0059cfb0: in the Lair).
    void duriel_died(QuestBits& quest_bits, bool by_him) {
        if (open) { state = 3; if (log_in == 0) log_in = 8; }
        if (by_him && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 3) && !quest::bit(quest_bits, kQuest, 4)) quest::set(quest_bits, kQuest, 5);
        duriel_dead = true;
        tyrael_talks = true;
    }
    // The staff into the orifice (FUN_0059dd80). The Horadric Staff done
    // (quest 10 bits 0, 13; the staff and its parts taken), the timer to the
    // way down (FUN_0059d870) once.
    void staff_in(QuestBits& quest_bits) {
        quest::set(quest_bits, 10, 0);
        quest::set(quest_bits, 10, 13);
        staff_placed = true;
        if (!timer_started) { timer_started = true; open_in = 1; }
    }
    // The orifice's room comes up (InitFn 21, FUN_0059db50). True: it's
    // open (mode 2): the staff went in, in this game or an earlier one.
    bool orifice_seen() {
        if (later && !timer_started && !lair_open) { timer_started = true; open_in = 1; return true; }
        return lair_open;
    }
    // A tick. True: the way down opens (object 100 by the orifice).
    bool tick() {
        if (log_in > 0 && --log_in == 0 && log != 3 && log != 4 && log != 5) log = 3;
        if (open_in < 0 || open_in-- > 0) return false;
        timer_started = false;
        if (!staff_placed) return false;
        staff_placed = false;
        lair_open = true;
        return true;
    }
};

// The Tainted Sun (a2q3.cpp, the record at FUN_0059eea0): quest 11. The
// sun goes out a while after a player first reaches the Lost City or the
// Valley of Snakes; the altar in the Claw Viper Temple's second level
// brings it back. Bits: 0 done, 1 reward due, 2 the dark seen, 3 Drognan
// told, 4 in the temple, 13 took part, 14 done by someone else, 15 closed.
// N-player gap: one player here — the rewarded list (rec+0x1c) is a bool,
// the party's credit (LAB_0059a0b0 -> 0x59a040) isn't here.
struct TaintedSunQuest {
    static constexpr int kQuest = 11, kDrognan = monster_ids::kDrognan, kLostCity = level_ids::kLostCity, kValley = level_ids::kValleyOfSnakes;
    static constexpr int kTemple = level_ids::kClawViperTempleLevel2, kAltar = 149;   // objects.txt row (InitFn 20, OperateFn 24)
    static constexpr int kTold = 348, kThanksFirst = 362, kThanksLast = 372;
    bool open = true;   // +9
    int state = 0;      // +0xc: 0 not yet, 1 dark, 2 Drognan told, 3 in the temple, 4 the altar used, 5 done
    int log = 0;        // +0xb
    int dark_in = -1;   // data+1: ticks to the darkness (rand(17) on the quest seed), -1 not running
    bool dark = false;  // data+2: the sun's out (FUN_0061c450 .. FUN_0061c4d0)
    bool used = false;  // data+4: the altar's been used (data+8 its mode 2)
    int log_in = 0;     // the altar's timer (10 ticks, LAB_0059a700): log 3
    bool rewarded = false;

    // A player's bits for the state (LAB_0059a4f0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state >= 1) quest::set(quest_bits, kQuest, 2);
        if (state == 2) quest::set(quest_bits, kQuest, 3);
        if (state == 3) quest::set(quest_bits, kQuest, 4);
    }
    // The sun goes out (FUN_0059a350): log 1, given out, the act's sky
    // dark (each player gets S->C 0x53 {5, 0, 1}, FUN_0059a100).
    void darken() {
        log = 1;
        if (state == 0) state = 1;
        dark = true;
    }
    // The player joins (FUN_0059a5d0); done or closed: shut for the game.
    void join(const QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) open = false;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) { quest::set(game_bits, kQuest, 13); return; }
        if (!quest::bit(quest_bits, kQuest, 2)) return;
        darken();                                 // FUN_0059a570: their sky's dark again
        if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { log = 2; state = 2; }
        else { log = 1; state = 1; }
    }
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // The player went from level `from` to `dest` (FUN_0059ee00). True:
    // the desert's far end starts the darkness's timer, rand(17) ticks on
    // the quest seed (FUN_0059edc0), which the caller rolls into dark_in.
    bool enter(int from, int dest) {
        const bool starts = (dest == kLostCity || dest == kValley) && state == 0 && open && dark_in < 0 && !dark;
        if (from == level_ids::kLutGholein) {
            rewarded = false;                     // FUN_00545310
            if (state == 2) state = 3;
        }
        return starts;
    }
    // A tick (40 ms).
    void tick(QuestBits& quest_bits) {
        if (dark_in >= 0 && dark_in-- == 0 && !dark) { darken(); mark(quest_bits); }   // LAB_0059ed80
        if (log_in > 0 && --log_in == 0 && state == 4) log = 3;
    }
    // The altar's room comes up (InitFn 20 -> FUN_0059a3f0): in the temple,
    // dark if not yet; log 2 if none. Its mode: 2 once used.
    void altar_seen() {
        if (!open) return;
        if (state < 2) { darken(); state = 3; }
        if (log == 0) log = 2;
    }
    // What `npc` says (FUN_0059a270): blocks at 0x73a188 by the state
    // (0x73a620: -1, 0, 1, 2).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { 176, 344, false }, { 175, 343, false }, { 198, 341, false }, { 199, 342, false }, { kDrognan, 348, true }, { 202, 347, false },
          { 244, 350, false }, { 210, 349, false }, { 200, 345, false }, { 201, 340, false }, { 178, 346, false } },
        { { 176, 344, false }, { 175, 343, false }, { 198, 341, false }, { 199, 342, false }, { kDrognan, 348, false }, { 202, 347, false },
          { 244, 350, false }, { 210, 349, false }, { 200, 345, false }, { 201, 340, false }, { 178, 346, false } },
        { { 176, 361, false }, { 175, 353, false }, { 198, 352, false }, { 199, 359, false }, { kDrognan, 358, false }, { 202, 357, false },
          { 244, 360, false }, { 210, 355, false }, { 201, 351, false }, { 200, 354, false }, { 178, 356, false } },
        { { 176, 368, true }, { 175, 366, true }, { 198, 363, true }, { 199, 364, true }, { kDrognan, 371, true }, { 202, 370, true },
          { 244, 372, true }, { 210, 367, true }, { 200, 365, true }, { 201, 362, true } },
        { { 176, 368, false }, { 175, 366, false }, { 198, 363, false }, { 201, 362, false }, { 199, 364, false }, { kDrognan, 371, false },
          { 202, 370, false }, { 210, 367, false }, { 200, 365, false }, { 178, 369, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 1)) block = 3;
        else if (quest::bit(quest_bits, kQuest, 14)) return {};
        else if (rewarded) block = 4;
        else if (state != 0 && open && !quest::bit(quest_bits, kQuest, 0) && state <= 3) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!" (FUN_0059a1c0): the reward due, over the town's people; or
    // Drognan while it's new.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        static constexpr std::array<int, 10> kTown{ 176, 175, 198, 199, 177, 202, 244, 210, 200, 201 };
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        return (quest::bit(quest_bits, kQuest, 1) && std::ranges::find(kTown, npc) != kTown.end()) || (npc == kDrognan && state == 1);
    }
    // The player heard `string` from `npc` (LAB_0059ebe0): Drognan's 348
    // tells it; any thanks (362..372) with the reward due is done.
    void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string) {
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
        rewarded = true;
        if (!open) quest::set(game_bits, kQuest, 13);
    }
    // The altar used (OperateFn 24, FUN_0059a7e0). `refused`: the player has
    // it (bit 0 / 1) and quest 10's done or carries the Viper Amulet / the
    // staff: "I can't" (voice 0x13). Otherwise once: the sun back (S->C 0x53
    // {2, 0, 0}), state 4, the player's credit (bits 1 and 13, 14 off; the
    // altar's room too, FUN_0059a680), a Viper Amulet for each player
    // without one or the staff or quest 10 done (LAB_0059a770), the game's
    // bit 13, the 10-tick log timer.
    struct Use { bool refused = false, used = false, took_part = false; int amulets = 0; };
    Use use_altar(QuestBits& quest_bits, QuestBits& game_bits, bool has_amulet, bool has_staff) {
        const bool has_it = quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1);
        if (has_it && (quest::bit(quest_bits, 10, 0) || has_amulet || has_staff)) return { .refused = true };
        if (used) return {};
        used = true;
        Use out{ .used = true };
        out.amulets = !has_amulet && !has_staff && !quest::bit(quest_bits, 10, 0) ? 1 : 0;
        if (!open) return out;
        dark = false;
        state = 4;
        if (!quest::bit(quest_bits, kQuest, 0)) {
            quest::set(quest_bits, kQuest, 1);
            quest::set(quest_bits, kQuest, 13);
            quest::set(quest_bits, kQuest, 14, false);
        }
        out.took_part = quest::bit(quest_bits, kQuest, 13);   // LAB_0059a730: voice 0x34
        quest::set(game_bits, kQuest, 13);
        log_in = 10;
        return out;
    }
};

// Quest 4; bits: 0 done, 1 reward due, 2 Akara told, 3 under way, 4 the
// stones done, 10 the Cow King dead, 13 took part in the rescue, 14 missed
// it, 15 closed. Fields are the record's data (rec+0x18) bytes.
// ponytail: one player — lists A (+0xb4, missed the rescue), B (+0x138,
// heard Cain's thanks) and the rewarded list (rec+0x1c) are a bool each;
// the party's bits, the voices and the quest-item-gone rewind (+0xc4,
// FUN_00592c80: d2d's scrolls only go by the quest) are left out.
struct CainQuest {
    static constexpr int kQuest = 4, kAkara = monster_ids::kAkara, kCain = monster_ids::kCain, kCampCain = monster_ids::kCampCain, kStony = level_ids::kStonyField, kTristram = level_ids::kTristram;
    int state = 0;      // +0xc: 0 init, 1 open, 2 Akara told, 3 out of town, 4 scroll, 5 deciphered / stones, 6 done, 7 the Rogues got him
    int log = 0;        // +0xb
    bool active = true; // rec+9: closed at join by bit 0 or 15 (FUN_00544410)
    std::array<int, 5> order{};  // +0x00: the stone (objects.txt Id 17..21) to touch at step i
    int touched = 0;    // +0x0c
    int throttle = 0;   // +0x2c: the "need the scroll" voice's
    bool ordered = false, rejoined = false, stones_open = false, deciphered = false, stones_done = false;   // +0x4a, +0x4c, +0x4d, +0x4e, +0x4f
    bool resolved = false, camp_cain = false, camp_due = false, akara_log = false, closed_join = false;    // +0x50, +0x51, +0x52, +0x53, +0x63
    bool decipher_log = false, portal_made = false, tree_used = false;                                     // +0x64, +0x45, +0x58
    bool rescued_flag = false;  // the game's flag (4, 13): Cain rescued game-wide
    bool missed = false, thanked = false, rewarded = false;

    // The chain reached it (+0xf0, FUN_00593d70, from quest 2's): state
    // 0 → 1. True: it passes on to quest 3's (+0x10).
    bool open() {
        if (state == 0 && active) { state = 1; return false; }
        return state == 6 || !active;
    }
    // The quest log's state (+0xd 6).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 6); }
    // A player's bits for the state (LAB_00592130).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        else if (state > 2 && state < 6) quest::set(quest_bits, kQuest, 3);
    }
    // The player joins, carrying the scroll (bks) / the deciphered one (bkd) (FUN_00597030).
    void join(const QuestBits& quest_bits, bool bks, bool bkd) {
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
    // What `npc` says about it (FUN_00592580): blocks at 0x737668, by the
    // state (0x737648: -1, 0, 1, 2, 3, 4).
    // ponytail: the bkd a player still carries once the stones are done
    // isn't taken here (the fifth stone took the only one).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[10] = {
        { { kAkara, 97, true } },
        { { kAkara, 99, false }, { 150, 98, false }, { 154, 100, false }, { 147, 102, false }, { 155, 101, false } },
        { { 150, 105, false }, { 155, 107, false }, { 154, 103, false }, { kAkara, 104, false }, { 147, 106, false } },
        { { 150, 108, false }, { 155, 111, false }, { 154, 109, false }, { kAkara, 112, true }, { 147, 110, false } },
        { { 150, 113, false }, { 155, 116, false }, { 154, 114, false }, { kAkara, 117, false }, { 147, 115, false } },
        { { 150, 119, false }, { 155, 122, false }, { 154, 121, false }, { kCampCain, 123, true }, { kAkara, 118, true }, { 147, 120, false } },
        { { kCampCain, 125, true } },
        { { kCampCain, 123, false }, { kAkara, 118, false }, { 147, 120, false } },
        { { kCampCain, 125, false } },
        { { kCain, 124, true } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool bks) const {
        const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1), b13 = quest::bit(quest_bits, kQuest, 13), b14 = quest::bit(quest_bits, kQuest, 14);
        int block = -1;
        if (npc == kCampCain && !thanked && b13) block = 5;
        else if (bit1) block = npc == kCampCain && thanked ? 7 : 5;
        else if (missed) block = 6;
        else if (rewarded) block = npc == kCampCain && !thanked ? 5 : b14 ? 8 : bit0 ? 7 : -1;
        else if (!b14 && state != 0 && !bit0 && !quest::bit(quest_bits, kQuest, 15)) block = bks ? 3 : state == 4 ? 2 : state <= 5 ? state - 1 : -1;
        if (npc == kCain) block = 9;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_00592fb0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool bks = false) const {
        const bool bit0 = quest::bit(quest_bits, kQuest, 0), bit1 = quest::bit(quest_bits, kQuest, 1), b13 = quest::bit(quest_bits, kQuest, 13);
        if (npc == kAkara) return (state == 4 && !bit0 && !bit1 && bks) || (state == 1 && !bit0 && !bit1) || (state == 6 && b13 && !bit0) || bit1;
        return npc == kCampCain && (missed || (!thanked && b13));
    }
    // The player heard `string` from `npc` (FUN_00592250). decipher: Akara
    // takes the scroll (bks) for the deciphered one (bkd, FUN_005466b0 ilvl
    // 0, quality 2); ring: her reward, a ring (Normal ilvl 7 magic,
    // Nightmare 30 / Hell 60 rare). With bit 13, the game's flag (4, 13)
    // going on runs the chain (+0xf0).
    enum class Said { none, decipher, ring };
    Said said(QuestBits& quest_bits, int npc, int string, bool bks) {
        if (npc == kCampCain) {
            if (string == 125) { rewarded = true; missed = false; }
            if (string == 126 || string == 123) thanked = true;
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
        rewarded = true;
        if (quest::bit(quest_bits, kQuest, 13)) {
            log = 0xd; state = 6;
            rescued_flag = true;
        }
        if (closed_join) rescued_flag = true;
        return Said::ring;
    }
    // The talk with `npc` closed (FUN_005921b0).
    void talk_closed(QuestBits& quest_bits, int npc) {
        if (npc != kAkara) return;
        if (akara_log) { log = 1; akara_log = false; mark(quest_bits); }
        if (decipher_log) { log = 3; mark(quest_bits); decipher_log = false; }
    }
    // The player went from level `from` to `dest` (FUN_00596de0). True:
    // camp Cain's due in the camp (FUN_00592960).
    bool enter(QuestBits& quest_bits, int from, int dest) {
        if (dest == kTristram && !camp_cain && !resolved && state > 5) { state = 5; log = 4; mark(quest_bits); }
        const bool fresh = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1);
        if (from == level_ids::kRogueEncampment) {
            rewarded = missed = false;
            // ponytail: marked now, not at the next event 4 (FUN_00592e20)
            if (fresh && state == 2) { state = 3; mark(quest_bits); }
        }
        if (dest == level_ids::kRogueEncampment) return camp_spawn();
        if (dest == level_ids::kLutGholein && fresh && !resolved && state < 6) { rogues(quest_bits); tree_used = true; }
        return false;
    }
    // Camp Cain appears if he's due (FUN_005940e0 / FUN_00592960).
    bool camp_spawn() {
        if (!camp_due || camp_cain) return false;
        camp_cain = true; camp_due = false;
        return true;
    }
    // Not rescued before Act 2: the Rogues get him (FUN_00597310; the Lut
    // Gholein arrival's the same, with the tree's mode). FUN_00596ca0(0, 1)
    // resolves it; the players still in it missed it (FUN_00592b10).
    void rogues(QuestBits& quest_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || resolved || state >= 6) return;
        resolved = true;
        if (!camp_cain) camp_due = true;
        state = 7; log = 5;
        rescued_flag = true;
        quest::set(quest_bits, kQuest, 14);
        missed = true;
    }
    // The Tree of Inifuss (OperateFn 12, FUN_00593af0). True: the scroll
    // (bks) drops at the tree.
    bool tree(const QuestBits& quest_bits, bool has_scroll) {
        if (!active || state >= 6 || tree_used || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || has_scroll) return false;
        state = 4; log = 2;
        tree_used = true;
        return true;
    }
    // The stones' order from the quest rng (game+0x10f4 +0x18, drawn on in
    // place; FUN_00592e90): seed = low * 0x6ac690c5 + high; step n goes to
    // slot low % 5 if it's free.
    void stone_order(std::uint32_t& low, std::uint32_t& high) {
        order = {};
        for (int step = 0; step < 5;) {
            const std::uint64_t seed = std::uint64_t(low) * 0x6ac690c5u + high;
            low = std::uint32_t(seed); high = std::uint32_t(seed >> 32);
            if (!order[low % 5]) order[low % 5] = 17 + step++;
        }
        ordered = true;
    }
    // A Cairn Stone (objects.txt Id `stone`, OperateFn 9, FUN_00593710);
    // `fresh`: it's still mode 0. lit: it lights; portal: the fifth — the
    // bkd goes, CairnStones (missile 0x120) opens the way to Tristram.
    enum class Stone { none, lit, portal };
    Stone stone(QuestBits& quest_bits, int stone, bool bkd, bool fresh) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return Stone::none;
        if (!bkd) { ++throttle; return Stone::none; }
        if (!active || state > 5 || stones_done) return Stone::none;
        state = 5;
        if (stone != order[std::size_t(touched)]) return Stone::none;   // out of order: ignored, kept
        ++touched;
        if (!fresh) return Stone::none;
        if (touched < 5) return Stone::lit;
        stones_done = true;
        if (log < 4) { log = 4; quest::set(quest_bits, kQuest, 4); }
        return Stone::portal;
    }
    // The stones as Stony Field comes up (InitFn 6, FUN_005935e0). True:
    // the portal's re-made at StoneAlpha (FUN_00592d50: x + 4, y + 4).
    bool stones_init() {
        if (active && !rejoined) return false;
        rejoined = false;
        if (portal_made) return false;
        return portal_made = true;
    }
    // The Gibbet (OperateFn 10, FUN_00593480), `fresh` its mode 0. True: it
    // opens, the player took part (bits 13, 1).
    bool gibbet(QuestBits& quest_bits, bool fresh) {
        if (!active || resolved || state >= 6) return false;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || !fresh) return false;
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
        return true;
    }
    // The Gibbet's opened (its mode end, FUN_00593290): Tristram Cain
    // (monster 0x92) comes out at x + 3, y + 3 and walks to his own portal
    // (World::cain_walk). `spawned` false (no room for him): a town portal's
    // due by it (x + 6, y + 6) and he goes straight to the camp (+0x52).
    void rescued(bool spawned) {
        if (!spawned && !camp_cain) camp_due = true;
        log = 6;
    }
    // Tristram Cain went through his portal (FUN_005944f0: +0x91, +0x52):
    // camp Cain's due (FUN_005940e0 → FUN_00592960).
    void portal_entered() { camp_due = true; }
    // The Cube's Cow Portal (output kind 1, FUN_00594140): none opened this
    // game yet (record 0xb's flag, FUN_00544760), the Cow King alive (bit
    // 10), the last act's boss down in this difficulty (expansion: quest
    // 0x28 bit 0, else 0x1a bit 0), in the Rogue Encampment. Then object 60
    // to the Moo Moo Farm by the player (FUN_00545340, FUN_0056d130) and the
    // flag set (FUN_00544720); else the inputs stay. The Pandemonium and
    // Finale portals (kinds 2 / 3: FUN_00594270 / FUN_00594280) are a bare
    // `return 0` in 1.14d, so their keys and organs stay too.
    [[nodiscard]] static bool cow_portal_opens(const QuestBits& quest_bits, bool opened, bool expansion, int level_id) {
        return !opened && !quest::bit(quest_bits, kQuest, 10) && quest::bit(quest_bits, expansion ? 0x28 : 0x1a, 0) && level_id == level_ids::kRogueEncampment;
    }
};

// The Forgotten Tower, one game's (a1q5.cpp, the record at FUN_00595920).
// Quest 5; bits: 0 done (set at the kill: no reward talk), 2..6 progress,
// 13 took part, 14 not there.
// ponytail: one player — lists A / B (rec+0x18) are `told` / `due`, the
// cellar-5 list and party passes are left out; rec+9 (active) is taken as
// always set, so the tome never starts read (InitFn 4, FUN_00595a00).
struct TowerQuest {
    static constexpr int kQuest = 5, kTower = level_ids::kForgottenTower, kCellar = level_ids::kTowerCellarLevel5, kCountess = 6, kTome = 127;   // kCountess: SuperUniques row
    // Her treasure's spawner (Missiles.txt towerchestspawner: Range,
    // Param1, Param2, Param3).
    static constexpr int kTreasureFrames = 400, kTreasureOpen = 150, kTreasureEvery = 2, kTreasureRadius = 5;
    int state = 0;      // +0xc: 0 init, 2 started, 3 cellar / out of town, 5 Countess dead (1 and 4 unused)
    int log = 0;        // +0xb
    int log_in = 0;     // ticks to the kill's timer (0x5954c0, 7), 0 not running
    bool told = false;  // list A: had their success talk
    bool due = false;   // list B: killed her in cellar 5, the success talk due
    bool dead = false;  // d+0x118: rec+0xc0 is gone
    bool treasure = false;   // d+0x119: her treasure's spawners made
    bool first_talk = false;   // d+0x11a
    bool tome_early = false;   // d+0x11b: the tome read while log == 0

    // A player's bits for the state (LAB_00594890; 0x59494c by log).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3 && log >= 1 && log <= 4) quest::set(quest_bits, kQuest, log + 2);
    }
    // The player joins (LAB_00595860).
    void join(const QuestBits& quest_bits) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) return;
        if (quest::bit(quest_bits, kQuest, 4)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 6)) { state = 3; log = 4; }
        else if (quest::bit(quest_bits, kQuest, 5)) { state = 2; log = 3; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
    // The chain reached it (+0xf0, FUN_00595240). True: it passes on to
    // quest 3's (+0x10).
    [[nodiscard]] bool chain() const { return state == 5; }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A tick (40 ms): the kill's timer sets the log to 13.
    void tick() {
        if (log_in > 0 && --log_in == 0 && state == 5) log = 13;
    }
    // The Moldy Tome read (OperateFn 6, FUN_00594e70), then its message
    // 127 heard.
    void read_tome(QuestBits& quest_bits) {
        if (state < 2) { state = 2; if (log == 0) tome_early = true; }
        said(quest_bits, -1, kTome);
    }
    // What `npc` says about it (FUN_00594c50): blocks at 0x737ed8 by the
    // state (0x7382ac: -1, -1, 0, 1, 2, 3, -1).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[4] = {
        { { 265, 131, false }, { 154, 129, false }, { 148, 130, false }, { 155, 132, false }, { 150, 133, false }, { 147, 128, false } },
        { { 150, 134, false }, { 155, 136, false }, { 154, 137, false }, { 148, 138, false }, { 265, 135, false }, { 147, 139, false } },
        { { 150, 140, true }, { 155, 141, true }, { 265, 145, true }, { 154, 144, true }, { 148, 143, true }, { 147, 142, true } },
        { { 150, 140, false }, { 155, 141, false }, { 265, 145, false }, { 154, 144, false }, { 148, 143, false }, { 147, 142, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        const bool bit0 = quest::bit(quest_bits, kQuest, 0), b13 = quest::bit(quest_bits, kQuest, 13);
        if (bit0 && !b13) return {};
        if (state > 3 && !due && !told) return {};
        int block = -1;
        if (due) block = 2;
        else if (bit0) { if (!told) return {}; block = 3; }
        else if (state >= 2 && state <= 5) block = state - 2;
        std::vector<QuestMsg> out;
        if (block >= 0)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_005952c0): the success talk's due, not
    // with Warriv or Gheed.
    [[nodiscard]] bool alert(const QuestBits&, int npc) const {
        return due && (npc == monster_ids::kKashya || npc == monster_ids::kCharsi || npc == monster_ids::kAkara || npc == monster_ids::kCampCain);
    }
    // The player heard `string` from `npc` (FUN_00594960). True: the first
    // success talk after the kill — the chain opens (rec+0xf0, FUN_00595240
    // → quest 3's).
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (string == kTome) {                         // no NPC check
            bool changed = false;
            if (tome_early) {
                if (log == 0) { log = 1; changed = true; }
                if (log == 3) { log = 2; changed = true; if (state < 3) state = 3; }
            }
            if (state < 2) { state = 2; mark(quest_bits); }
            else if (changed) mark(quest_bits);
            return false;
        }
        const bool town = npc == monster_ids::kCharsi || npc == monster_ids::kKashya || npc == monster_ids::kCampCain || npc == monster_ids::kWarriv
                       || npc == monster_ids::kAkara || npc == monster_ids::kGheed;
        if (!town || string < 140 || string > 145) return false;
        bool chain = false;
        if (quest::bit(quest_bits, kQuest, 13) && first_talk) { first_talk = false; state = 5; chain = true; }
        if (due) { due = false; told = true; }
        return chain;
    }
    // The player went from level `from` to `dest` (FUN_00595010).
    void enter(QuestBits& quest_bits, int from, int dest) {
        if (dest == kTower) {
            if (state == 0) { state = 2; log = 3; }
            else if (state <= 3 && log == 1) log = 4;
            else return;
            mark(quest_bits);
        } else if (dest == kCellar) {
            if (state > 3 || log == 2) return;
            state = 3; log = 2;
            mark(quest_bits);
        } else if (from == 1) {                        // out of town
            if (state == 2) { if (!quest::bit(quest_bits, kQuest, 0)) state = 3; }
            else if (state == 5) told = false;         // FUN_00594740 (then rec+0xa = 0 once both lists are empty)
        }
    }
    // The Countess died (FUN_00595710, once: rec+0xc0 is cleared). True:
    // the player's kill for the quest (FUN_00594f10: in cellar 5 — bits 13
    // and 0, the voice); elsewhere bit 14. The 7-tick timer starts.
    bool killed(QuestBits& quest_bits, bool in_cellar) {
        if (dead) return false;
        dead = true;
        state = 5;
        first_talk = true;
        log_in = 7;
        if (quest::bit(quest_bits, kQuest, 0)) return false;
        if (!in_cellar) { quest::set(quest_bits, kQuest, 14); return false; }
        quest::set(quest_bits, kQuest, 13);
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 1, false);
        due = true;
        return true;
    }
};

// Tools of the Trade, one game's (a1q3.cpp, the record at FUN_00591f70).
// Quest 3; bits: 0 done (imbued), 1 reward due (the imbue), 2 given,
// 3 out of town / the malus dropped, 6 the malus picked up once, 13
// returned in this game, 14 returned by someone else, 15 closed. The
// malus ('hdm ') drops from its stand, object 108 in the Barracks.
// ponytail: one player — the participant and party lists, the carrier
// count (+0x9c) and the reset when the last carrier loses it (0x5918d0,
// events 6 / 9) are left out.
struct ToolsQuest {
    static constexpr int kQuest = 3, kCharsi = monster_ids::kCharsi, kClvl = 8, kStand = operate_fn::kMalusStand;
    int state = 0;           // +0xc: 0 closed, 1 open, 2 Charsi gave it, 3 out of town, 4 the malus dropped, 5 returned
    int log = 0;             // +0xb
    bool active = true;      // +9: off when the player joins with it done or closed (FUN_00546270)
    bool dropped = false;    // data+1 / +0x98 == 2: the stand's used
    bool given = false, returned = false;   // data[2] / data[3]: the log waits for the talk to close
    bool game_returned = false;             // the game's flag 13 (FUN_00544720(3, 0xd))
    bool carried_in = false;                // data+0xa1: the player joined holding the malus

    // A player's bits for the state (0x591340).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3 || state == 4) quest::set(quest_bits, kQuest, 3);
    }
    // The player joins (FUN_00591ed0); a first player with it done or
    // closed turns it off for the game (FUN_00546270, FUN_00544410).
    void join(const QuestBits& quest_bits, bool holding = false) {
        carried_in = holding;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
        if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    }
    // The chain reached it (+0xf0, FUN_00591e40): open (Charsi's "!").
    // True: it passes on to the next (+0x10 = 6).
    bool open() {
        if (state == 0 && active) { state = 1; return false; }
        return state == 5 || !active;
    }
    // What `npc` says about it (FUN_005916a0): holding the malus at clvl
    // 8+, block 3 (Charsi's "the malus!"); else the block for the state
    // (0x737630: -1, 0, 1, 2, 3, 4; state 4 skipped). Blocks at 0x737198.
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[5] = {
        { { kCharsi, 146, true } },
        { { 148, 148, false }, { 150, 149, false }, { 265, 147, false }, { kCharsi, 150, false }, { 147, 151, false }, { 155, 153, false } },
        { { 150, 156, false }, { 155, 159, false }, { kCharsi, 157, false }, { 265, 154, false }, { 148, 155, false }, { 147, 158, false } },
        { { 150, 162, false }, { 155, 165, false }, { kCharsi, 163, true }, { 265, 160, false }, { 148, 161, false }, { 147, 164, false } },
        { { 150, 162, false }, { 155, 165, false }, { 265, 160, false }, { 148, 161, false }, { 147, 164, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool holding, int clvl) const {
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
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool holding, int clvl) const {
        if (npc != kCharsi || quest::bit(quest_bits, kQuest, 0)) return false;
        if (state == 1 && !quest::bit(quest_bits, kQuest, 1)) return true;
        return clvl >= kClvl && holding;
    }
    // The player heard `string` from `npc` (FUN_00591490). True: Charsi
    // took the malus back (the caller removes it, FUN_00544160) — the
    // imbue's due (bits 13 and 1) and "quest complete" (0xca7). The state
    // going 4 → 5 runs the chain (+0xf0); else, the malus carried into the
    // game, quest 6's (+0x10) runs directly.
    bool said(QuestBits& quest_bits, int npc, int string, bool holding) {
        if (npc != kCharsi) return false;
        if (string == 146) { state = 2; given = true; return false; }
        if (string != 163 || quest::bit(quest_bits, kQuest, 0) || !holding) return false;
        quest::set(quest_bits, kQuest, 13);
        quest::set(quest_bits, kQuest, 1);
        if (active && state == 4) { state = 5; returned = true; game_returned = true; }
        return true;
    }
    // The talk with `npc` closed (0x5913c0).
    void talk_closed(QuestBits& quest_bits, int npc) {
        if (npc != kCharsi) return;
        if (given) { mark(quest_bits); log = 1; given = false; }
        else if (returned) { log = 0xd; returned = false; }
    }
    // The player left level `from` (LAB_00591810): out of town after
    // Charsi gave it.
    void enter(QuestBits& quest_bits, int from) {
        if (!active || from != 1 || state != 2 || quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        log = 1;
        state = 3;
        mark(quest_bits);
    }
    // The stand operated (OperateFn 21, FUN_00591ac0). drop: the malus
    // falls from it (FUN_00559a30) and it's used (mode 2); refuse: "I
    // can't" (FUN_00553380) — too low a level, or it's off in this game
    // (the stand goes to mode 2 all the same).
    enum class Stand { none, refuse, drop };
    Stand operate(QuestBits& quest_bits, int clvl) {
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
    bool picked_up(QuestBits& quest_bits) {
        log = 2;
        if (quest::bit(quest_bits, kQuest, 6)) return false;
        quest::set(quest_bits, kQuest, 6);
        return true;
    }
    // Charsi imbued an item (FUN_00591790): done, the reward's used.
    void imbued(QuestBits& quest_bits) {
        quest::set(quest_bits, kQuest, 0);
        quest::set(quest_bits, kQuest, 1, false);
    }
    // The quest log's state (FUN_00591d30).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool holding, int clvl) const {
        if (quest::bit(quest_bits, kQuest, 1)) return 10;
        if (holding) return quest::bit(quest_bits, kQuest, 0) ? 0 : 2;
        if (!active) return 0;
        if (quest::bit(quest_bits, kQuest, 13)) return 0xd;
        if (quest::bit(quest_bits, kQuest, 14)) return 0xc;
        if (state < 5) return log;
        return game_returned ? (clvl >= kClvl ? 12 : 4) : 0;
    }
};

// The chain from `quest` (its +0xf0, on through +0x10 while one passes
// on): 1 → 2 → 4 → 3 → 6, 5 → 3 (the records' +0x10). The first join runs
// it from quest 1 (FUN_00546270).
inline void chain(int quest, DenQuest& den, BurialQuest& burial, CainQuest& cain, TowerQuest& tower, ToolsQuest& tools, AndyQuest& andy) {
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

// The quest log (client side, QuestLog.cpp).
// What the log says about a quest (FUN_004a1950): its record (0x7237a4 on,
// 64 bytes a quest: name, the message to replay once done, the reward's
// state - 1 (-1 none), then {string, message} a log state: state s at
// [2s + 1], [2s + 2]; 3725 none), by the log state the server sent (s), the
// player's flags and the game's (FUN_004b32e0). `shown` (+0x266): 0 done,
// its animation still to play (bit 12 clear), 1 done, 2 hidden, 3 shown.
// The Den's states 3 / 4 add the count ("Monsters remaining: " N, 3739 for
// one or none).
// ponytail: single player (DAT_007a0610 0: 3729, never 3730).
inline constexpr std::array<std::array<std::uint16_t, 29>, 12> kQuestLogRecords = { {
    { 3714, 76, 4, 3735, 64, 3736, 64, 3737, 64, 3738, 64, 3740, 64, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3740, 64, 3728, 3725, 3727, 3725, 3726, 64 },
    { 3715, 92, 2, 3741, 81, 3742, 81, 3743, 81, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3743, 81, 3728, 3725, 3727, 3725, 3726, 81 },
    { 3716, 163, 0xffff, 3755, 146, 3756, 146, 3733, 146, 3732, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3757, 163, 3728, 3725, 3727, 3725, 3726, 146 },
    { 3717, 123, 0xffff, 3744, 97, 3745, 97, 3746, 97, 3747, 97, 3748, 97, 3749, 97, 3734, 97, 3725, 3725, 3725, 3725,
      3750, 97, 3728, 3725, 3727, 3725, 3726, 97 },
    { 3718, 127, 0xffff, 3751, 127, 3754, 127, 3752, 127, 3753, 127, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3725, 3725, 3728, 3725, 3727, 3725, 3726, 127 },
    { 3719, 184, 9, 3758, 166, 3759, 166, 3761, 166, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3760, 166, 3728, 3725, 3727, 3725, 3726, 184 },
    // Act 2's, quests 9..14 (0x723b24 on).
    { 923, 334, 2, 941, 304, 942, 304, 943, 304, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      943, 3725, 3728, 3725, 3727, 3725, 3726, 304 },
    { 924, 339, 0xffff, 944, 3725, 945, 335, 946, 338, 948, 3725, 947, 339, 947, 3725, 3725, 3725, 3725, 3725, 3730, 3725,
      947, 3725, 3728, 3725, 3727, 3725, 3726, 339 },
    { 925, 3725, 0xffff, 950, 3725, 951, 3725, 952, 348, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      952, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 926, 3725, 0xffff, 954, 3725, 953, 373, 953, 377, 955, 377, 956, 396, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      956, 396, 3728, 396, 3727, 3725, 3726, 396 },
    { 927, 3725, 0xffff, 957, 3725, 958, 3725, 3726, 3725, 959, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      959, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 928, 442, 4, 960, 430, 962, 431, 11030, 431, 964, 431, 965, 302, 966, 442, 961, 396, 3729, 3725, 3730, 3725,
      965, 3725, 3728, 3725, 3727, 3725, 3726, 442 },
} };
// `state` is what the picker keeps as the quest's last shown state
// (0x7bf380, -1 left as it was); `marks` when a change of it is news (+0x263:
// the open log picks that quest, FUN_004a3220).
struct QuestText { int string = 0, count = -1, speech = 0, shown = 2, state = -1; bool marks = false; };
struct QuestState {
    QuestLogStates log{};                  // the log state the server sent, by quest (the view's quest_log)
    QuestGameFlags game{};                 // the game's quest flags (the view's game_quests)
    int den_left = 0;                      // DAT_007bf2a4
};
inline QuestText quest_text(const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state) {
    if (quest < 1 || quest > 14 || quest == 7 || quest == 8) return {};
    const auto& rec = kQuestLogRecords[std::size_t(quest < 7 ? quest - 1 : quest - 3)];
    auto flag = [&](int bit) { return d2d::rules::quest::bit(quest_bits, quest, bit); };
    auto game = [&](int bit) { return (quest_state.game[std::size_t(quest)] >> bit & 1) != 0; };
    auto entry = [&](int string, int speech, int shown, int state, bool marks) {
        return QuestText{ string == 3725 ? 0 : string, -1, speech == 3725 ? 0 : speech, shown, state, marks };
    };
    const bool bit0 = flag(0), bit1 = flag(1), b13 = flag(13);
    int log_state = quest_state.log[std::size_t(quest)];
    if (bit0) {                                               // done: 13 in this game, 11 before
        log_state = b13 ? 13 : 11;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[1], flag(12) ? 1 : 0, log_state, false);
    }
    if (quest == 14 && !b13) {                                // the Seven Tombs' own case (record 0x723a64)
        const bool summoner = d2d::rules::quest::bit(quest_bits, 12, 0) || d2d::rules::quest::bit(quest_bits, 12, 13);
        const int tomb = summoner && log_state == 1 ? 7 : flag(3) ? 5 : flag(4) ? 6 : 0;
        if (tomb) return entry(rec[std::size_t(2 * tomb + 1)], rec[std::size_t(2 * tomb + 2)], 3, tomb, false);
    }
    if (b13 && bit1 && rec[2] != 0xffff) {                    // the reward's due
        log_state = rec[2] + 1;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], 3, log_state, true);
    }
    if (bit1 && flag(15)) return rec[21] == 3725 ? QuestText{} : entry(rec[21], rec[22], 3, 10, false);
    if (log_state == 0)
        return (game(13) || game(15)) && !b13 && !bit1 ? entry(3729, 3725, 3, -1, false) : QuestText{};
    if (log_state > 13) return {};
    if (quest == 1) {                                       // the Den (name 3714): its count
        QuestText text = entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
        if (log_state == 3 || log_state == 4) { if (quest_state.den_left >= 2) text.count = quest_state.den_left; else text.string = 3739; }
        return text;
    }
    int line = rec[std::size_t(2 * log_state + 1)];
    if ((!b13 && !bit1 && game(13)) || flag(14) || line == 3727) line = 3729;
    return entry(line, rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
}
// The quest log's acts (FUN_004a2220): an act's tab is there once the act
// before it is over (quests 7, 15, 23, 26 done; the fifth only in the
// expansion); a tab asked for that isn't falls back to the last that is.
[[nodiscard]] inline bool quest_act_open(const QuestBits& quest_bits, int act, bool expansion) {
    static constexpr std::array<int, 5> kActDone = { 0, 7, 15, 23, 26 };
    if (act <= 0) return act == 0;
    return act < 5 && (act < 4 || expansion) && quest::bit(quest_bits, kActDone[std::size_t(act)], 0);
}
[[nodiscard]] inline int quest_log_act(const QuestBits& quest_bits, int act) {
    act = act > 4 ? 0 : act;
    while (act > 0 && !quest_act_open(quest_bits, act, true)) --act;
    return act;
}
// Which quest the log opens on in a tab (FUN_004a3220): the one the Quest
// Log button was for (`pending`), else, in the table's order, the first
// with news (QuestText::marks with a changed state) or its done animation
// to play; else the one last clicked in this tab (`remembered`), else the
// first under way (shown 3). -1 none.
struct QuestLogPick { int slot = -1, shown = 2; bool fresh = false; };
[[nodiscard]] inline int quest_log_pick(const std::vector<QuestLogPick>& entries, int pending, int remembered) {
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
// The quest a message is about, for the Talk submenu's label: its name's
// string id (0x722678), 0 none.
[[nodiscard]] inline int quest_name(int message) {
    if (message >= 64 && message <= 80) return 3714;       // Den of Evil
    if (message >= 81 && message <= 96) return 3715;       // Sisters' Burial Grounds
    if (message >= 97 && message <= 126) return 3717;      // The Search for Cain
    if (message >= 127 && message <= 145) return 3718;     // The Forgotten Tower
    if (message >= 146 && message <= 165) return 3716;     // Tools of the Trade
    if (message >= 166 && message <= 184) return 3719;     // Sisters to the Slaughter
    return 0;
}

// The class's "find" lines (the client's FUN_004cc270, rows at 0x72a2c4):
// the first time in a game the player is in one of a row's levels, the
// line, while the row's quest is still to do (FUN_004a4180). `quest` is
// the flag word, the row's quest id through 0x723f38.
struct FindLine {
    std::array<int, 6> levels;   // 0-terminated
    int quest;
    const char* line;            // <class>_<line>
    int event;                   // its unit sound event
};
inline constexpr std::array<FindLine, 14> kFindLines{ {
    { { 17 }, 2, "act1_find_burial", 0x26 },
    { { 34, 35, 36, 37 }, 6, "act1_find_catacombs", 0x28 },
    { { 8 }, 1, "act1_find_den", 0x29 },
    { { 29, 30, 31 }, 6, "act1_find_jail", 0x2a },
    { { 26, 27 }, 3, "act1_find_monastery", 0x2b },
    { { 20, 21, 22, 23, 24, 25 }, 5, "act1_find_tower", 0x2c },
    { { 38 }, 6, "act1_find_tristram", 0x2e },
    { { 2, 3, 4, 5, 6, 7 }, 1, "act1_find_wilderness", 0x2f },
    { { 74 }, 13, "act2_find_arcane", 0x37 },
    { { 58 }, 14, "act2_find_clawviper", 0x38 },
    { { 110 }, 35, "act5_find_wilderness", 0x4c },
    { { 120 }, 39, "act5_find_mountaintop", 0x4e },
    { { 121 }, 38, "act5_find_nihlathak", 0x4d },
    { { 132 }, 40, "act5_find_worldstone", 0x4f },
} };
// The row whose levels hold `level`, or none.
[[nodiscard]] inline const FindLine* find_line(int level) {
    for (const auto& row : kFindLines)
        if (std::ranges::find(row.levels, level) != row.levels.end() && level != 0) return &row;
    return nullptr;
}

}  // namespace d2d::rules
