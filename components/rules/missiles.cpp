// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of missiles.hpp's functions, with the research behind each.
#include "missiles.hpp"

#include "monsters.hpp"
#include "rules.hpp"
#include "weather.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::rules {

// A frame moves it velocity / 4096 subtiles (FUN_006502d0: x 0x400 >> 6,
// then x the unit vector >> 12).
int missile_velocity(int vel, int vel_lev, int level) {
    return ((vel + vel_lev * level / 8) << 8) * 75 / 100;
}

// Its life in frames, Range + LevRange x lvl (FUN_0064a330 keeps it a short).
int missile_range(int range, int lev_range, int level) {
    return std::clamp(range + lev_range * level, -0x8000, 0x7fff);
}

// Draws on {the owner's stat 0x148 (pierce_idx), 666} count while each
// lands under `chance` (skill_pierce + item_pierce), 4 at most.
int pierce_charges(int chance, std::uint32_t seed) {
    Rng roll{ seed };
    int charges = 0;
    while (charges < 4 && roll(100) < chance) ++charges;
    return charges;
}

AimRow aim_row(std::uint32_t x, std::uint32_t y, std::uint32_t to_x, std::uint32_t to_y) {
    const bool west = std::int32_t(to_x) < std::int32_t(x), north = std::int32_t(to_y) < std::int32_t(y);
    const std::uint32_t across = west ? x - to_x : to_x - x, down = north ? y - to_y : to_y - y;
    const bool steep = std::int32_t(across) <= std::int32_t(down);
    const std::uint32_t major = steep ? down : across, minor = steep ? across : down;
    const int row = major == 0 ? 0 : std::int32_t(minor * 0x7fu) / std::int32_t(major);
    return { std::clamp(row, 0, 127), steep, west, north };
}

std::pair<int, int> missile_aim(std::uint32_t x, std::uint32_t y, std::uint32_t to_x, std::uint32_t to_y) {
    const auto aim = aim_row(x, y, to_x, to_y);
    const auto& [small, big] = kMissileAim[std::size_t(aim.row)];
    const int aim_x = aim.steep ? small : big, aim_y = aim.steep ? big : small;
    return { aim.west ? -aim_x : aim_x, aim.north ? -aim_y : aim_y };
}

// tools/emu/facing.py; tests/test_facing.cpp.
int aim_direction(std::uint32_t x, std::uint32_t y, std::uint32_t to_x, std::uint32_t to_y) {
    const auto aim = aim_row(x, y, to_x, to_y);
    int dir = kAimEighth[std::size_t(aim.row)];
    if (!aim.steep) dir = (-dir - 1) & 0xf;
    if (aim.north) dir = (-dir - 1) & 0x1f;
    return aim.west ? (dir + 8) & 0x3f : (((-dir - 1) & 0x3f) + 8) & 0x3f;
}

// Royal Strike's third release (FUN_005d5080): one missile's aim, in
// subtiles, off the target.

// Each axis is a step of `seed` modulo 0x28, then less 0x14, so
// -20..19. A (0, 0) pair is stored as x = 0x14.
std::pair<int, int> release_spread_offset(Rng& seed) {
    const int offset_x = seed(0x28) - 0x14;
    const int offset_y = seed(0x28) - 0x14;
    if (offset_x == 0 && offset_y == 0) return { 0x14, 0 };
    return { offset_x, offset_y };
}

// A charge-up's scatter release (srvprgfunc 39, FUN_005d3f90): one try's
// offset off the target in subtiles, radius - rand(2 x radius) on x, then
// on y (0x5d4080..0x5d409c: EDX = EDI + EDI), so -radius + 1..radius. A
// try further than radius (squared) makes nothing (the caller's test).
std::pair<int, int> release_scatter_offset(Rng& seed, int radius) {
    const int offset_x = radius - seed(2 * radius);
    const int offset_y = radius - seed(2 * radius);
    return { offset_x, offset_y };
}

// Charged Bolt's path (FUN_005c9290 -> path type 10, FUN_0067a240): from
// subtile (x, y), steps / 2 hops of two subtiles, each the 8-way way
// toward (to_x, to_y) (FUN_00678c10 -> DAT_006f1518's first) turned -1, 0
// or +1 by a draw on `seed` (low & 31 into -1, 0, 1, -1, ... with 31 at +1).

// The seed is {target x + the bolt's index, 666} (FUN_00650e40).
std::vector<std::pair<int, int>> wiggle_points(int x, int y, int to_x, int to_y, int steps, Rng seed) {
    const int dx = to_x - x, dy = to_y - y;
    int aim_x = dx, aim_y = dy;
    if (std::abs(dx) >= std::abs(dy) * 2) aim_y = dy < 0 ? -1 : dy & 1;
    else if (std::abs(dx) * 2 <= std::abs(dy)) aim_x = dx < 0 ? -1 : dx & 1;
    const int base = kTry[std::size_t(std::clamp(aim_x, -2, 2) * 5 + 12 + std::clamp(aim_y, -2, 2))][0];
    std::vector<std::pair<int, int>> points;
    for (int k = 0; k < steps >> 1; ++k) {
        const int draw = int(seed.next() & 31);
        const int turn = draw == 31 ? 1 : draw % 3 - 1;
        const auto& step = kDir[std::size_t((base + turn) & 7)];
        points.emplace_back(x, y);
        x = (x + step[0] * 2) & 0xffff; y = (y + step[1] * 2) & 0xffff;
    }
    points.emplace_back(x, y);
    return points;
}

// Do 143's missiles' route (FUN_005d4870's path callback FUN_005d4680, path
// type 10): `count` points (the range, 77 at most) from subtile (x, y), each
// two subtiles on (0x6e32ac x, 0x6e328c y) along the 8-way (0x745600:
// (dir + 4) / 8) of a heading that starts at direction64 toward (to_x, to_y)
// and turns by turn x sign at every point: sign +1 when the first draw on
// `seed` is odd, else -1; turn the next draw % 3 + 2 (DAT_00741b10). After
// points 0, 15, 30 ... (DAT_00741b0c) the sign flips and turn is drawn again.
std::vector<std::pair<int, int>> wobble_route(int x, int y, int to_x, int to_y, int count, Rng& seed) {
    static constexpr std::array<int, 8> kStepX{ 2, 2, 0, -2, -2, -2, 0, 2 }, kStepY{ 0, 2, 2, 2, 0, -2, -2, -2 };
    constexpr int kFlipEvery = 15, kTurnLeast = 2;
    int heading = direction64(x, y, to_x, to_y) & 0xff;
    int sign = (seed.next() & 1) != 0 ? 1 : -1;
    int turn = int(seed.next() % 3) + kTurnLeast;
    std::vector<std::pair<int, int>> points;
    for (int point = 0; point < count; ++point) {
        heading += turn * sign;
        const auto way = std::size_t(((heading & 0x3f) + 4) / 8 & 7);
        x += kStepX[way]; y += kStepY[way];
        points.emplace_back(x, y);
        if (point % kFlipEvery == 0) {
            sign = -sign;
            turn = int(seed.next() % 3) + kTurnLeast;
        }
    }
    return points;
}

// tools/emu/hammer_route.py.
std::vector<std::pair<int, int>> hammer_route(int x, int y) {
    constexpr int kOut = 0x2580, kRound = 16, kPoints = 0x4d;
    const std::int64_t from_x = (std::int64_t(x) << 16) + 0x8000, from_y = (std::int64_t(y) << 16) + 0x8000;
    std::vector<std::pair<int, int>> points;
    for (int step = 1, last_x = x, last_y = y; int(points.size()) < kPoints; ++step) {
        const double radius = double(step) * kOut;
        const int at_x = int((from_x + std::llrint(double(cos512(step * kRound)) * radius)) >> 16);
        const int at_y = int((from_y + std::llrint(double(sin512(step * kRound)) * radius)) >> 16);
        if (at_x == last_x && at_y == last_y) continue;
        points.emplace_back(at_x, at_y);
        last_x = at_x; last_y = at_y;
    }
    return points;
}

bool missile_touches(int x, int y, int unit_x, int unit_y, int size, int missile_size) {
    if (missile_size < 1 || missile_size > 3 || size < 1) return false;
    const int across = std::abs(x - unit_x), down = std::abs(y - unit_y);
    switch (std::min(size, 3) - 1 + (missile_size - 1) * 3) {
        case 0: return across == 0 && down == 0;
        case 1: case 3: return across + down <= 1;
        case 2: case 6: return across <= 1 && down <= 1;
        case 4: return across + down <= 2;
        case 5: case 7: return (across < 3 && down < 2) || (down < 3 && across <= 1);
        default: return across <= 2 && down <= 2;
    }
}


// The frame a client do 5 missile (the ground fires: blaze, firewall, meteorfire, immolationfire) shows after `ticks`
// client frames of its `life` (frames left at birth, missile data +0x10), its frame counter (unit +0x44) in 256ths.
// Each frame FUN_004d3540 first picks: at frame SubStart - 1 a jump of rand(SubStop - SubStart) (FUN_0045c3e0 on the
// unit's seed); else with SubStart frames left, frame SubStart - 3; else with fewer left, two frames back (each at
// least 0). Then FUN_004d30c0 steps it by the rate (+0x4c): with LoopAnim and SubLoop and more than
// AnimLen - SubStop + 1 frames left, FUN_00621330 wraps it from SubStop back to SubStart; otherwise FUN_00621310
// wraps it at AnimLen; then one frame less is left. So the fire rises once, burns in SubStart..SubStop - 1 from a
// random point, and sinks as it runs out.
int ground_fire_frame(int ticks, int life, int anim_len, int sub_start, int sub_stop, bool sub_loop, int rate, Rng& seed) {
    int frame = 0;   // 256ths
    for (int tick = 0; tick < ticks; ++tick) {
        const int left = life - tick, whole = frame >> 8;
        if (whole == sub_start - 1) frame = (whole + seed(sub_stop - sub_start)) << 8;
        else if (left == sub_start) frame = std::max(sub_start - 3, 0) << 8;
        else if (left < sub_start) frame = std::max(whole - 2, 0) << 8;
        frame += rate;
        if (sub_loop && left > anim_len - sub_stop + 1) {
            if (frame >= sub_stop << 8) frame += (sub_start - sub_stop) << 8;
        } else if (frame >= anim_len << 8) {
            frame -= anim_len << 8;
        }
    }
    return frame >> 8;
}


// FUN_006485f0 for a player's or monster's path: a new goal and its step.
auto Facing::aim(int dir64) -> void {
    dir64 &= 0x3f;
    if (dir64 == goal) return;
    goal = std::uint8_t(dir64);
    step = kDiffDirections[std::size_t((dir64 - now) & 0x3f)];
}

// FUN_00648640: a frame's turn, onto the goal once it reaches or passes it.
auto Facing::turn() -> void {
    if (now == goal) return;
    now = std::uint8_t((now + step) & 0x3f);
    const int past = (now - goal) & 0x3f;
    if ((past > 0x1f && step < 0) || (past < 0x1f && step >= 0)) now = goal;
}

// FUN_0059fa30: from subtile (x, y)'s centre at (to_x, to_y)'s (path
// type 4 straight on: FUN_006492f0 -> FUN_0064fe40), never at itself.
auto MissileFlight::launch(int x, int y, int to_x, int to_y, int velocity, int max_velocity, int accel) -> MissileFlight {
    if (to_x == x && to_y == y) { ++to_x; ++to_y; }
    MissileFlight flight;
    flight.x = (std::uint32_t(x) << 16) + 0x8000; flight.y = (std::uint32_t(y) << 16) + 0x8000;
    std::tie(flight.aim_x, flight.aim_y) = missile_aim(flight.x, flight.y, (std::uint32_t(to_x) << 16) + 0x8000, (std::uint32_t(to_y) << 16) + 0x8000);
    flight.velocity = velocity; flight.max_velocity = max_velocity; flight.accel = accel;
    return flight;
}

}
