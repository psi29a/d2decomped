// SPDX-License-Identifier: GPL-3.0-or-later
// The merc's think (MonAI 61 "Hireable", FUN_005e52d0) and how it follows
// its owner (FUN_005e3930). tools/emu/merc.py checks it against game.exe.
#pragma once

#include "monsters.hpp"
#include "rules.hpp"
#include "town_npcs.hpp"

#include <array>
#include <cstdlib>
#include <utility>

namespace d2d::rules {

// Monster modes the think reads.
inline constexpr int kMonsterNeutral = 1, kMonsterWalk = 2, kMonsterRun = 0xf;
// Player modes: walking, running, walking in town.
inline constexpr int kPlayerWalk = 2, kPlayerRun = 3, kPlayerTownWalk = 6;

// What the think sees. Subtiles throughout; `size` the merc's
// (FUN_00620510). `ring`: the owner's footsteps (player data +0xa8, 20
// spots, `cursor` +0xa0 the next to write; a spot goes in when the player,
// walking or running, is over 45 (squared) from the last, FUN_00554ca0's
// caller, or arrives through a warp). `end`: the owner's path end (+0x18).
// `on_bit40`: the collision bit 0x40 under the merc (FUN_0064d910).
// `target`: the found foe's distance (FUN_005ddc30), -1 none.
struct MercView {
    int cls = 0, x = 0, y = 0, size = 1, mode = kMonsterNeutral;
    int owner_x = 0, owner_y = 0, owner_mode = 1, end_x = 0, end_y = 0;
    std::array<std::pair<int, int>, 20> ring{};
    int cursor = 0;
    bool on_bit40 = false, town = false;
    int target = -1;
};

// A move it sets off (FUN_005a7c20 through FUN_005dee50 and its kin): to a
// spot, walking (mode 2) or running (0xf), the path ctx (FUN_005a6260):
// a path type (0: the walk's own, 0xd then 0xf), the pace % and the path's
// steps (0: the type's own); stopping 1 off (FUN_00649070).
struct MercMove { int x = 0, y = 0, mode = kMonsterWalk, type = 0, pct = 0, steps = 0; };

// The think's outcome: a move that found a path, a stand (frames), a
// teleport into the owner's room, the attack think (FUN_005e5050), every
// move failing (the next think aidel on), or nothing (moving already).
struct MercAct {
    enum class Kind { none, moved, failed, stand, teleport, attack } kind = Kind::none;
    MercMove move;
    int frames = 0;
};

// FUN_005dc380: each axis less the merc's size (not under 0); (short + 2 x
// long) / 2.
[[nodiscard]] inline int merc_gap(int x, int y, int size, int to_x, int to_y) {
    const int across = std::max(std::abs(x - to_x) - size, 0), down = std::max(std::abs(y - to_y) - size, 0);
    return (across > down ? down + across * 2 : across + down * 2) / 2;
}

// The think (FUN_005e52d0). Moving already, it does nothing. Then by its
// distance to the owner (merc_gap; 16 and 24 unless the AI control's +0x14
// is 0x11..0x13, which only commands set): over 100 a teleport into the
// owner's room; over 24 it runs after the footsteps; over 16 with the owner
// walking, 8 off the owner's path end ahead of him, at its own pace; running,
// the same at pace 60. Not standing, it stands 5. Standing on bit 0x40, 6 in
// 128 (12 for the bow and spear mercs, all but MonStats 0x152 / 0x230 /
// 0x231) wander 5. Outside town, a foe under 25 off: the attack think. On
// bit 0x40, wander 5. On another level than the owner: 8 off his path end at
// pace 60. Within 1 of him: path type 7 to a spot 4 about him, else 4 away
// on each axis, else to his path end (steps 0x28). Else 5 in 100 a spot 16
// about him; else it stands 5. A spot "n about" (FUN_005df530 / de200 /
// df400): one step of its seed picks the axis that's n off (low bit 0: y),
// the other rand(n), two more steps their signs. Rolls on the merc's seed
// (+0x20). `level_at(x, y)` names the level under a subtile (FUN_0061b130);
// `try_move(move)` sets off, true when it finds a path.
template <class LevelAt, class TryMove>
MercAct hireable_think(const MercView& view, Rng& seed, LevelAt&& level_at, TryMove&& try_move) {
    using Kind = MercAct::Kind;
    MercAct act{ .kind = Kind::failed };
    MercMove ctx;                                                 // FUN_005a6260: only what's not 0 is set
    auto pace = [&](int type, int pct, int steps) {
        if (type) ctx.type = type;
        if (pct) ctx.pct = pct;
        if (steps) ctx.steps = std::min(steps, 0x4d);
    };
    auto set_out = [&](int x, int y, int mode = kMonsterWalk) {        // FUN_005a7c20: the ctx is used up
        MercMove move = ctx;
        move.x = x; move.y = y; move.mode = mode;
        ctx = {};
        if (!try_move(move)) return false;
        act = { .kind = Kind::moved, .move = move };
        return true;
    };
    auto about = [&](int centre_x, int centre_y, int reach) {
        int off_x = 0, off_y = 0;
        if ((seed.next() & 1) == 0) { off_x = seed(reach); off_y = reach; }
        else { off_x = reach; off_y = seed(reach); }
        if (seed.next() & 1) off_x = -off_x;
        if (seed.next() & 1) off_y = -off_y;
        return std::pair{ centre_x + off_x, centre_y + off_y };
    };
    auto wander = [&](int centre_x, int centre_y, int reach) { const auto [x, y] = about(centre_x, centre_y, reach); return set_out(x, y); };
    // FUN_005e3930 mode 0: 8 off the owner's path end, from where it heads
    // (rules::direction64 into 8, DAT_00745600) round the compass, on the
    // owner's level; else wander 4.
    auto near_owner = [&](int pct) {
        static constexpr std::array<std::pair<int, int>, 8> kAhead{ { { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } } };
        int eighth = ((direction64(view.owner_x, view.owner_y, view.end_x, view.end_y) + 4) >> 3) & 7;
        const int level = level_at(view.owner_x, view.owner_y);
        for (int turn = 0; turn < 8; ++turn, eighth = (eighth + 1) & 7) {
            const int x = view.end_x + kAhead[std::size_t(eighth)].first * 8, y = view.end_y + kAhead[std::size_t(eighth)].second * 8;
            if (level_at(x, y) != level) continue;
            pace(0, pct, 0x28);
            if (set_out(x, y)) return;
        }
        wander(view.x, view.y, 4);
    };
    // FUN_005e3930 mode 1: with the owner walking, to his path end; else to
    // the newest footstep over 5 off (FUN_005dc5c0), walking, then as
    // `mode` with type 0xf, then once with type 1; none takes: wander a
    // quarter of the gap (4 at least), pace 15.
    auto footsteps = [&](int mode, int pct) {
        if (view.owner_mode == kPlayerWalk) {
            pace(0, 0, 100);
            set_out(view.end_x, view.end_y);
            return;
        }
        bool searched = false;
        for (int looked = 0, cursor = view.cursor; looked < 20; ++looked) {
            cursor = cursor == 0 ? 0x13 : cursor - 1;
            const auto [x, y] = view.ring[std::size_t(cursor)];
            if (npc_distance(view.x, view.y, x, y) <= 5) continue;
            pace(0, pct, 100);
            if (set_out(x, y)) return;
            pace(0xf, pct, 100);
            if (set_out(x, y, mode)) return;
            if (searched) continue;
            searched = true;
            pace(1, pct, 100);
            if (set_out(x, y, mode)) return;
        }
        pace(0, 0xf, 0);
        wander(view.x, view.y, std::max(merc_gap(view.x, view.y, view.size, view.owner_x, view.owner_y) >> 2, 4));
    };
    if (view.mode == kMonsterWalk || view.mode == kMonsterRun) return { .kind = Kind::none };
    const int gap = merc_gap(view.x, view.y, view.size, view.owner_x, view.owner_y);
    if (gap > 100) return { .kind = Kind::teleport };
    if (gap > 24) { footsteps(kMonsterRun, 0x3c); return act; }
    if (gap > 16 && (view.owner_mode == kPlayerWalk || view.owner_mode == kPlayerTownWalk)) { near_owner(0); return act; }
    if (gap > 16 && view.owner_mode == kPlayerRun) { near_owner(0x3c); return act; }
    if (view.mode != kMonsterNeutral) return { .kind = Kind::stand, .frames = 5 };
    const bool shooter = view.cls != 0x152 && (view.cls < 0x230 || view.cls > 0x231);
    if (view.on_bit40 && seed(0x80) < (shooter ? 12 : 6)) { wander(view.x, view.y, 5); return act; }
    if (!view.town && view.target >= 0 && view.target < 0x19) return { .kind = Kind::attack };
    if (view.on_bit40) { wander(view.x, view.y, 5); return act; }
    if (level_at(view.owner_x, view.owner_y) != level_at(view.x, view.y)) { near_owner(0x3c); return act; }
    if (gap <= 1) {
        pace(7, 0, 0);
        if (wander(view.owner_x, view.owner_y, 4)) return act;
        const int away_x = view.x < view.owner_x ? -1 : view.owner_x < view.x ? 1 : 0, away_y = view.y < view.owner_y ? -1 : view.owner_y < view.y ? 1 : 0;
        if (set_out(view.x + 4 * away_x, view.y + 4 * away_y)) return act;
        pace(0, 0, 0x28);
        set_out(view.end_x, view.end_y);
        return act;
    }
    if (seed(100) < 5) { wander(view.owner_x, view.owner_y, 16); return act; }
    return { .kind = Kind::stand, .frames = 5 };
}

}  // namespace d2d::rules
