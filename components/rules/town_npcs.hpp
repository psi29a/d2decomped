// SPDX-License-Identifier: GPL-3.0-or-later
// Town NPCs' think: MonAI Npc (32, FUN_005e7130), the AI of every Act 1
// vendor (Akara, Charsi, Gheed, Kashya, Warriv). docs/research/re/town-npcs.md.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

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
    enum class Kind { stand, walk, mode } kind = Kind::stand;
    int frames = 0;           // stand
    int x = 0, y = 0;         // walk, level subtiles
    int fail = 15;
    int mode = 0;             // mode: 8 S1 .. 11 S4
    int face = -1;            // FUN_00648820, 64 directions; -1 none
    bool greet = false;       // unit sound 0x12 to the visitor (FUN_00553380): its greeting
};

// FUN_005dc5c0: the larger axis gap plus half the smaller.
[[nodiscard]] inline int npc_distance(int x, int y, int to_x, int to_y) {
    const int gap_x = std::abs(x - to_x), gap_y = std::abs(y - to_y);
    return gap_y < gap_x ? (gap_y + gap_x * 2) / 2 : (gap_x + gap_y * 2) / 2;
}

// FUN_005dc380, an NPC to another unit: each axis gap less the NPC's size
// (FUN_00620510: MonStats2 +8, 2 for the camp's), then ai_distance.
[[nodiscard]] inline int npc_reach(int x, int y, int to_x, int to_y, int size) {
    const int gap_x = std::max(std::abs(x - to_x) - size, 0), gap_y = std::max(std::abs(y - to_y) - size, 0);
    return gap_y < gap_x ? (gap_y + gap_x * 2) / 2 : (gap_x + gap_y * 2) / 2;
}

// FUN_005de4e0 (from FUN_005de6d0, mode 2 walk): a spot up to `most`
// subtiles toward (to_x, to_y), ending `keep` off, the step split between
// the axes by their gaps, then both raised together till they make it.
[[nodiscard]] inline std::pair<int, int> npc_step_toward(int x, int y, int to_x, int to_y, int distance, int most, int keep) {
    const int sign = distance < keep ? -1 : 1, step = std::min(std::abs(distance - keep), most);
    const int gap_x = std::abs(to_x - x), gap_y = std::abs(to_y - y), total = std::max(gap_x + gap_y, step);
    int step_x = 0, step_y = 0;
    if (total > 0) {
        step_x = gap_x * step / total; step_y = gap_y * step / total;
        while (step_x + step_y < step) { ++step_x; ++step_y; }
    }
    const int way_x = x < to_x ? 1 : to_x < x ? -1 : 0, way_y = y < to_y ? 1 : to_y < y ? -1 : 0;
    return { x + way_x * step_x * sign, y + way_y * step_y * sign };
}

// One think of a town NPC at subtile (x, y),
// MonStats row `class_id`, `modes` its MonStats2 mode bits (1 << 8 S1 ...),
// `current` its mode now, `visitor` the player it heeds. Draws only on its
// unit seed (+0x20). The classes FUN_005e7130 singles out (Cain 5, Ormus,
// Alkor, Jerhyn, Larzuk's 0x200) are none of Act 1.
inline NpcAct npc_think(NpcBrain& brain, Rng& seed, std::span<const NpcPoint> path, int x, int y, int class_id,
                        std::uint32_t modes, int current, const NpcVisitor& visitor = {}) {
    auto stand = [](int frames) { return NpcAct{ .frames = frames }; };
    auto walk = [](int to_x, int to_y, int fail) { return NpcAct{ .kind = NpcAct::Kind::walk, .x = to_x, .y = to_y, .fail = fail }; };
    // FUN_005e6800: the first think keeps the spot as home, stand 20.
    if (!brain.homed) {
        brain.homed = true;
        brain.home_x = x; brain.home_y = y;
        return stand(20);
    }
    // FUN_005e68f0, the visitor (town-npcs.md "The visitor").
    if (!visitor.talking && brain.held <= 0) {
        if (visitor.present) {
            if (visitor.distance < 3 || visitor.distance > 23) {   // close: stop, greet every 60 thinks
                auto act = stand(20);
                if (brain.greeted == 0) { brain.greeted = 60; act.greet = true; }
                else if (brain.greeted > 0) --brain.greeted;
                else brain.greeted = 0;
                return act;
            }
            if (npc_distance(x, y, brain.home_x, brain.home_y) > 16) {   // FUN_005e6860: home first
                brain.linger_x = brain.home_x; brain.linger_y = brain.home_y; brain.linger_count = 12; brain.linger_stand = 10;
                return stand(10);
            }
            const auto [to_x, to_y] = npc_step_toward(x, y, visitor.x, visitor.y, visitor.distance, visitor.distance < 5 ? visitor.distance - 2 : 3, 2);
            return walk(to_x, to_y, 15);
        }
    } else {
        // Talked to: while +0x14 is over 36, a walk to the scratch words
        // (+0x18, +0x1c) taken as a spot no path reaches (bugs.md 16), so
        // the think comes aidel (15) on; then stand 8 as it counts down.
        // ponytail: with +0x14 run out game.exe schedules no think (what
        // wakes it after the talk isn't traced); d2d thinks 8 frames on.
        const auto act = stand(brain.held > 36 ? 15 : 8);
        if (brain.held < 0) brain.held = 0;
        else --brain.held;
        return act;
    }
    // FUN_005e6ae0: a point being lingered at, one think each: back to it
    // from over 3 off, else stand.
    if (brain.linger_count > 0) {
        --brain.linger_count;
        if (brain.linger_x && brain.linger_y && npc_distance(x, y, brain.linger_x, brain.linger_y) > 3) return walk(brain.linger_x, brain.linger_y, 15);
        return stand(brain.linger_stand);
    }
    int face = -1;
    if (brain.special_mode) {                            // then the special mode at its point
        if (brain.special_mode < 8 || brain.special_mode > 11) { brain.special_mode = 0; return stand(50); }
        if (const int gap = npc_distance(x, y, brain.special_x, brain.special_y); gap > 0) {
            if (brain.special_tries > 0) { --brain.special_tries; return walk(brain.special_x, brain.special_y, 25); }
            if (gap > 1) brain.special_mode = 0;
        }
        if (class_id == 0x9a) face = 0x38;               // Charsi at her anvil
        if (class_id == 0x9b) face = 0x34;               // Warriv
        if (class_id == 0xb2) face = 4;                  // Fara
        if (class_id == 0x195) face = brain.special_mode == 8 ? 0x34 : brain.special_mode == 9 ? 0x30 : -1;   // Jamella
        if (class_id == 0x1ff && seed(100) > 4) { brain.special_mode = 0; auto act = stand(50); act.face = face; return act; }
        if (const int mode = brain.special_mode) {
            auto act = current == mode ? stand(50) : NpcAct{ .kind = NpcAct::Kind::mode, .mode = mode };
            if (current != mode) brain.special_mode = 0;
            act.face = face;
            return act;
        }
        if (class_id == 0xb2 && seed(100) < 66) brain.special_mode = 8;
    }
    // FUN_005e7080: 66 in 100 a random point of the path, its action
    // (table 0x741d74: 1 and 3 walk there and linger 12 thinks, 2 linger 20,
    // 4 / 5 then S1 / S2 there); else (or already there) stand 8.
    auto act = stand(8);
    if (!path.empty() && seed(100) < 66) {
        const auto& point = path[std::size_t(seed(int(path.size())))];
        if (point.action >= 1 && point.action <= 5 && point.x && point.y) {
            const bool moves = npc_distance(x, y, point.x, point.y) != 0;    // FUN_005e6de0
            if (moves) act = walk(point.x, point.y, 15);
            if (moves || point.action == 2 || point.action >= 4) {
                brain.linger_x = point.x; brain.linger_y = point.y;
                brain.linger_count = point.action == 2 ? 20 : 12; brain.linger_stand = 10;
            }
            if (point.action >= 4) {
                const int special = point.action == 4 ? 8 : 9;
                brain.special_mode = modes >> special & 1 ? special : 1;
                brain.special_x = point.x; brain.special_y = point.y; brain.special_tries = 4;
            }
        }
    }
    act.face = face;
    return act;
}

}  // namespace d2d::rules
