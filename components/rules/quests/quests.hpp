// SPDX-License-Identifier: GPL-3.0-or-later
// Quests: the save's quest flags and the Den of Evil (game.exe 1.14d
// D2Game/Quests/a1q1.cpp, docs/research/re/quests.md).
#pragma once

#include "level_ids.hpp"
#include "monster_ids.hpp"
#include "object_ids.hpp"
#include "quest_clock.hpp"
#include "quest_log_send.hpp"

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
[[nodiscard]] bool bit(const QuestBits& quest_bits, int quest, int bit);
void set(QuestBits& quest_bits, int quest, int bit, bool set = true);
// A record's list of players by unit id (game.exe: up to 32 ids, the count
// at +0x80; FUN_005452c0 looks one up, FUN_00545240 drops one).
struct Players {
    std::vector<int> ids;
    [[nodiscard]] bool has(int player) const { return std::ranges::contains(ids, player); }
    void add(int player) { if (!has(player)) ids.push_back(player); }
    void remove(int player) { std::erase(ids, player); }
};
}  // namespace quest
// Akara's reset of the stat and skill points (the Den of Evil's reward):
// its flags are quest 41's (0 used, 1 open, 13 opened late).
inline constexpr int kRespecQuest = 41;

// A quest message an NPC has for the player (FUN_00543790): its string.tbl
// id; `greet` plays as the NPC is clicked (kind 0), else it's a talk topic
// (kind 2) under the quest's name.
struct QuestMsg { int string = 0; bool greet = false; };

// The quest log's state the server sends a player for a quest whose record has no log function of its own.
[[nodiscard]] int quest_log(const QuestBits& quest_bits, int quest, int state, int log, int done);

// The Den of Evil, one game's (the quest record at FUN_00590720). Quest 1
// in the flags; bits: 0 done, 1 reward due, 2 given, 3 / 4 in the Den,
// 13 cleared in this game, 14 cleared by someone else, 15 closed.
struct DenQuest {
    static constexpr int kQuest = 1, kAkara = monster_ids::kAkara, kDen = level_ids::kDenOfEvil;
    quest::Players participants;   // data list: each player who killed one of its monsters (FUN_00545200)
    int state = 1;      // +0xc: 1 not given, 2 given, 3 in the Den, 4 cleared, 5 rewarded
    int log = 0;        // +0xb
    bool active = true; // +9: off when the player joins with it done or closed (FUN_00546270)
    bool news = false;  // the log sent to each player (FUN_00544300 flag 1, FUN_0058fbe0) since the World last took it
    bool told = false;  // data +0x86: Akara gave it, the log waits for the talk to close
    bool closing = true;   // +0xa8: the talk-close hook, until it fires or the log moves on

    // A player's bits for the state (LAB_0058fc90).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins: the state from their flags.
    void join(const QuestBits& quest_bits);
    // The chain reached it (+0xf0, FUN_00590620). True: it passes on to
    // quest 2's (+0x10).
    [[nodiscard]] bool chain() const { return state == 5 || !active; }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // Into the Den.
    void enter_den(QuestBits& quest_bits);
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc) const;
    // Whether `npc` has something to say about it — the balloon over its head: Akara, before she's given it or
    // while the reward is due.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // A monster died at `player`'s hand (FUN_00590260: added to the list
    // unless there).
    void took_part(int player) { if (state < 4 && active) participants.add(player); }
    // A monster died; `remaining` are the Den's left (level +0x2cc - +0x2d0)
    // (FUN_00590260, at each death). 0: cleared (each player's credit
    // follows). Only once `all_up`, every populatable Den room populated (its
    // record +4 against FUN_00642be0's count). Under 6 the count is sent
    // (log 4, FUN_0058fbe0); at log 4 with 6 or more (a monster back up) it's
    // sent again, populated or not.
    enum class Kill { none, few, cleared };
    Kill killed(int remaining, bool all_up);
    // The clear's credit for one player neither done nor due: a participant bits 13 and 1, the reward due, any
    // other bit 14.
    static bool credit(QuestBits& quest_bits, bool listed);
    // The player heard `string` from `npc`.
    bool said(QuestBits& quest_bits, int npc, int string);
    // The talk with `npc` closed: after Akara gave it, log 1.
    void talk_closed(int npc);
    // The player left town for a level other than the Den: given, it's under way (state 3), log 1.
    void left_town(QuestBits& quest_bits);
};

// Sisters' Burial Grounds, one game's (a1q2.cpp, the record at
// FUN_00591210). Quest 2; bits: 0 done, 1 reward due, 2 given, 3 / 4 in
// the Burial Grounds, 13 took part in the kill, 14 killed by someone
// else, 15 closed.
// The kill's credit over the players (participants, the party, the rest)
// is World::blood_raven_died's (World::kill_credit).
struct BurialQuest {
    static constexpr int kQuest = 2, kKashya = monster_ids::kKashya, kBurial = level_ids::kBurialGrounds, kBloodRaven = monster_ids::kBloodRaven;
    bool open = true;   // +9: closed in this game when the player's done it (FUN_00546270)
    int state = 0;      // +0xc: 0 closed, 1 given out, 2 Kashya spoke, 3 Burial Grounds, 4 Blood Raven dead, 5 done
    int log = 0;        // +0xb
    int log_in = 0;     // quest steps to the kill's "return to Kashya" (timer 0xf, LAB_00590bf0), 0 not running
    bool news = false;  // the log sent to each player (FUN_00544300 flag 1, FUN_00590830) since the World last took it
    bool pending = false;    // data+3: Kashya gave it, the log waits for the talk to close
    quest::Players rewarded;   // in the +0x1c list: took the reward, still in town

    // A player's bits for the state (LAB_00590890).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins; a done or closed quest is shut for the game.
    void join(const QuestBits& quest_bits);
    // The chain reaches it: the Den's done or closed.
    bool chain();
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A quest-clock step (QuestClock): the kill's timer (LAB_00590bf0) sends log 3 (flag 1).
    void tick() {
        if (log_in > 0 && --log_in == 0) { log = 3; news = true; }
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // The balloon over `npc`.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The player heard `string` from `npc`.
    bool said(QuestBits& quest_bits, int npc, int string, int player);
    // The talk with `npc` closed.
    void talk_closed(QuestBits& quest_bits, int npc);
    // The player went from level `from` to `dest`.
    void enter(QuestBits& quest_bits, int from, int dest, int player);
    // Blood Raven died.
    bool killed(QuestBits& quest_bits, bool nearby);
};

// Sisters to the Slaughter, one game's (a1q6.cpp, the record at
// FUN_00596990). Quest 6; bits: 0 done, 1 reward due, 2 given, 3 / 4 in
// the Catacombs, 13 took part, 14 cleared by someone else, 15 closed.
struct AndyQuest {
    static constexpr int kQuest = 6, kCain = monster_ids::kCampCain, kAkara = monster_ids::kAkara, kKashya = monster_ids::kKashya,
                         kWarriv = monster_ids::kWarriv, kAndariel = monster_ids::kAndariel;
    int state = 0;      // +0xc: 0 init, 1 available, 2 Cain gave it, 3 Catacombs, 4 Andariel dead, 5 done
    int log = 0;        // +0xb
    int start_in = 0;   // quest steps to the start timer (0x14, then 0x596580), 0 not running
    bool active = true; // +9: off when the player joins with it done or closed (FUN_00546270)
    int after_kill = 0; // +0x192: 1 at Andariel's death, +1 each time her timer (0x596500, 1 tick) runs; 0 not running
    int after_kill_steps = 0;   // quest steps since that timer last ran
    quest::Players cain, akara, kashya;   // data +0x000 / +0x084 / +0x108: players with her death's news for them (FUN_00596260)
    quest::Players rewarded;              // rec+0x1c
    bool cain_pending = false;   // +0x195: Cain gave it, the log waits for the talk to close
    bool news = false;           // the log sent to each player (FUN_00544300 flag 1, FUN_00595b20) since the World last took it

    // A player's bits for the state (0x595bd0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins.
    void join(const QuestBits& quest_bits);
    // The chain reached it (+0xf0, FUN_005968e0): the start timer runs. It
    // passes on no further (+0x10 is 0x25, the next act's).
    void chain() { if (state == 0 && active) start_in = quest_timer_steps(0x14); }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A quest-clock step (QuestClock).
    int tick();
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // The balloon over `npc`.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, int player) const;
    // The player heard `string` from `npc`.
    bool said(QuestBits& quest_bits, int npc, int string, int player);
    // The talk with `npc` closed.
    void talk_closed(int npc);
    // The player went from level `from` to `dest`.
    void enter(QuestBits& quest_bits, int from, int dest);
    // Andariel died.
    bool killed(QuestBits& quest_bits);
    // A player in her level at her death: Cain, Akara and Kashya have news for them, unless they're done or
    // closed.
    void in_lair(const QuestBits& quest_bits, int player);
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
// The bits are the game's flags (`game_bits`, game +0x10f4 +0xc,
// FUN_00544760); the record is the Seven Tombs' (FUN_00543640: +9 open,
// +0xc state).
struct PalaceSpots {
    bool outside = false, palace = false, guard = false;
    bool palace_west = false;          // the palace spot is x - 10 (else x + 15)
};
[[nodiscard]] PalaceSpots palace_spots(const QuestBits& game_bits, bool tombs_open, int tombs_state);

// Act 2's opening (a2q0.cpp, the record at FUN_00598810): quest 8. Jerhyn
// greets (253, FUN_005986b0) with his "!" (FUN_00598780) until the player
// has bit 0; hearing it sets that and the game's bit 13 (LAB_00598640), as
// a join with bit 0 does (LAB_005987d0). Cain greets a player who missed
// his rescue (quest 4 bit 14) and isn't in the record's list with 125
// (block 1), and drops them from quest 4's missed list (FUN_005940a0);
// hearing it adds them to the list (LAB_00598640, FUN_00545200).
struct ActTwoOpening {
    static constexpr int kQuest = 8, kJerhyn = monster_ids::kJerhyn, kGreeting = 253, kCain = monster_ids::kCainAct2, kRescuedByRogues = 125;
    quest::Players told;   // data +0: heard Cain's 125
    static void join(const QuestBits& quest_bits, QuestBits& game_bits) {
        if (quest::bit(quest_bits, kQuest, 0)) quest::set(game_bits, kQuest, 13);
    }
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player, quest::Players& cain_missed) const;
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) { return npc == kJerhyn && !quest::bit(quest_bits, kQuest, 0); }
    void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player);
};

// Radament's Lair (a2q1.cpp, the record at FUN_00599340): quest 9, given
// out from the start (state 1). Bits: 0 done, 1 reward due, 2 Atma told,
// 3 / 4 under way, 5 the Book of Skill due (cleared reading it), 13 took
// part, 14 killed by someone else, 15 closed.
// The kill's credit over the players (FUN_00598d40 the party's) is
// World::radament_died's; the chain on to the Seven Tombs (+0x10 13) is
// World's.
struct RadamentQuest {
    static constexpr int kQuest = 9, kAtma = monster_ids::kAtma, kLair = level_ids::kSewersLevel3, kRadament = monster_ids::kRadament;
    static constexpr int kInit = 304, kSuccess = 334;
    bool open = true;   // +9
    int state = 1;      // +0xc: 1 given out, 2 Atma spoke, 3 out of town, 4 Radament dead, 5 done
    int log = 0;        // +0xb
    int log_in = 0;     // quest steps to the kill's "return to Atma" (timer 0xc, LAB_00598f70)
    bool pending = false;          // data+8: Atma spoke, the log waits for the talk to close
    quest::Players rewarded;         // in the +0x1c list
    bool reward_closed = false;    // data+0x10: joined closed with the reward due
    bool news = false;             // the log sent to each player (FUN_00544300 flag 1, FUN_005988b0) since the World last took it

    // A player's bits for the state (LAB_005989e0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, log == 1 ? 3 : 4);
    }
    // The player joins.
    void join(QuestBits& quest_bits, QuestBits& game_bits, bool has_book);
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A quest-clock step (QuestClock): the kill's timer (LAB_00598f70) sends log 3 (flag 1).
    void tick() {
        if (log_in > 0 && --log_in == 0 && state == 4) { log = 3; news = true; }
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // Atma's "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The player heard `string` from `npc`.
    void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player);
    // The talk with `npc` closed.
    void talk_closed(QuestBits& quest_bits, int npc);
    // The player went from level `from`: out of town.
    void enter(QuestBits& quest_bits, int from, int player);
    // Radament died (FUN_00599020). The books to drop at him (one each for a
    // player due one without one, LAB_00598fa0); `took_part` (FUN_00598e20:
    // the player's room is the killer's or next to it) also plays the
    // class's act2_complete_radament (voice 0x32).
    struct Kill { int books = 0; bool took_part = false; };
    Kill killed(QuestBits& quest_bits, QuestBits& game_bits, bool nearby, bool has_book);
    // The Book of Skill read: only while bit 5 is set; it clears, and a skill point (stat 5).
    static bool read_book(QuestBits& quest_bits);
};

// The Horadric Staff (a2q2.cpp, the record at FUN_0059eb00): quest 10, all
// Cain's (Act 2's, 244) and the items the player carries: the scroll
// (tr1), the shaft (msf), the Cube (box), the amulet (vip), the staff (hst).
// Bits: 0 done (the staff in the orifice: the Seven Tombs'), 1 reward due,
// 3 Cain's read the scroll (or seen any part), 4 / 5 / 6 the amulet /
// shaft / Cube shown, 10 the staff shown, 13 / 14 done here / by others,
// 15 closed. Its log is its own (+0xe8, FUN_0059e630).
// The record counts the parts it made (data+0x18..0x24) but nothing decides
// on them (their one reader, by the Cube's count, zeroes it). Its chests give
// a part to each player who wants one (World::operate).
struct HoradricStaffQuest {
    static constexpr int kQuest = 10, kCain = monster_ids::kCainAct2;
    static constexpr int kScrollChest = 40, kShaftChest = 41, kCubeChest = 39;   // OperateFns (objects.txt 355, 356, 354)
    struct Items { bool scroll = false, shaft = false, cube = false, amulet = false, staff = false; };
    bool open = true;              // +9
    quest::Players rewarded;         // in the +0x1c list
    bool shaft_dropped = false;    // data+0x28: the shaft chest gave one
    bool cube_dropped = false;     // data+0x29: the Cube chest gave one
    bool staff_made = false;       // data+0x2a: transmuted (FUN_0059e5c0; the player's bit 11)

    void join(QuestBits& quest_bits, const Items& items, bool staff_placed);
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, const Items& items, int player) const;
    // Cain's "!": the reward due, or a part to show.
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc, const Items& items);
    // The player heard `string` from Cain.
    bool said(QuestBits& quest_bits, int npc, int string, int player);
    // The log.
    [[nodiscard]] int log_state(const QuestBits& quest_bits, const Items& items, bool altar_used) const;
    // A quest chest opened (OperateFns 40 / 41 / 39.
    [[nodiscard]] std::string chest(int operate_fn, const QuestBits& quest_bits, const Items& items);
};

// The Arcane Sanctuary (a2q4.cpp, the record at FUN_0059f2a0): quest 12.
// Open once the Tainted Sun's done (the chain) or the staff's made
// (FUN_0059b660); Drognan (373) sends the player to Jerhyn, Jerhyn (377)
// to the palace; the guard (331) stands aside then; Horazon's Journal in
// the Sanctuary (OperateFn 42) finishes it and its 396 opens the way to the
// Canyon of the Magi. Bits: 0 done, 1 reward due, 2..5 under way (the
// mark), 7 / 8 the guard's two lines heard, 13 took part, 14 by others,
// 15 closed.
// The journal credits each player in the Sanctuary (World::operate). The
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
    bool news = false;         // the log sent to each player (FUN_00544300 flag 1, FUN_0059ad50) since the World last took it
    quest::Players rewarded;

    // A player's bits for the state (LAB_0059adb0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3) quest::set(quest_bits, kQuest, 3);
        if (state == 4 && (log == 2 || log == 3)) quest::set(quest_bits, kQuest, 4);
        if (state == 4 && log == 4) quest::set(quest_bits, kQuest, 5);
    }
    // A joined client's palace guard on his "!".
    [[nodiscard]] static bool guard_halts(bool talk_open, const QuestBits& quest_bits, bool harem_open);
    static constexpr int kGuardHalt = 3983, kHaremBlocker = 318;
    // The guard stands aside (FUN_0059aef0).
    void let_in(QuestBits& quest_bits) {
        guard_in = true;
        if (guard_state == 0) guard_new = true;
        guard_state = 2;
        mark(quest_bits);
    }
    // The chain reached it (LAB_0059b500): open; it passes on (+0x10 13).
    bool chain() { if (state == 0 && open) state = 1; return true; }
    // The Horadric Staff made.
    void staff_made(QuestBits& quest_bits);
    // The player joins.
    void join(const QuestBits& quest_bits, QuestBits& game_bits, bool has_staff);
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, Roll&& roll, int player) const {
        if (npc == kGuard) return { { guard_in ? 187 + int(roll() % 3) : 186, true } };
        int block = -1;
        if (quest::bit(quest_bits, kQuest, 1)) block = 4;
        else if (rewarded.has(player)) block = 5;
        else if (state != 0 && open && !quest::bit(quest_bits, kQuest, 0) && !(state > 4 && !quest::bit(quest_bits, kQuest, 13))) block = state - 1;
        std::vector<QuestMsg> out;
        if (block >= 0 && block < 6)
            for (const auto& entry : kBlocks[block]) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
        return out;
    }
    // The "!".
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The player heard `string` from `npc`.
    bool said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player_room, int player);
    // The talk with `npc` closed.
    void talk_closed(QuestBits& quest_bits, int npc);
    // The player went from `from` to `dest`.
    void enter(QuestBits& quest_bits, int from, int dest, int player);
    // Horazon's Journal read: its 396 to the player, its room kept.
    bool journal(int room);
    // A player's credit at the first read: in the Sanctuary without bit 0 / 1, 13, 1, 0; else bit 14.
    static void journal_credit(QuestBits& quest_bits, bool in_sanctuary);
};

// The Summoner (a2q5.cpp, the record at FUN_0059c280): quest 13. Opened
// when he first goes for a player (FUN_005f85c0 -> FUN_0059c330); his
// death credits the players by him; the town's thanks finish it. Bits: 0
// done, 1 reward due, 2 under way, 13 took part, 14 by others, 15 closed.
// The kill's credit over the players is World::act2_kill's (kill_credit).
struct SummonerQuest {
    static constexpr int kQuest = 13, kSummoner = monster_ids::kSummoner, kSanctuary = level_ids::kArcaneSanctuary;
    static constexpr int kThanksFirst = 419, kThanksLast = 429;
    bool open = true;
    int state = 0;             // +0xc: 1 under way, 2 he's dead, 3 done
    int log = 0;
    bool started = false;      // data+1
    int log_in = 0;            // data+8: the death's 3-tick timer (LAB_0059bfd0), in quest steps
    bool voiced = false;       // data+9: its first run, the voice, done; the next sets the log
    bool news = false;         // the log sent to each player (FUN_00544300 flag 1, FUN_0059bcb0) since the World last took it
    quest::Players rewarded;
    void mark(QuestBits& quest_bits) const {   // LAB_0059bd10
        if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && state == 1) quest::set(quest_bits, kQuest, 2);
    }
    void engaged(QuestBits& quest_bits);
    void join(const QuestBits& quest_bits) {   // LAB_0059c220
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { open = false; return; }
        if (quest::bit(quest_bits, kQuest, 2)) { log = 2; state = 1; }
    }
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 2); }
    // He died.
    bool killed(QuestBits& quest_bits, bool nearby);
    // A quest-clock step.
    bool tick();
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[3] = {
        { { 176, 414, false }, { 175, 413, false }, { 198, 408, false }, { 199, 417, false }, { 177, 410, false }, { 202, 411, false },
          { 244, 418, false }, { 210, 412, false }, { 201, 409, false }, { 200, 415, false }, { 178, 416, false } },
        { { 176, 427, true }, { 175, 424, true }, { 198, 419, true }, { 199, 423, true }, { 177, 422, true }, { 202, 426, true },
          { 244, 429, true }, { 210, 425, true }, { 201, 421, true }, { 200, 420, true }, { 178, 428, true } },
        { { 176, 427, false }, { 175, 424, false }, { 198, 419, false }, { 199, 423, false }, { 177, 422, false }, { 202, 426, false },
          { 244, 429, false }, { 210, 425, false }, { 201, 421, false }, { 200, 420, false }, { 178, 428, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    [[nodiscard]] static bool alert(const QuestBits& quest_bits, int npc) {   // FUN_0059bb40
        static constexpr std::array<int, 10> kTown{ 176, 175, 199, 177, 202, 244, 210, 201, 200, 178 };
        return std::ranges::find(kTown, npc) != kTown.end() && quest::bit(quest_bits, kQuest, 1);
    }
    void said(QuestBits& quest_bits, QuestBits& game_bits, int string, int player);
    void enter(int from, int dest, int player) { if (open && dest >= level_ids::kLutGholein && from == level_ids::kLutGholein) rewarded.remove(player); }   // LAB_0059c200
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
// Duriel's death marks each player in the Lair (World::act2_kill). The
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
    int log_in = 0;            // Duriel's death's 8-tick timer (LAB_0059cee0), in quest steps
    bool news = false;         // the log sent to each player (FUN_00544300 flag 1, FUN_0059c650) since the World last took it
    quest::Players rewarded;
    void mark(QuestBits& quest_bits) const {    // FUN_0059c6c0
        if (!quest::bit(quest_bits, kQuest, 0) && state > 1) quest::set(quest_bits, kQuest, 2);
    }
    bool chain() { if (state == 0 && open) state = 1; return true; }   // FUN_0059d450, on to +0x10's
    // A later player joins (+0xd8, LAB_0059d4d0): their word 10 bit 9 cleared.
    static void join_more(QuestBits& quest_bits) { quest::set(quest_bits, 10, 9, false); }
    void join(QuestBits& quest_bits);
    // The log.
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const;
    // The player went from `from` to `dest`: levels 40..74.
    void enter(QuestBits& quest_bits, int from, int dest, int player);
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool summoner_done, int player) const;
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // What hearing `string` from `npc` does (FUN_0059cb20).
    struct Heard { bool town_portal = false, act3 = false, take_staff = false; };
    Heard said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, bool in_lair, int player);
    void talk_closed(QuestBits& quest_bits, int npc);
    // Duriel died.
    void duriel_died(QuestBits& quest_bits, bool by_him);
    // The way down's timer (FUN_0059dd80): (Range - 75) / 20 ticks of
    // Missiles.txt row 338 horadricstaff (+0x96; Range 440: 18), so the
    // hole opens some 75 frames before the staff's missile is done.
    static constexpr int staff_ticks(int horadricstaff_range) { return (horadricstaff_range - 75) / 20; }
    // The staff into the orifice.
    void staff_in(QuestBits& quest_bits, int ticks);
    // The orifice's room comes up.
    bool orifice_seen();
    // A quest-clock step.
    bool tick();
};

// The Tainted Sun (a2q3.cpp, the record at FUN_0059eea0): quest 11. The
// sun goes out a while after a player first reaches the Lost City or the
// Valley of Snakes; the altar in the Claw Viper Temple's second level
// brings it back. Bits: 0 done, 1 reward due, 2 the dark seen, 3 Drognan
// told, 4 in the temple, 13 took part, 14 done by someone else, 15 closed.
// The altar's share for the other players (its room's, their party's) and
// their amulets are World::operate's.
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
    int log_in = 0;     // the altar's timer (10 ticks, LAB_0059a700): log 3; in quest steps
    bool news = false;  // the log sent to each player (FUN_00544300 flag 1, FUN_00599f50) since the World last took it
    quest::Players rewarded;

    // A player's bits for the state (LAB_0059a4f0).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state >= 1) quest::set(quest_bits, kQuest, 2);
        if (state == 2) quest::set(quest_bits, kQuest, 3);
        if (state == 3) quest::set(quest_bits, kQuest, 4);
    }
    // The sun goes out (FUN_0059a350): log 1, given out, the act's sky
    // dark (each player gets S->C 0x53 {5, 0, 1}, FUN_0059a100). Its log 1
    // is flag 1 (0x59a364): the callers raise news, but for the join's.
    void darken() {
        log = 1;
        if (state == 0) state = 1;
        dark = true;
    }
    // The player joins; done or closed: shut for the game.
    void join(const QuestBits& quest_bits, QuestBits& game_bits);
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // The player went from level `from` to `dest`.
    bool enter(int from, int dest, int player);
    // A quest-clock step (QuestClock).
    void tick(QuestBits& quest_bits);
    // The altar's room comes up: in the temple, dark if not yet; log 2 if none.
    void altar_seen();
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // The "!": the reward due, over the town's people; or Drognan while it's new.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc) const;
    // The player heard `string` from `npc`: Drognan's 348 tells it; any thanks (362..372) with the reward due is
    // done.
    void said(QuestBits& quest_bits, QuestBits& game_bits, int npc, int string, int player);
    // The altar used (OperateFn 24, FUN_0059a7e0). `refused`: the player has
    // it (bit 0 / 1) and quest 10's done or carries the Viper Amulet / the
    // staff: "I can't" (voice 0x13). Otherwise once: the sun back (S->C 0x53
    // {2, 0, 0}), state 4, the player's credit (bits 1 and 13, 14 off; the
    // altar's room too, FUN_0059a680), a Viper Amulet for each player
    // without one or the staff or quest 10 done (LAB_0059a770), the game's
    // bit 13, the 10-tick log timer.
    struct Use { bool refused = false, used = false, took_part = false; int amulets = 0; };
    Use use_altar(QuestBits& quest_bits, QuestBits& game_bits, bool has_amulet, bool has_staff);
    // A player's share of the altar (the operator's, one in its room or
    // their party's): bits 1 and 13, 14 off, unless done. True: bit 13.
    static bool credit(QuestBits& quest_bits) {
        if (!quest::bit(quest_bits, kQuest, 0)) {
            quest::set(quest_bits, kQuest, 1);
            quest::set(quest_bits, kQuest, 13);
            quest::set(quest_bits, kQuest, 14, false);
        }
        return quest::bit(quest_bits, kQuest, 13);
    }
    // Whether a player gets a Viper Amulet from it (LAB_0059a770).
    static bool wants_amulet(const QuestBits& quest_bits, bool has_amulet, bool has_staff) { return !has_amulet && !has_staff && !quest::bit(quest_bits, 10, 0); }
};

// Quest 4; bits: 0 done, 1 reward due, 2 Akara told, 3 under way, 4 the
// stones done, 10 the Cow King dead, 13 took part in the rescue, 14 missed
// it, 15 closed. Fields are the record's data (rec+0x18) bytes.
// Left out: the party's bits (stage B of docs/PONYTAIL_PLAN.md group 4),
// the voices and the quest-item-gone rewind (+0xc4, FUN_00592c80: d2d's
// scrolls only go by the quest); the missed rescue marks the player
// arriving in Lut Gholein, where FUN_00592b10 marks every player.
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
    bool spot_noted = false;    // +0x67: Cain's AI noted the spot (FUN_005944b0)
    int spot_x = 0, spot_y = 0; // +0x9c / +0xa0: it, in subtiles (FUN_005943b0 moves it on 3 and 3 when no room holds it)
    bool news = false;          // the log sent to each player (FUN_00544300 flag 1, FUN_005920d0) since the World last took it
    quest::Players missed, thanked;   // lists A (+0xb4: missed the rescue) and B (+0x138: heard Cain's thanks)
    quest::Players rewarded;          // rec+0x1c

    // The chain reached it: state 0 → 1.
    bool open();
    // The quest log's state (+0xd 6).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 6); }
    // A player's bits for the state (LAB_00592130).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        else if (state > 2 && state < 6) quest::set(quest_bits, kQuest, 3);
    }
    void join(const QuestBits& quest_bits, bool bks, bool bkd);
    // What `npc` says about it (FUN_00592580): blocks at 0x737668, by the
    // state (0x737648: -1, 0, 1, 2, 3, 4). Once the stones are done
    // (+0x4f) it first takes one bkd a player still carries
    // (FUN_00544160, +0x7c less one): World::act1_talk. Event 0 runs only
    // the talker's act's quests (FUN_00543d10 -> FUN_005438e0's act filter).
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool bks, int player) const;
    // The balloon over `npc`.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool bks, int player) const;
    // The player heard `string` from `npc` (FUN_00592250). decipher: Akara
    // takes the scroll (bks) for the deciphered one (bkd, FUN_005466b0 ilvl
    // 0, quality 2); ring: her reward, a ring (Normal ilvl 7 magic,
    // Nightmare 30 / Hell 60 rare). With bit 13, the game's flag (4, 13)
    // going on runs the chain (+0xf0).
    enum class Said { none, decipher, ring };
    Said said(QuestBits& quest_bits, int npc, int string, bool bks, int player);
    // The talk with `npc` closed.
    void talk_closed(QuestBits& quest_bits, int npc);
    // A Bark Scroll (bks / bkd, Items.txt quest 5: bound to this record)
    // into the player's inventory (event 4, FUN_00592e20): with +9, their
    // bits for the state (LAB_00592130). tools/emu/quest_item_events.py.
    void item_event(QuestBits& quest_bits) const { if (active) mark(quest_bits); }
    // The player went from level `from` to `dest`.
    bool enter(QuestBits& quest_bits, int from, int dest, int player);
    // Camp Cain's due (+0x52 set, +0x51 clear): FUN_005940e0 / FUN_00596de0 then try FUN_00592960.
    [[nodiscard]] bool camp_spawn() const { return camp_due && !camp_cain; }
    // FUN_00592960 found him a spot: he's come (+0x51 1, +0x52 0). With none he stays due.
    void camp_made() { camp_cain = true; camp_due = false; }
    // Not rescued before Act 2: the Rogues get him (FUN_00597310; the Lut
    // Gholein arrival's the same, with the tree's mode). FUN_00596ca0(0, 1)
    // resolves it; the players still in it missed it (FUN_00592b10).
    void rogues(QuestBits& quest_bits, int player) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1) || resolved || state >= 6) return;
        resolved = true;
        if (!camp_cain) camp_due = true;
        state = 7; log = 5; news = true;   // flag 1 (FUN_00596de0 0x596ff6, FUN_00597310 0x5973c4)
        rescued_flag = true;
        missed_it(quest_bits, player);
    }
    // FUN_00592b10's pass: a player neither done nor due missed the rescue.
    void missed_it(QuestBits& quest_bits, int player) {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        quest::set(quest_bits, kQuest, 14);
        missed.add(player);
    }
    // The Tree of Inifuss.
    bool tree(const QuestBits& quest_bits, bool has_scroll);
    void stone_order(std::uint32_t& low, std::uint32_t& high);
    // A Cairn Stone (objects.txt Id `stone`, OperateFn 9, FUN_00593710);
    // `fresh`: it's still mode 0. lit: it lights; portal: the fifth — the
    // bkd goes, CairnStones (missile 0x120) opens the way to Tristram.
    enum class Stone { none, lit, portal };
    Stone stone(QuestBits& quest_bits, int stone, bool bkd, bool fresh);
    // The stones as Stony Field comes up.
    bool stones_init();
    // The Gibbet, `fresh` its mode 0.
    bool gibbet(QuestBits& quest_bits, bool fresh);
    // The Gibbet's opened: Tristram Cain comes out at x + 3, y + 3 and walks to his own portal
    // (World::cain_walk).
    void rescued(bool spawned);
    // Tristram Cain went through his portal (FUN_005944f0: +0x91, +0x52):
    // camp Cain's due (FUN_005940e0 → FUN_00592960).
    void portal_entered() { camp_due = true; }
    // Cain's AI's first think (FUN_005e77a0 -> FUN_005944b0): the spot he stands at is noted once for the
    // record (+0x9c / +0xa0, flag +0x67): true then. False when it is noted already: the think takes
    // FUN_005944f0 instead (portal_entered), and the spot stays the one noted.
    bool note_spot(int x, int y) {
        if (spot_noted) { portal_entered(); return false; }
        spot_noted = true; spot_x = x; spot_y = y;
        return true;
    }
    // Cain's portal in Tristram (object 189.
    static int portal_closed_frame(int entered);
    // The portal's mode at `frame` from its opening, mode 4 at `closed` (portal_closed_frame.
    [[nodiscard]] static int portal_mode(int frame, int closed);
    // The Cube's Cow Portal.
    [[nodiscard]] static bool cow_portal_opens(const QuestBits& quest_bits, bool opened, bool expansion, int level_id);
};

// The Forgotten Tower, one game's (a1q5.cpp, the record at FUN_00595920).
// Quest 5; bits: 0 done (set at the kill: no reward talk), 2..6 progress,
// 13 took part, 14 not there.
// The kill's passes over the players are World::countess_died's (the
// cellar-5 list's pass, FUN_00594dd0, never fires: the first set bit 0).
// A record the first join closed (+9 clear: World::closed_at_join) has the
// tome start in mode 3 (InitFn 4, FUN_00595a00: World::act1_enter).
struct TowerQuest {
    static constexpr int kQuest = 5, kTower = level_ids::kForgottenTower, kCellar = level_ids::kTowerCellarLevel5, kCountess = 6, kTome = 127;   // kCountess: SuperUniques row
    // Her treasure's spawner (Missiles.txt towerchestspawner: Range,
    // Param1, Param2, Param3; LevRange 1 at the level FUN_005954f0 makes
    // it with, 1: kTreasureLife frames).
    static constexpr int kTreasureFrames = 400, kTreasureOpen = 150, kTreasureEvery = 2, kTreasureRadius = 5, kTreasureLife = kTreasureFrames + 1;
    int state = 0;      // +0xc: 0 init, 2 started, 3 cellar / out of town, 5 Countess dead (1 and 4 unused)
    int log = 0;        // +0xb
    int log_in = 0;     // quest steps to the kill's timer (0x5954c0, 7), 0 not running
    quest::Players told;   // list A (data+0x00): had their success talk
    quest::Players due;    // list B (data+0x34): killed her in cellar 5, the success talk due
    bool dead = false;  // d+0x118: rec+0xc0 is gone
    bool treasure = false;   // d+0x119: her treasure's spawners made
    std::vector<int> chests;   // d+0x68 (count +0x88): cellar 5's chests (npc indices) as their rooms came up, at most 8
    bool first_talk = false;   // d+0x11a
    bool tome_early = false;   // d+0x11b: the tome read while log == 0
    bool news = false;         // the log sent to each player (FUN_00544300 flag 1) since the World last took it

    // A player's bits for the state (LAB_00594890; 0x59494c by log).
    void mark(QuestBits& quest_bits) const {
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
        if (state == 2) quest::set(quest_bits, kQuest, 2);
        if (state == 3 && log >= 1 && log <= 4) quest::set(quest_bits, kQuest, log + 2);
    }
    // The player joins.
    void join(const QuestBits& quest_bits);
    // The chain reached it (+0xf0, FUN_00595240). True: it passes on to
    // quest 3's (+0x10).
    [[nodiscard]] bool chain() const { return state == 5; }
    // The quest log's state (+0xd 4).
    [[nodiscard]] int log_state(const QuestBits& quest_bits) const { return quest_log(quest_bits, kQuest, state, log, 4); }
    // A quest-clock step (QuestClock): the kill's timer sets the log to 13.
    void tick() {
        if (log_in > 0 && --log_in == 0 && state == 5) { log = 13; news = true; }   // Q05_TimerNotifyCompletion (0x5954c0): log 0xd, flag 1
    }
    // Whether a player is sent the log (FUN_00594830): not one done (bit 0; unlike the other act 1 records',
    // bit 15 isn't read) without bit 13 or 14.
    static bool log_goes_to(const QuestBits& quest_bits) {
        return !quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 13) || quest::bit(quest_bits, kQuest, 14);
    }
    // The Moldy Tome read, then its message 127 heard.
    void read_tome(QuestBits& quest_bits, int player);
    // What `npc` says about it (FUN_00594c50): blocks at 0x737ed8 by the
    // state (0x7382ac: -1, -1, 0, 1, 2, 3, -1).
    struct E { int npc, string; bool greet; };
    static inline const std::vector<E> kBlocks[4] = {
        { { 265, 131, false }, { 154, 129, false }, { 148, 130, false }, { 155, 132, false }, { 150, 133, false }, { 147, 128, false } },
        { { 150, 134, false }, { 155, 136, false }, { 154, 137, false }, { 148, 138, false }, { 265, 135, false }, { 147, 139, false } },
        { { 150, 140, true }, { 155, 141, true }, { 265, 145, true }, { 154, 144, true }, { 148, 143, true }, { 147, 142, true } },
        { { 150, 140, false }, { 155, 141, false }, { 265, 145, false }, { 154, 144, false }, { 148, 143, false }, { 147, 142, false } },
    };
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, int player) const;
    // The balloon over `npc`: the success talk's due, not with Warriv or Gheed.
    [[nodiscard]] bool alert(const QuestBits&, int npc, int player) const;
    // The player heard `string` from `npc` (FUN_00594960). True: the first
    // success talk after the kill — the chain opens (rec+0xf0, FUN_00595240
    // → quest 3's).
    bool said(QuestBits& quest_bits, int npc, int string, int player) {
        if (string == kTome) {                         // no NPC check
            bool changed = false;
            if (tome_early) {
                if (log == 0) { log = 1; changed = true; }               // FUN_00594960: log 1, flag 1
                if (log == 3) { log = 2; changed = true; if (state < 3) state = 3; }   // log 2, flag 1
                news = news || changed;
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
        if (due.has(player)) { due.remove(player); told.add(player); }
        return chain;
    }
    // The player went from level `from` to `dest`.
    bool enter(const QuestBits& quest_bits, int from, int dest, int player);
    // The Countess died.
    bool killed(QuestBits& quest_bits, bool in_cellar, int player);
};

// Tools of the Trade, one game's (a1q3.cpp, the record at FUN_00591f70).
// Quest 3; bits: 0 done (imbued), 1 reward due (the imbue), 2 given,
// 3 out of town / the malus dropped, 6 the malus picked up once, 13
// returned in this game, 14 returned by someone else, 15 closed. The
// malus ('hdm ') drops from its stand, object 108 in the Barracks.
// The carrier count (+0x9c) feeds only a log refresh when the last carrier
// leaves (event 9); d2d's log goes by what each player holds. Its reset
// when the last carrier loses the malus (0x5918d0) hangs off event 6,
// whose wrapper nothing calls (stubs.md). The participant list (FUN_00545200
// on a return) is only ever emptied by leaving.
struct ToolsQuest {
    static constexpr int kQuest = 3, kCharsi = monster_ids::kCharsi, kClvl = 8, kStand = operate_fn::kMalusStand;
    int state = 0;           // +0xc: 0 closed, 1 open, 2 Charsi gave it, 3 out of town, 4 the malus dropped, 5 returned
    int log = 0;             // +0xb
    bool active = true;      // +9: off when the player joins with it done or closed (FUN_00546270)
    bool dropped = false;    // data+1 / +0x98 == 2: the stand's used
    bool given = false, returned = false;   // data[2] / data[3]: the log waits for the talk to close
    bool game_returned = false;             // the game's flag 13 (FUN_00544720(3, 0xd))
    bool carried_in = false;                // data+0xa1: the player joined holding the malus
    int carriers = 0;                       // data+0x9c: the players who joined holding it
    bool news = false;                      // the log sent to each player (FUN_00544300 flag 1, FUN_005912e0) since the World last took it

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
        carriers = holding ? 1 : 0;
        if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 15)) { active = false; return; }
        if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
        else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    }
    // A later player joins: holding the malus, with +9 and not returned, a carrier more; the first, the stand
    // used: log 2.
    void join_more(bool holding);
    // The chain reached it: open (Charsi's "!").
    bool open();
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
    [[nodiscard]] std::vector<QuestMsg> talk(const QuestBits& quest_bits, int npc, bool holding, int clvl) const;
    // The balloon over `npc`: Charsi, while it's open or the malus comes back at clvl 8+.
    [[nodiscard]] bool alert(const QuestBits& quest_bits, int npc, bool holding, int clvl) const;
    // The player heard `string` from `npc`.
    bool said(QuestBits& quest_bits, int npc, int string, bool holding);
    // Another party member's share when the malus comes back: clvl 8+, neither done nor due: bits 13 and 1.
    static void party_credit(QuestBits& quest_bits, int clvl);
    // The talk with `npc` closed.
    void talk_closed(QuestBits& quest_bits, int npc);
    // The player left level `from`: out of town after Charsi gave it.
    void enter(QuestBits& quest_bits, int from);
    // The stand operated (OperateFn 21, FUN_00591ac0). drop: the malus
    // falls from it (FUN_00559a30) and it's used (mode 2); refuse: "I
    // can't" (FUN_00553380) — too low a level, or it's off in this game
    // (the stand goes to mode 2 all the same).
    enum class Stand { none, refuse, drop };
    Stand operate(QuestBits& quest_bits, int clvl);
    // The malus went into the inventory.
    bool picked_up(QuestBits& quest_bits);
    // Charsi imbued an item: done, the reward's used.
    void imbued(QuestBits& quest_bits);
    // The quest log's state.
    [[nodiscard]] int log_state(const QuestBits& quest_bits, bool holding, int clvl) const;
};

// The chain from `quest`: 1 → 2 → 4 → 3 → 6, 5 → 3.
void chain(int quest, DenQuest& den, BurialQuest& burial, CainQuest& cain, TowerQuest& tower, ToolsQuest& tools, AndyQuest& andy);

// The quest log (client side, QuestLog.cpp).
// What the log says about a quest (FUN_004a1950): its record (0x7237a4 on,
// 64 bytes a quest: name, the message to replay once done, the reward's
// state - 1 (-1 none), then {string, message} a log state: state s at
// [2s + 1], [2s + 2]; 3725 none), by the log state the server sent (s), the
// player's flags and the game's (FUN_004b32e0). `shown` (+0x266): 0 done,
// its animation still to play (bit 12 clear), 1 done, 2 hidden, 3 shown.
// The Den's states 3 / 4 add the count ("Monsters remaining: " N, 3739 for
// one or none).
inline constexpr std::array<std::array<std::uint16_t, 29>, 27> kQuestLogRecords = { {
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
    // Act 3, quests 17..22 (0x723ae4).
    { 930, 564, 0xffff, 968, 549, 969, 549, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3730, 3725, 3729, 3725,
      3725, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 931, 548, 0xffff, 970, 543, 971, 543, 972, 543, 973, 543, 974, 543, 975, 548, 976, 3725, 3725, 3725, 3725, 3725,
      3725, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 932, 587, 0xffff, 993, 3725, 977, 571, 978, 571, 979, 571, 980, 587, 981, 587, 3730, 3725, 3729, 3725, 3725, 3725,
      3725, 3725, 3725, 3725, 3728, 3725, 3727, 3725 },
    { 933, 3725, 4, 982, 3725, 983, 527, 984, 529, 985, 531, 986, 534, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      986, 534, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 934, 3725, 3, 994, 3725, 987, 594, 988, 594, 989, 594, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      989, 594, 3728, 626, 3727, 3725, 3726, 626 },
    { 935, 3725, 4, 995, 3725, 990, 628, 991, 628, 992, 628, 935, 628, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      3728, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    // Act 4, quests 25..27 (0x723c64 on; record 12, 929, at 0x723aa4 is no quest's).
    { 937, 675, 0xffff, 996, 670, 997, 670, 998, 670, 999, 675, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      999, 3725, 3728, 3725, 3727, 3725, 3726, 676 },
    { 938, 3725, 0xffff, 1004, 681, 1005, 681, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3725, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 939, 3725, 0xffff, 1000, 678, 1001, 678, 1002, 678, 1000, 679, 3725, 3725, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      3728, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    // Act 5, quests 35..40 (0x723d24 on).
    { 22618, 3725, 0xffff, 22619, 20077, 22620, 20077, 22621, 20077, 21786, 20090, 3725, 3725, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      21786, 20090, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 22622, 3725, 0xffff, 22623, 20096, 22624, 20104, 22625, 20096, 22626, 20096, 21789, 20096, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      22625, 20096, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 22627, 3725, 0xffff, 22628, 20116, 21788, 20116, 22629, 20116, 22630, 20116, 22631, 20116, 22632, 20116, 3730, 3725, 3725, 3725, 3725, 3725,
      22631, 20116, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 22633, 3725, 0xffff, 22634, 20137, 21787, 20137, 22635, 20137, 22636, 20137, 21790, 20148, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      21790, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 22637, 3725, 3, 22638, 20153, 22639, 20153, 22640, 20002, 22639, 20002, 3725, 3725, 3729, 3725, 3730, 3725, 3725, 3725, 3725, 3725,
      22639, 20002, 3728, 3725, 3727, 3725, 3726, 3725 },
    { 22641, 3725, 0xffff, 21792, 20169, 21881, 20169, 22643, 20169, 22644, 20169, 22645, 20169, 3725, 3725, 3725, 3725, 3725, 3725, 3725, 3725,
      3728, 3725, 3728, 3725, 3727, 3725, 3726, 3725 },
} };
// Whether the World sends a quest's log state itself, at its record's own FUN_00544300 flag-1 sites (each one
// FUN_00544190 -> S->C 0x5d, ev::QuestNews), so the client doesn't read a change in the View's log for one.
// Since 9817e18b, a2q2's pickup-only FUN_00599a30 send is an event too. The rest are act 1's 1 .. 6, act 2's 9
// and 11 .. 14, act 3's 0x11 and 0x13 .. 0x16 (not a3q2's 0x12: its talk-close log 1 keeps the View's change;
// its pickups are a separate event, World::item_event -> FUN_005b81c0), a4q2 .. a4q4
// (0x19 Izual, 0x1a Terror's End, 0x1b Hellforge) and act 5's 0x23 .. 0x28. A4q1's 0x18 keeps the View's change.
[[nodiscard]] constexpr bool quest_log_sent_by_event(int quest) {
    return (quest >= 0x1 && quest <= 0x6) || (quest >= 0x9 && quest <= 0xe) || quest == 0x11 || (quest >= 0x13 && quest <= 0x16)
        || (quest >= 0x19 && quest <= 0x1b) || (quest >= 0x23 && quest <= 0x28);
}
// Whether an act 1 .. 5 record's log send (FUN_00544300 flag 1) reaches a player, by their bits: the record's per-player callback.
[[nodiscard]] bool quest_log_goes_to(const QuestBits& quest_bits, int quest);
// `state` is what the picker keeps as the quest's last shown state
// (0x7bf380, -1 left as it was); `marks` when a change of it is news (+0x263:
// the open log picks that quest, FUN_004a3220).
struct QuestText { int string = 0, count = -1, speech = 0, shown = 2, state = -1; bool marks = false; };
struct QuestState {
    QuestLogStates log{};                  // the log state the server sent, by quest (the view's quest_log)
    QuestGameFlags game{};                 // the game's quest flags (the view's game_quests)
    int den_left = 0;                      // DAT_007bf2a4
    int arreat_left = 0;                   // DAT_007bf2ac: Rescue on Mount Arreat's soldiers left
    bool multiplayer = false;              // DAT_007a0610 != 0: any game type but single player's (a joined game)
};
QuestText quest_text(const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state);
// The quest log's acts.
[[nodiscard]] bool quest_act_open(const QuestBits& quest_bits, int act, bool expansion);
[[nodiscard]] int quest_log_act(const QuestBits& quest_bits, int act);
// Which quest the log opens on in a tab (FUN_004a3220): the one the Quest
// Log button was for (`pending`), else, in the table's order, the first
// with news (QuestText::marks with a changed state) or its done animation
// to play; else the one last clicked in this tab (`remembered`), else the
// first under way (shown 3). -1 none.
struct QuestLogPick { int slot = -1, shown = 2; bool fresh = false; };
[[nodiscard]] int quest_log_pick(const std::vector<QuestLogPick>& entries, int pending, int remembered);
// The quest a message is about, for the Talk submenu's label (FUN_0049f910):
// its name's string id from the {u16 message, u16 name} table at 0x722678
// (0x20f rows, as runs here), else 0xe8c.
[[nodiscard]] int quest_name(int message);

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
[[nodiscard]] const FindLine* find_line(int level);

}  // namespace d2d::rules
