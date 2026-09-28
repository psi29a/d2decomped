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

}  // namespace d2d::rules
