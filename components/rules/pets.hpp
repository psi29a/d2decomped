// SPDX-License-Identifier: GPL-3.0-or-later
// The pets' think (MonAI 67 "NecroPet", FUN_005e4cf0: the golems, the
// Valkyrie, the skeletons and skeletal mages) and how a pet follows its
// owner (FUN_005e3ea0). tools/emu/necropet.pet_y checks it against game.exe.
#pragma once

#include "merc.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace d2d::rules {

// What the think sees, as MercView (subtiles). `cur`: the owner's path +0x10
// (where it's at on its path), `end` its end (+0x18). `arrive`: where the
// owner last arrived through a warp or teleport (player data +0x148,
// FUN_00554ea0). `pets`: how many pets the owner has (FUN_00574f40);
// `crowd`: how many of them stand within 2 of this one (FUN_005e3900).
// `velocity` / `run`: MonStats. The foe (FUN_005dd7f0): its spot, the
// search's distance and whether it's in melee (FUN_00622c40), `ignored`
// its +0xc4 bit 30; `clear` a line to it (FUN_005dc640).
struct PetView {
    int x = 0, y = 0, size = 1;
    int owner_x = 0, owner_y = 0, owner_mode = 1, cur_x = 0, cur_y = 0, end_x = 0, end_y = 0;
    std::array<std::pair<int, int>, 20> ring{};
    int cursor = 0, arrive_x = 0, arrive_y = 0, pets = 1, crowd = 0;
    bool town = false;
    int velocity = 0, run = 0;
    bool foe = false, foe_melee = false, foe_ignored = false, clear = true;
    int foe_x = 0, foe_y = 0, foe_distance = 0;
};

// The think's outcome: a move that found a path, every move failing, a
// stand (frames), a teleport (FUN_005e3ea0 mode 3), a swing at the foe
// (FUN_005ddf90 mode 4), or a walk at it that found a path. `unreachable`:
// a walk at the foe found none (AI control +8 bit 1, FUN_005dd230).
struct PetAct {
    enum class Kind { moved, failed, stand, teleport, swing, chase } kind = Kind::failed;
    MercMove move;
    int frames = 0;
    bool unreachable = false;
};

// The think (FUN_005e4cf0 → FUN_005e4830; +0x14 is only set by a talk, so
// FUN_005e4ac0 never runs for a pet). Over 50 from the owner: a teleport;
// 29 or over: after the owner (FUN_005e3ea0 mode 0) at MonStats Run x 100 /
// Velocity - 100 % pace (100 when that's over 99 or Velocity is 0). Else
// the foe: one within 24 the search found that's 6 or nearer, else any it
// found under 36 by merc_gap (not bit 30); none without a clear line. A
// step of its seed % 100 under 15 lets it stay put instead of following
// (and adds a subtile of reach). Then FUN_005e45d0 decides whether to follow;
// if not: with a foe outside town, in melee 80 in 100 a swing (else stand
// 10), out of melee a walk at it (pace 0xc steps, path type 0xd, 1 off) —
// none found marks it unreachable and 70 in 100 wanders 4, else stands 10.
// No foe: it wanders 4. `level_at(x, y)` names the level under a subtile
// (FUN_0061b130); `try_move(move)` sets off, true when it finds a path
// (`move.foe`: at the foe).
template <class LevelAt, class TryMove>
PetAct necropet_think(const PetView& view, Rng& seed, LevelAt&& level_at, TryMove&& try_move) {
    using Kind = PetAct::Kind;
    PetAct act{ .kind = Kind::failed };
    MercMove ctx;                                                 // FUN_005a6260: only what's not 0 is set
    auto pace = [&](int type, int pct, int steps) {
        if (type) ctx.type = type;
        if (pct) ctx.pct = pct;
        if (steps) ctx.steps = std::min(steps, 0x4d);
    };
    auto set_out = [&](int x, int y, int mode = kMonsterWalk, bool foe = false) {
        MercMove move = ctx;
        move.x = x; move.y = y; move.mode = mode; move.foe = foe;
        ctx = {};
        if (!try_move(move)) return false;
        act = { .kind = foe ? Kind::chase : Kind::moved, .move = move };
        return true;
    };
    auto about = [&](int centre_x, int centre_y, int reach) {          // FUN_005df400 / df530 / de200
        int off_x = 0, off_y = 0;
        if ((seed.next() & 1) == 0) { off_x = seed(reach); off_y = reach; }
        else { off_x = reach; off_y = seed(reach); }
        if (seed.next() & 1) off_x = -off_x;
        if (seed.next() & 1) off_y = -off_y;
        return std::pair{ centre_x + off_x, centre_y + off_y };
    };
    auto wander = [&](int centre_x, int centre_y, int reach) { const auto [x, y] = about(centre_x, centre_y, reach); return set_out(x, y); };
    auto stand = [&](int frames) { act = { .kind = Kind::stand, .frames = frames }; return true; };
    const int pet_x = view.x, pet_y = view.y, own_x = view.owner_x, own_y = view.owner_y, end_x = view.end_x, end_y = view.end_y;
    // FUN_005defe0: `reach` off on each axis, away from the owner.
    auto away = [&](int reach) {
        const int away_x = pet_x < own_x ? -1 : own_x < pet_x ? 1 : 0, away_y = pet_y < own_y ? -1 : own_y < pet_y ? 1 : 0;
        if (reach > 5) pace(0, 0, reach);
        return set_out(pet_x + reach * away_x, pet_y + reach * away_y);
    };
    // FUN_005e3ea0: after the owner, by mode (run: running moves). 0: 8 off
    // the owner's path end ahead of it, round the compass on its level, each
    // spot then halfway to it; else wander 4, halfway to the owner, the owner.
    // 1: the owner walking, its path end then halfway; then its footsteps
    // as the merc's (pace rand(40) + 40 unless given); none: wander a
    // quarter of the gap. 2: 10 in 100 wander 3..5, else its path end. 3: a
    // teleport. 4: with others crowding it, `reach` away, else wander
    // `reach`. 5: path type 7, `reach` about the owner, `reach` away, its
    // path end. 2 and 4 stand 15 when nothing took.
    auto follow = [&](int mode, bool run, int pct, int reach) -> bool {
        const int move_mode = run ? kMonsterRun : kMonsterWalk;
        if (mode == 3) { act = { .kind = Kind::teleport }; return true; }   // ponytail: d2d's teleport always lands
        if (mode == 0) {
            static constexpr std::array<std::pair<int, int>, 8> kAhead{ { { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } } };
            int eighth = ((direction64(own_x, own_y, end_x, end_y) + 4) >> 3) & 7;
            const int level = level_at(own_x, own_y);
            for (int turn = 0; turn < 8; ++turn, eighth = (eighth + 1) & 7) {
                const int x = end_x + kAhead[std::size_t(eighth)].first * 8, y = end_y + kAhead[std::size_t(eighth)].second * 8;
                if (level_at(x, y) != level) continue;
                pace(0, pct, 0x28);
                if (set_out(x, y, move_mode)) return true;
                if (set_out(int(std::uint32_t(pet_x + x) >> 1), int(std::uint32_t(pet_y + y) >> 1), move_mode)) return true;
            }
            if (wander(pet_x, pet_y, 4)) return true;
            if (run && set_out((pet_x + own_x) / 2, (pet_y + own_y) / 2, kMonsterRun)) return true;
            if (set_out((pet_x + own_x) / 2, (pet_y + own_y) / 2)) return true;
            return set_out(own_x, own_y);
        }
        if (mode == 1) {
            if (view.owner_mode == kPlayerWalk) {
                pace(0, 0, 100);
                if (set_out(end_x, end_y) || set_out((pet_x + end_x) / 2, (pet_y + end_y) / 2)) return true;
            }
            if (pct == 0) pct = seed(0x28) + 0x28;
            bool tried = false;
            for (int looked = 0, cursor = view.cursor;;) {
                cursor = cursor == 0 ? 0x13 : cursor - 1;
                const auto [x, y] = view.ring[std::size_t(cursor)];
                if (x != 0 && y != 0 && npc_distance(pet_x, pet_y, x, y) > 5) {
                    pace(0, pct, 100);
                    if (set_out(x, y, move_mode)) return true;
                    pace(0xf, pct, 100);
                    if (set_out(x, y, move_mode)) return true;
                    if (!tried) {
                        tried = true;
                        pace(1, pct, 100);
                        if (set_out(x, y, move_mode)) return true;
                    }
                }
                if (++looked > 0x13) {
                    pace(0, 0xf, 0);
                    return wander(pet_x, pet_y, std::max(int(std::uint32_t(merc_gap(pet_x, pet_y, view.size, own_x, own_y)) >> 2), 4));
                }
            }
        }
        if (mode == 2 && seed(100) < 10) {
            if (wander(pet_x, pet_y, seed(3) + 3)) return true;
            pace(0, 0, 0x28);
            if (set_out(end_x, end_y)) return true;
        }
        if (mode == 4 && view.crowd > 0 && (away(reach) || wander(pet_x, pet_y, reach))) return true;
        if (mode == 5) {
            pace(7, 0, 0);
            if (wander(own_x, own_y, reach) || away(reach)) return true;
            pace(0, 0, 0x28);
            return set_out(end_x, end_y);
        }
        return stand(15);
    };
    // FUN_005e45d0: whether (and how) it follows instead of fighting.
    auto decide = [&](bool foe, bool melee, bool stay, int reach) -> bool {
        const int gap = merc_gap(pet_x, pet_y, view.size, own_x, own_y);
        reach = std::min(reach + (view.pets >> 1), 0x24);
        if (gap <= 1 && view.owner_mode == kMonsterNeutral && !melee) return follow(5, false, 0, reach);
        if (foe && !view.town) return gap > 0x50 && follow(3, false, 0, reach);
        int mode = 2;
        if (view.owner_mode == kPlayerWalk || view.owner_mode == kPlayerTownWalk || view.owner_mode == kPlayerRun) mode = 0;
        if (view.cur_x != end_x && view.cur_y != end_y) mode = 0;
        if (level_at(own_x, own_y) != level_at(pet_x, pet_y)) mode = 1;
        if (gap > reach) mode = 1;
        if (gap > 0x32) mode = 3;
        if (npc_distance(pet_x, pet_y, view.arrive_x, view.arrive_y) < 0x1c && ((mode == 1 && gap < 0x1e) || mode == 2)) mode = 4;
        if (stay && mode == 2) return false;
        return follow(mode, false, 0, reach);
    };
    const int gap = merc_gap(pet_x, pet_y, view.size, own_x, own_y);
    if (gap > 0x32) { follow(3, false, 0, 0); return act; }
    const int pct = view.velocity < 1 || view.run * 100 / view.velocity - 100 > 99 ? 100 : view.run * 100 / view.velocity - 100;
    if (gap >= 0x1d) { follow(0, false, pct, 0); return act; }
    bool foe = view.foe && view.foe_distance <= 0x18;
    if (!foe || view.foe_distance > 6) foe = view.foe && !view.foe_ignored && merc_gap(pet_x, pet_y, view.size, view.foe_x, view.foe_y) < 0x24;
    if (!view.clear) foe = false;
    const bool stay = seed.next() % 100 <= 14;
    if (decide(foe, view.foe && view.foe_melee, stay, stay ? 8 : 7)) return act;
    if (foe && !view.town) {
        if (view.foe_melee) {
            if (seed(100) > 0x4f) return { .kind = Kind::stand, .frames = 10 };
            return { .kind = Kind::swing };
        }
        pace(0, 0, 0xc);
        pace(0xd, 0, 0);
        if (set_out(view.foe_x, view.foe_y, kMonsterWalk, true)) return act;
        if (seed.next() % 100 < 0x46) wander(pet_x, pet_y, 4);
        else stand(10);
        act.unreachable = true;
        return act;
    }
    wander(pet_x, pet_y, 4);
    return act;
}

// FUN_005dc640: a clear line from the pet to its foe. Lines (sight_blocked
// from the pet's size to a size-2 end, `blocked` the collision under mask
// 0x805, FUN_006229f0) to the foe's spot, then to the spots beside it
// across the line, 2..4 off by their gap (merc_gap: 2 under 3, 3 under 11,
// 4 under 25, else 3); any one clear will do. tools/emu/pet_line.py checks
// it against game.exe.
template <class Blocked>
bool pet_line_clear(int pet_x, int pet_y, int size, int foe_x, int foe_y, Blocked&& blocked) {
    const int gap = merc_gap(pet_x, pet_y, size, foe_x, foe_y);
    const int step = gap < 3 ? 2 : gap < 11 ? 3 : gap < 25 ? 4 : 3;
    const int step_x = pet_x < foe_x ? -step : foe_x < pet_x ? step : 0, step_y = pet_y < foe_y ? -step : foe_y < pet_y ? step : 0;
    const std::array<std::pair<int, int>, 3> ends{ { { foe_x, foe_y }, { foe_x - step_y, foe_y + step_x }, { foe_x + step_y, foe_y - step_x } } };
    return std::ranges::any_of(ends, [&](const std::pair<int, int>& end) { return !sight_blocked(pet_x, pet_y, size, end.first, end.second, 2, blocked); });
}

}  // namespace d2d::rules
