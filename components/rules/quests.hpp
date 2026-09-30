// Quests: the save's quest flags and the Den of Evil (game.exe 1.14d
// D2Game/Quests/a1q1.cpp, docs/research/re/quests.md).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::rules {

// A difficulty's quest flags (the d2s block's 96 bytes): quest q's bit b
// is bit q * 16 + b (FUN_0065c310 / 0065c360 / 0065c3a0).
using QuestBits = std::array<std::uint8_t, 96>;
[[nodiscard]] inline bool qbit(const QuestBits& quest_bits, int quest, int bit) {
    const int bit_index = quest * 16 + bit;
    return quest_bits[std::size_t(bit_index >> 3)] >> (bit_index & 7) & 1;
}
inline void qset(QuestBits& quest_bits, int quest, int bit, bool set = true) {
    const int bit_index = quest * 16 + bit;
    const auto mask = std::uint8_t(1u << (bit_index & 7));
    auto& byte = quest_bits[std::size_t(bit_index >> 3)];
    byte = std::uint8_t(set ? byte | mask : byte & ~mask);
}

// A quest message an NPC has for the player (FUN_00543790): its string.tbl
// id; `greet` plays as the NPC is clicked (kind 0), else it's a talk topic
// (kind 2) under the quest's name.
struct QuestMsg { int string = 0; bool greet = false; };

// The Den of Evil, one game's (the quest record at FUN_00590720). Quest 1
// in the flags; bits: 0 done, 1 reward due, 2 given, 3 / 4 in the Den,
// 13 cleared in this game, 14 cleared by someone else, 15 closed.
// ponytail: one player — the party and late-joiner lists are left out,
// and the quest log's state (+0xb) is kept only for the bit it picks.
struct DenQuest {
    static constexpr int kQuest = 1, kAkara = 148, kDen = 8;
    int state = 1;      // +0xc: 1 not given, 2 given, 3 in the Den, 4 cleared, 5 rewarded
    int log = 0;        // +0xb

    // A player's bits for the state (LAB_0058fc90).
    void mark(QuestBits& quest_bits) const {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 1)) return;
        if (state == 2) qset(quest_bits, kQuest, 2);
        if (state == 3) qset(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins: the state from their flags (LAB_00590690).
    void join(const QuestBits& quest_bits) {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 15)) return;
        if (qbit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (qbit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (qbit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        struct E { int npc, string; bool greet; };
        static const std::vector<E> kBlocks[5] = {
            { { 148, 64, true } },
            { { 148, 65, false }, { 155, 70, false }, { 147, 69, false }, { 150, 66, false }, { 154, 67, false } },
            { { 150, 72, false }, { 155, 75, false }, { 154, 73, false }, { 148, 71, false }, { 147, 74, false } },
            { { 150, 77, false }, { 155, 80, false }, { 154, 78, false }, { 148, 76, true }, { 147, 79, false } },
            { { 155, 80, false }, { 154, 78, false }, { 147, 79, false } },
        };
        int block = 3;
        if (!qbit(quest_bits, kQuest, 1)) {
            if (qbit(quest_bits, kQuest, 0) || (state > 3 && !qbit(quest_bits, kQuest, 13))) return {};
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
        if (npc != kAkara || qbit(quest_bits, kQuest, 0)) return false;
        return (state == 1 && !qbit(quest_bits, kQuest, 1)) || qbit(quest_bits, kQuest, 1);
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
        if (!qbit(quest_bits, kQuest, 0) && !qbit(quest_bits, kQuest, 1)) { qset(quest_bits, kQuest, 13); qset(quest_bits, kQuest, 1); }
        return Kill::cleared;
    }
    // The player heard `string` from `npc` (FUN_0058fdd0, packet 0x31).
    // True: Akara's reward — a skill point (stat 5 + 1), done; quest 41
    // (her respec) opens; bits 2..11 cleared (FUN_0065c3e0).
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc != kAkara) return false;
        if (string == 64) { state = 2; mark(quest_bits); return false; }
        if (string != 76 || !qbit(quest_bits, kQuest, 1)) return false;
        if (qbit(quest_bits, kQuest, 13) && state != 5) state = 5;
        qset(quest_bits, kQuest, 0);
        qset(quest_bits, kQuest, 1, false);
        qset(quest_bits, 41, 13);
        qset(quest_bits, 41, 1);
        for (int bit = 2; bit < 12; ++bit) qset(quest_bits, kQuest, bit, false);
        return true;
    }
};

// Sisters to the Slaughter, one game's (a1q6.cpp, the record at
// FUN_00596990). Quest 6; bits: 0 done, 1 reward due, 2 given, 3 / 4 in
// the Catacombs, 13 took part, 14 cleared by someone else, 15 closed.
// ponytail: one player — the Cain / Akara / Kashya lists (+0x000 / +0x084
// / +0x108) and the rewarded list (rec+0x1c) are a bool each for them.
struct AndyQuest {
    static constexpr int kQuest = 6, kCain = 265, kAkara = 148, kKashya = 150, kWarriv = 155, kAndariel = 156, kLair = 37, kLut = 40;
    int state = 0;      // +0xc: 0 init, 1 available, 2 Cain gave it, 3 Catacombs, 4 Andariel dead, 5 done
    int log = 0;        // +0xb
    int start_in = 20;  // ticks to the start timer (0x5968e0: 0x14, then 0x596580)
    int after_kill = 0; // +0x192: ticks since Andariel died (timer 0x596500), 0 not running
    bool cain = false, akara = false, kashya = false, rewarded = false;
    bool cain_pending = false;   // +0x195: Cain gave it, the log waits for the talk to close

    // A player's bits for the state (0x595bd0).
    void mark(QuestBits& quest_bits) const {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 1)) return;
        if (state == 2) qset(quest_bits, kQuest, 2);
        if (state == 3) qset(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins (0x596900).
    void join(const QuestBits& quest_bits) {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 15)) return;
        if (qbit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
        else if (qbit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
        else if (qbit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
    }
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const {
        struct E { int npc, string; bool greet; };
        static const std::vector<E> kBlocks[5] = {
            { { kCain, 166, true } },
            { { kAkara, 168, false }, { kKashya, 172, false }, { 154, 169, false }, { kCain, 167, false }, { 147, 170, false }, { kWarriv, 171, false } },
            { { kKashya, 178, false }, { kWarriv, 177, false }, { 147, 175, false }, { kCain, 173, false }, { 154, 176, false }, { kAkara, 174, false } },
            { { kKashya, 181, true }, { kCain, 184, true }, { 154, 180, false }, { 147, 182, false }, { kWarriv, 183, true }, { kAkara, 179, true } },
            { { kKashya, 181, false }, { kCain, 184, false }, { 154, 180, false }, { 147, 182, false }, { kWarriv, 183, false }, { kAkara, 179, false } },
        };
        const bool b0 = qbit(quest_bits, kQuest, 0), b13 = qbit(quest_bits, kQuest, 13);
        const bool listed = (npc == kCain && cain) || (npc == kAkara && akara) || (npc == kKashya && kashya);
        const bool cak = npc == kCain || npc == kAkara || npc == kKashya;
        int block = -1;
        if (listed) block = 3;
        else if (qbit(quest_bits, kQuest, 1)) block = cak ? 4 : 3;
        else if (rewarded) block = 4;
        else if (state == 1 && npc == kCain && !b0) block = 0;
        else if (state != 0 && !(b0 && !b13) && !(state >= 4 && !b13) && !(b0 && b13)) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block <= 4)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_005967f0).
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const {
        const bool b0 = qbit(quest_bits, kQuest, 0), b1 = qbit(quest_bits, kQuest, 1);
        if (npc == kCain) return cain || (!b0 && state == 1 && !b1);
        if (npc == kWarriv) return !b0 && b1;
        return (npc == kAkara && akara) || (npc == kKashya && kashya);
    }
    // The player heard `string` from `npc` (FUN_00595c60). True: Warriv's
    // reward — done (bit 0), and the quest's complete message.
    bool said(QuestBits& quest_bits, int npc, int string) {
        if (npc == kCain && string == 166) { state = 2; cain_pending = true; mark(quest_bits); }
        if (npc == kCain && string == 184) cain = false;
        if (npc == kAkara && string == 179) akara = false;
        if (npc == kKashya && string == 181) kashya = false;
        if (npc != kWarriv || string != 183 || !qbit(quest_bits, kQuest, 1)) return false;
        if (qbit(quest_bits, kQuest, 13)) { log = 0xd; state = 5; }
        qset(quest_bits, kQuest, 1, false);
        qset(quest_bits, kQuest, 0);
        rewarded = true;
        return true;
    }
    // The talk with `npc` closed (0x595b80).
    void talk_closed(int npc) {
        if (npc == kCain && cain_pending) { log = 1; cain_pending = false; }
    }
    // The player went from level `from` to `to` (FUN_00596010).
    void enter(QuestBits& quest_bits, int from, int to) {
        if (to < 34 || to > 37) {
            if (state == 4 && to == kLut) state = 5;
            else if (from == 1 && state == 2 && !qbit(quest_bits, kQuest, 0) && !qbit(quest_bits, kQuest, 1)) { state = 3; mark(quest_bits); }
            return;
        }
        if (state <= 2) state = 3;
        if (to == kLair && log < 2) log = 1;
        mark(quest_bits);
    }
    // Andariel died (FUN_005965a0). True: the player's kill for the quest
    // (bits 13 and 1, FUN_00596210) — Act 2 opens and her quest drop is
    // due: two chipped gems, one standard. `in_lair`: the player's in her
    // level (FUN_00596260: the lists).
    // ponytail: a merc's kill counts as the player's.
    bool killed(QuestBits& quest_bits, bool in_lair) {
        const bool first = !qbit(quest_bits, kQuest, 0) && !qbit(quest_bits, kQuest, 1);
        if (first) { qset(quest_bits, kQuest, 13); qset(quest_bits, kQuest, 1); }
        if (in_lair && !qbit(quest_bits, kQuest, 0) && !qbit(quest_bits, kQuest, 15)) cain = akara = kashya = true;
        after_kill = 1;
        state = 4;
        return first;
    }
    // Her quest drop's codes (0x7361dc chipped, 0x736444 standard), by lo % 7.
    static constexpr const char* kChipped[7] = { "gcv", "gcr", "gcb", "gcy", "gcg", "gcw", "skc" };
    static constexpr const char* kStandard[7] = { "gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku" };
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
    static constexpr int kQuest = 3, kCharsi = 154, kClvl = 8, kStand = 21;   // kStand: the stand's OperateFn
    int state = 0;           // +0xc: 0 closed, 1 open, 2 Charsi gave it, 3 out of town, 4 the malus dropped, 5 returned
    int log = 0;             // +0xb
    bool active = true;      // +9: off when the player joins with it done or closed (FUN_00546270)
    bool dropped = false;    // data+1 / +0x98 == 2: the stand's used
    bool given = false, returned = false;   // data[2] / data[3]: the log waits for the talk to close
    bool game_returned = false;             // the game's flag 13 (FUN_00544720(3, 0xd))

    // A player's bits for the state (0x591340).
    void mark(QuestBits& quest_bits) const {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 1)) return;
        if (state == 2) qset(quest_bits, kQuest, 2);
        if (state == 3 || state == 4) qset(quest_bits, kQuest, 3);
    }
    // The player joins (FUN_00591ed0); a first player with it done or
    // closed turns it off for the game (FUN_00546270, FUN_00544410).
    void join(const QuestBits& quest_bits) {
        if (qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 15)) { active = false; return; }
        if (qbit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
        else if (qbit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool holding, int clvl) const {
        struct E { int npc, string; bool greet; };
        static const std::vector<E> kBlocks[5] = {
            { { kCharsi, 146, true } },
            { { 148, 148, false }, { 150, 149, false }, { 265, 147, false }, { kCharsi, 150, false }, { 147, 151, false }, { 155, 153, false } },
            { { 150, 156, false }, { 155, 159, false }, { kCharsi, 157, false }, { 265, 154, false }, { 148, 155, false }, { 147, 158, false } },
            { { 150, 162, false }, { 155, 165, false }, { kCharsi, 163, true }, { 265, 160, false }, { 148, 161, false }, { 147, 164, false } },
            { { 150, 162, false }, { 155, 165, false }, { 265, 160, false }, { 148, 161, false }, { 147, 164, false } },
        };
        const bool b0 = qbit(quest_bits, kQuest, 0);
        if (b0 && !qbit(quest_bits, kQuest, 13)) return {};
        int block = -1;
        if (holding) { if (clvl >= kClvl && !b0) block = 3; }
        else if (!b0 && state != 0 && state != 4) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block <= 4)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The balloon over `npc` (FUN_00591c30): Charsi, while it's open or
    // the malus comes back at clvl 8+.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool holding, int clvl) const {
        if (npc != kCharsi || qbit(quest_bits, kQuest, 0)) return false;
        if (state == 1 && !qbit(quest_bits, kQuest, 1)) return true;
        return clvl >= kClvl && holding;
    }
    // The player heard `string` from `npc` (FUN_00591490). True: Charsi
    // took the malus back (the caller removes it, FUN_00544160) — the
    // imbue's due (bits 13 and 1) and "quest complete" (0xca7).
    bool said(QuestBits& quest_bits, int npc, int string, bool holding) {
        if (npc != kCharsi) return false;
        if (string == 146) { state = 2; given = true; return false; }
        if (string != 163 || qbit(quest_bits, kQuest, 0) || !holding) return false;
        qset(quest_bits, kQuest, 13);
        qset(quest_bits, kQuest, 1);
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
        if (!active || from != 1 || state != 2 || qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 1)) return;
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
        if (dropped || qbit(quest_bits, kQuest, 0) || qbit(quest_bits, kQuest, 1)) return Stand::none;
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
        if (qbit(quest_bits, kQuest, 6)) return false;
        qset(quest_bits, kQuest, 6);
        return true;
    }
    // Charsi imbued an item (FUN_00591790): done, the reward's used.
    void imbued(QuestBits& quest_bits) {
        qset(quest_bits, kQuest, 0);
        qset(quest_bits, kQuest, 1, false);
    }
    // The quest log's state (FUN_00591d30).
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool holding, int clvl) const {
        if (qbit(quest_bits, kQuest, 1)) return 10;
        if (holding) return qbit(quest_bits, kQuest, 0) ? 0 : 2;
        if (!active) return 0;
        if (qbit(quest_bits, kQuest, 13)) return 0xd;
        if (qbit(quest_bits, kQuest, 14)) return 0xc;
        if (state < 5) return log;
        return game_returned ? (clvl >= kClvl ? 12 : 4) : 0;
    }
};

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

}  // namespace d2d::rules
