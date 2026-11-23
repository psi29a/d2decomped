// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of town_npcs.hpp's functions; their notes are in the header.
#include "town_npcs.hpp"

#include "rules.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>

namespace d2d::rules {

int npc_distance(int x, int y, int to_x, int to_y) {
    const int gap_x = std::abs(x - to_x), gap_y = std::abs(y - to_y);
    return gap_y < gap_x ? (gap_y + gap_x * 2) / 2 : (gap_x + gap_y * 2) / 2;
}

int npc_reach(int x, int y, int to_x, int to_y, int size) {
    const int gap_x = std::max(std::abs(x - to_x) - size, 0), gap_y = std::max(std::abs(y - to_y) - size, 0);
    return gap_y < gap_x ? (gap_y + gap_x * 2) / 2 : (gap_x + gap_y * 2) / 2;
}

int npc_visitor_pick(int x, int y, std::span<const NpcCandidate> candidates) {
    int best = -1, best_distance = 16;
    for (std::size_t k = 0; k < candidates.size(); ++k)
        if (const int distance = npc_reach(x, y, candidates[k].x, candidates[k].y, 2); candidates[k].alert && distance < best_distance) {
            best = int(k);
            best_distance = distance;
        }
    return best;
}

std::pair<int, int> npc_step_toward(int x, int y, int to_x, int to_y, int distance, int most, int keep) {
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

std::optional<NpcAct> npc_heed(NpcBrain& brain, int x, int y, const NpcVisitor& visitor) {
    auto stand = [](int frames) { return NpcAct{ .frames = frames }; };
    if (!visitor.talking && brain.held <= 0) {
        if (!visitor.present) return std::nullopt;
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
        return NpcAct{ .kind = NpcAct::Kind::walk, .x = to_x, .y = to_y, .fail = 15 };
    }
    // Talked to: while +0x14 is over 36, a walk to the scratch words
    // (+0x18, +0x1c) taken as a spot no path reaches (bugs.md 16), so
    // the think comes aidel (15) on; then stand 8 as it counts down.
    // With +0x14 run out (0, or -1 a think after) it schedules no think
    // (FUN_005e68f0 returns 1 with no FUN_005de080): frames -1. NU has no
    // end event (mode table 0x6e2260) and the talk's end (FUN_0054b9f0 ->
    // FUN_00572f20) schedules none, so it stands till a talk starts
    // (FUN_00548b00 case 1: FUN_005417d0 event 2 next frame).
    const auto act = stand(brain.held > 36 ? 15 : brain.held > 0 ? 8 : -1);
    if (brain.held < 0) brain.held = 0;
    else --brain.held;
    return act;
}

NpcAct npc_think(NpcBrain& brain, Rng& seed, std::span<const NpcPoint> path, int x, int y, int class_id,
                 std::uint32_t modes, int current, const NpcVisitor& visitor, bool towner) {
    auto stand = [](int frames) { return NpcAct{ .frames = frames }; };
    auto walk = [](int to_x, int to_y, int fail) { return NpcAct{ .kind = NpcAct::Kind::walk, .x = to_x, .y = to_y, .fail = fail }; };
    // FUN_005e6800: the first think keeps the spot as home, stand 20.
    if (!brain.homed) {
        brain.homed = true;
        brain.home_x = x; brain.home_y = y;
        return stand(20);
    }
    // FUN_005e68f0, the visitor (town-npcs.md "The visitor").
    if (!towner)
        if (auto heeded = npc_heed(brain, x, y, visitor)) return *heeded;
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
    // (table 0x741d74: 1 and 3 walk there and linger 12 thinks, FUN_005e6de0
    // / FUN_005e6ef0; 2 linger 20 there or not, FUN_005e6e80; 4 / 5 then S1
    // / S2 there, mode 1 without it, 4 tries, FUN_005e6f00 / FUN_005e6fc0);
    // else (or already there) stand 8 (a towner 12).
    auto act = stand(towner ? 12 : 8);
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

NpcAct vendor_think(Rng& seed) {
    if (seed.next() % 100 < 20) return NpcAct{ .kind = NpcAct::Kind::mode, .mode = 8 };
    return NpcAct{ .frames = 30 };
}

// Talked to, stand 10. A player she heeds (FUN_005ddf20, the visitor pick;
// none hands back Flavie herself), not busy, rand(3) not 0 (drawn only
// then) and the search's flag set: her greeting timer (AI control +0x18)
// at 0 becomes 60 with the greeting (FUN_00553380(unit, 0x12, player)),
// else a negative one goes to 0 and a positive one counts down; stand 20.
// Else A1 at a monster under 25 (FUN_005ddc30, TownRogue's test), or stand
// 50. Checked against game.exe by tools/emu navi.py.
NpcAct navi_think(NpcBrain& brain, Rng& seed, bool listed, const NpcVisitor& visitor, bool visitor_busy, bool near_flag, bool enemy) {
    if (listed) return NpcAct{ .frames = 10 };
    if (visitor.present && !visitor_busy && seed(3) != 0 && near_flag) {
        NpcAct act{ .frames = 20 };
        if (brain.greeted == 0) {
            brain.greeted = 60;
            act.greet = true;
        } else {
            brain.greeted = std::max(brain.greeted, 0);
            if (brain.greeted > 0) --brain.greeted;
        }
        return act;
    }
    if (enemy) return NpcAct{ .kind = NpcAct::Kind::attack };
    return NpcAct{ .frames = 50 };
}

// Its home (AI control command 10: +0xc, +0x10; +0x18 the frame it last
// moved or heeded). Till he stands aside (FUN_0059b8b0: record 0xb's
// data+0x40 is 2, +0x18 / +0x19 clear): once 200 frames, back home from
// over 1 off, else heed the player; stand 20. Aside (or no record): to 3
// north of home when over 7 off (over 2 once data+0x40 is 2); else heed
// the player; stand 120. Checked against game.exe by tools/emu jarjar.py.
// FUN_0059b8f0's other spot (data +0x18 / +0x19, +0x30 / +0x34) is dead:
// nothing writes them (stubs.md).
NpcAct jarjar_think(NpcBrain& brain, int x, int y, int frame, bool aside, bool stood_aside, const NpcVisitor& visitor) {
    auto stand = [](int frames) { return NpcAct{ .frames = frames }; };
    auto walk = [](int to_x, int to_y) { return NpcAct{ .kind = NpcAct::Kind::walk, .x = to_x, .y = to_y, .fail = 15 }; };
    if (!brain.homed) {                                  // FUN_005e6800
        brain.homed = true;
        brain.home_x = x; brain.home_y = y;
        return stand(20);
    }
    if (!aside) {
        if (brain.guard_frame < frame && frame - brain.guard_frame < 200) return stand(20);
        if (npc_distance(x, y, brain.home_x, brain.home_y) > 1) { brain.guard_frame = frame; return walk(brain.home_x, brain.home_y); }
        if (auto heeded = npc_heed(brain, x, y, visitor)) { brain.guard_frame = frame; return *heeded; }
        return stand(20);
    }
    const int gap = npc_distance(x, y, brain.home_x, brain.home_y - 3);
    if ((stood_aside && gap > 2) || gap > 7) return walk(brain.home_x, brain.home_y - 3);
    // A heed's own walk is cut by the stand 120 after it (FUN_005de080 sets
    // NU at once); its greet and facing stand. game.exe's roll after a heed
    // that didn't act (FUN_005df530 / FUN_005de200) never runs: the guard's
    // interact holder makes FUN_005e68f0 act on any player by, and with none
    // FUN_005ddf20 gives the guard himself (tools/emu jarjar.py).
    NpcAct act = stand(120);
    if (const auto heeded = npc_heed(brain, x, y, visitor)) { act.greet = heeded->greet; act.face = heeded->face; }
    return act;
}

}
