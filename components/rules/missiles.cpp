// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of missiles.hpp's functions; their notes are in the header.
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
#include <utility>
#include <vector>

namespace d2d::rules {

// A frame moves it velocity / 4096 subtiles (FUN_006502d0: x 0x400 >> 6,
// then x the unit vector >> 12).
int missile_velocity(int vel, int vel_lev, int level) {
    return ((vel + vel_lev * level / 8) << 8) * 75 / 100;
}

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

// Each axis is a step of `seed` modulo 0x28, then less 0x14, so
// -20..19. A (0, 0) pair is stored as x = 0x14.
std::pair<int, int> release_spread_offset(Rng& seed) {
    const int offset_x = seed(0x28) - 0x14;
    const int offset_y = seed(0x28) - 0x14;
    if (offset_x == 0 && offset_y == 0) return { 0x14, 0 };
    return { offset_x, offset_y };
}

std::pair<int, int> release_scatter_offset(Rng& seed, int radius) {
    const int offset_x = radius - seed(2 * radius);
    const int offset_y = radius - seed(2 * radius);
    return { offset_x, offset_y };
}

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

}
