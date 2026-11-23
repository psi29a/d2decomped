// SPDX-License-Identifier: GPL-3.0-or-later
// Town NPCs' think: MonAI Npc (32, FUN_005e7130), the AI of every Act 1
// vendor (Akara, Charsi, Gheed, Kashya, Warriv). docs/research/re/town-npcs.md.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace d2d::rules {

// A DS1 path point (AI control +0x38: 12-byte {action, x, y}), level subtiles.
struct NpcPoint { int action = 0, x = 0, y = 0; };

// The AI control's commands (FUN_0058efa0, a list at +0x20 by type): 10
// home, 4 linger at a point, 7 a special mode there.
struct NpcBrain {
    bool homed = false;
    int home_x = 0, home_y = 0;
    int linger_x = 0, linger_y = 0, linger_count = 0, linger_stand = 0;   // cmd 4: +0xc x, +0x10 y, +0x14 thinks, +0x18 stand
    int special_mode = 0, special_x = 0, special_y = 0, special_tries = 0; // cmd 7: +0xc mode, +0x10 x, +0x14 y, +0x18 walks
    int held = 0;             // AI control +0x14: 40 as a player starts talking (FUN_00548b00), one off a think
    int greeted = 0;          // AI control +0x18: thinks till the next greeting (60)
    int guard_frame = 0;      // JarJar: command 10's +0x18, the frame it last moved or heeded
    bool listed = false;      // d2d: a player was talking to it last frame (to see a talk start)
};

// The player an NPC heeds (FUN_005ddf20 / FUN_005dde80): one within 16
// (FUN_005dc380, `distance`) whose quests give the NPC something to say
// (FUN_00544590: its "!"); else none. `talking`: a player is in the NPC's
// interact list (FUN_00572dc0), or that one is busy talking (FUN_00535060).
struct NpcVisitor {
    bool present = false, talking = false;
    int x = 0, y = 0, distance = 0;
};

// What a think does. A walk that can't set off thinks again `fail` frames on
// (FUN_005a73e0's aidel, 15 for every NPC; 25 for a special's).
struct NpcAct {
    enum class Kind { stand, walk, mode, attack } kind = Kind::stand;   // attack: A1 at the monster it sees (FUN_005ddf90 mode 4)
    int frames = 0;           // stand; -1: no think scheduled (till a talk starts)
    int x = 0, y = 0;         // walk, level subtiles
    int fail = 15;
    int mode = 0;             // mode: 8 S1 .. 11 S4
    int face = -1;            // FUN_00648820, 64 directions; -1 none
    bool greet = false;       // unit sound 0x12 to the visitor (FUN_00553380): its greeting
};

// FUN_005dc5c0: the larger axis gap plus half the smaller.
[[nodiscard]] int npc_distance(int x, int y, int to_x, int to_y);

// FUN_005dc380, an NPC to another unit: each axis gap less the NPC's size
// (FUN_00620510: MonStats2 +8, 2 for the camp's), then ai_distance.
[[nodiscard]] int npc_reach(int x, int y, int to_x, int to_y, int size);

// FUN_005dde80, an NPC with the interact flags picking its visitor among the
// players (x, y subtiles; alert: the NPC has a "!" for them, FUN_00544590):
// one under 16 (npc_reach, size 2) with a "!" replaces the best only when
// strictly nearer, so the nearest wins, the first on a tie. -1: none.
struct NpcCandidate { int x = 0, y = 0; bool alert = false; };
[[nodiscard]] int npc_visitor_pick(int x, int y, std::span<const NpcCandidate> candidates);

// FUN_005de4e0 (from FUN_005de6d0, mode 2 walk): a spot up to `most`
// subtiles toward (to_x, to_y), ending `keep` off, the step split between
// the axes by their gaps, then both raised together till they make it.
[[nodiscard]] std::pair<int, int> npc_step_toward(int x, int y, int to_x, int to_y, int distance, int most, int keep);

// FUN_005e68f0, the visitor (town-npcs.md "The visitor"): what the NPC
// does about the player it heeds, if anything (the think goes on without).
std::optional<NpcAct> npc_heed(NpcBrain& brain, int x, int y, const NpcVisitor& visitor);

// One think of a town NPC at subtile (x, y),
// MonStats row `class_id`, `modes` its MonStats2 mode bits (1 << 8 S1 ...),
// `current` its mode now, `visitor` the player it heeds. Draws only on its
// unit seed (+0x20). The classes FUN_005e7130 singles out (Cain 5, Ormus,
// Alkor, Jerhyn, Larzuk's 0x200) are none of Act 1.
// `towner`: MonAI Towner (FUN_005e7540, Lut Gholein's townsfolk and
// guards): the same home, commands and wander, no visitor, stand 12.
NpcAct npc_think(NpcBrain& brain, Rng& seed, std::span<const NpcPoint> path, int x, int y, int class_id,
                 std::uint32_t modes, int current, const NpcVisitor& visitor = {}, bool towner = false);

// MonAI Vendor (42, FUN_005e9e00: Lut Gholein's market traders): one step
// of the unit seed, under 20 in 100 S1 (FUN_005ddfc0 mode 8), else stand 30.
NpcAct vendor_think(Rng& seed);

// MonAI JarJar (81, FUN_005e7590: the palace guard, act2guard2).
NpcAct jarjar_think(NpcBrain& brain, int x, int y, int frame, bool aside, bool stood_aside, const NpcVisitor& visitor);

// MonAI Navi (58, FUN_005e7e20: Flavie). `listed`: a player in her interact
// list (FUN_00572dc0); `visitor_busy`: the player she heeds is in an
// interaction (FUN_00535060); `near_flag`: her player search's flag word
// (under 4 off, or what the stack held: town-npcs.md "Flavie"); `enemy`: a monster under
// 25 off (FUN_005ddc30). Draws one rand(3) on her unit seed with a visitor.
NpcAct navi_think(NpcBrain& brain, Rng& seed, bool listed, const NpcVisitor& visitor, bool visitor_busy, bool near_flag, bool enemy);

// The town NPCs' return line (FUN_004e0590 kind 2). The first trip out of an act
// by its end (FUN_005467e0: Warriv, Meshif; FUN_00546ac0: the hellgate) sets a
// town's NPCs' bits (FUN_00544fa0) in the player's congratulations (the save's
// +0x2E5, per difficulty). Coming into an act (FUN_00537340 -> FUN_00545100),
// the client is told which of its town's NPCs have theirs set (S->C 0x91); each
// greets with its return line at its next menu, and the client's C->S 0x4d
// clears the bit (FUN_005724c0). Tyrael's portal marks none (FUN_005b5880), and
// FUN_005467e0's Tyrael branch can't run (bugs.md #116): Act 5's list never is.
struct NpcReturns {
    // 0x732738 {MonStats class, bit} (the intro bits' too); any other class bit 0.
    static constexpr std::array<std::pair<int, int>, 34> kBits{ { { 147, 1 }, { 148, 2 }, { 150, 3 }, { 155, 4 }, { 154, 5 }, { 265, 6 }, { 175, 7 },
        { 176, 8 }, { 177, 9 }, { 178, 10 }, { 202, 11 }, { 200, 12 }, { 210, 13 }, { 201, 14 }, { 198, 15 }, { 199, 16 }, { 244, 17 }, { 245, 18 },
        { 246, 19 }, { 251, 20 }, { 252, 21 }, { 253, 22 }, { 254, 23 }, { 255, 24 }, { 256, 25 }, { 257, 26 }, { 264, 27 }, { 297, 28 }, { 511, 29 },
        { 512, 30 }, { 513, 31 }, { 514, 32 }, { 515, 33 }, { 520, 34 } } };
    [[nodiscard]] static int bit_of(int npc) {
        const auto found = std::ranges::find(kBits, npc, &std::pair<int, int>::first);
        return found == kBits.end() ? 0 : found->second;
    }
    // The towns' lists by act (0x7318b0, 0x7318c8, 0x7318f4, 0x731910); Act 4 has none.
    [[nodiscard]] static std::span<const int> town(int act) {
        static constexpr std::array<int, 6> kAct1{ 148, 154, 150, 147, 155, 265 };
        static constexpr std::array<int, 11> kAct2{ 175, 176, 177, 178, 202, 200, 210, 201, 198, 199, 244 };
        static constexpr std::array<int, 7> kAct3{ 245, 252, 253, 254, 255, 264, 297 };
        static constexpr std::array<int, 7> kAct5{ 520, 521, 511, 512, 513, 514, 515 };
        switch (act) {
        case 0: return kAct1;
        case 1: return kAct2;
        case 2: return kAct3;
        case 4: return kAct5;
        default: return {};
        }
    }
    static void mark(std::uint64_t& bits, int act) {
        for (const int npc : town(act)) bits |= std::uint64_t{ 1 } << bit_of(npc);
    }
    // FUN_00545090: those of the town with their bit set, in the list's order.
    [[nodiscard]] static std::vector<int> due(std::uint64_t bits, int act) {
        std::vector<int> owed;
        for (const int npc : town(act)) if (bits >> bit_of(npc) & 1) owed.push_back(npc);
        return owed;
    }
    static void heard(std::uint64_t& bits, int npc) { bits &= ~(std::uint64_t{ 1 } << bit_of(npc)); }
};

}  // namespace d2d::rules
