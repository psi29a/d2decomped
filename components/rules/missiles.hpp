// SPDX-License-Identifier: GPL-3.0-or-later
// A missile's flight — game.exe 1.14d: FUN_0059fa30 makes it, its
// srvdofunc 1 (FUN_005ae1f0 -> FUN_00554ca0 -> FUN_00650840) moves it a
// frame at a time on its dynamic path (Path.cpp). docs/research/re/missiles.md;
// tools/emu/missiles.py runs game.exe's own flights against this.
#pragma once

#include "monsters.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace d2d::rules {

// The unit vectors (x 4096) of FUN_0064fc60, by the smaller delta x 127 /
// the larger: {smaller axis, larger axis} (the table at 0x6eb7e0, 12-byte
// rows whose third word is the eighth direction64 counts).
inline constexpr std::array<std::array<int, 2>, 128> kMissileAim{ {
    { 0, 4096 }, { 32, 4095 }, { 64, 4095 }, { 96, 4094 }, { 128, 4093 }, { 161, 4092 }, { 193, 4091 }, { 225, 4089 },
    { 257, 4087 }, { 289, 4085 }, { 321, 4083 }, { 353, 4080 }, { 385, 4077 }, { 417, 4074 }, { 448, 4071 }, { 480, 4067 },
    { 511, 4063 }, { 543, 4059 }, { 574, 4055 }, { 606, 4050 }, { 637, 4046 }, { 668, 4041 }, { 699, 4035 }, { 729, 4030 },
    { 760, 4024 }, { 791, 4018 }, { 821, 4012 }, { 851, 4006 }, { 881, 3999 }, { 911, 3993 }, { 941, 3986 }, { 971, 3979 },
    { 1000, 3971 }, { 1030, 3964 }, { 1059, 3956 }, { 1088, 3948 }, { 1117, 3940 }, { 1145, 3932 }, { 1174, 3924 }, { 1202, 3915 },
    { 1230, 3906 }, { 1258, 3897 }, { 1286, 3888 }, { 1313, 3879 }, { 1340, 3870 }, { 1368, 3860 }, { 1394, 3851 }, { 1421, 3841 },
    { 1448, 3831 }, { 1474, 3821 }, { 1500, 3811 }, { 1526, 3800 }, { 1552, 3790 }, { 1577, 3780 }, { 1602, 3769 }, { 1627, 3758 },
    { 1652, 3747 }, { 1677, 3736 }, { 1701, 3725 }, { 1725, 3714 }, { 1749, 3703 }, { 1773, 3692 }, { 1796, 3680 }, { 1820, 3669 },
    { 1843, 3657 }, { 1866, 3646 }, { 1888, 3634 }, { 1911, 3622 }, { 1933, 3610 }, { 1955, 3599 }, { 1977, 3587 }, { 1998, 3575 },
    { 2020, 3563 }, { 2041, 3551 }, { 2062, 3539 }, { 2082, 3526 }, { 2103, 3514 }, { 2123, 3502 }, { 2143, 3490 }, { 2163, 3478 },
    { 2183, 3465 }, { 2202, 3453 }, { 2221, 3441 }, { 2240, 3428 }, { 2259, 3416 }, { 2278, 3403 }, { 2296, 3391 }, { 2314, 3379 },
    { 2332, 3366 }, { 2350, 3354 }, { 2368, 3341 }, { 2385, 3329 }, { 2402, 3317 }, { 2419, 3304 }, { 2436, 3292 }, { 2453, 3279 },
    { 2469, 3267 }, { 2486, 3255 }, { 2502, 3242 }, { 2518, 3230 }, { 2533, 3218 }, { 2549, 3205 }, { 2564, 3193 }, { 2580, 3181 },
    { 2595, 3169 }, { 2609, 3156 }, { 2624, 3144 }, { 2639, 3132 }, { 2653, 3120 }, { 2667, 3108 }, { 2681, 3096 }, { 2695, 3084 },
    { 2709, 3072 }, { 2722, 3060 }, { 2736, 3048 }, { 2749, 3036 }, { 2762, 3024 }, { 2775, 3012 }, { 2788, 3000 }, { 2800, 2988 },
    { 2813, 2977 }, { 2825, 2965 }, { 2837, 2953 }, { 2849, 2942 }, { 2861, 2930 }, { 2873, 2919 }, { 2884, 2907 }, { 2896, 2896 },
} };

// The path velocity FUN_0059fa30 gives every missile it makes:
// ((Vel + VelLev x lvl / 8) << 8) x 75 / 100. A frame moves it velocity /
// 4096 subtiles (FUN_006502d0: x 0x400 >> 6, then x the unit vector >> 12).
inline int missile_velocity(int vel, int vel_lev, int level) {
    return ((vel + vel_lev * level / 8) << 8) * 75 / 100;
}

// Its life in frames, Range + LevRange x lvl (FUN_0064a330 keeps it a short).
inline int missile_range(int range, int lev_range, int level) {
    return std::clamp(range + lev_range * level, -0x8000, 0x7fff);
}

// FUN_0064fc60: the unit vector (x 4096) from 16.16 (x, y) to (to_x, to_y).
// The smaller delta x 127 / the larger picks the row (32-bit, as the exe).
inline std::pair<int, int> missile_aim(std::uint32_t x, std::uint32_t y, std::uint32_t to_x, std::uint32_t to_y) {
    const bool west = to_x < x, north = to_y < y;
    const std::uint32_t across = west ? x - to_x : to_x - x, down = north ? y - to_y : to_y - y;
    const bool steep = std::int32_t(across) <= std::int32_t(down);
    const std::uint32_t major = steep ? down : across, minor = steep ? across : down;
    const int row = major == 0 ? 0 : std::int32_t(minor * 0x7fu) / std::int32_t(major);
    const auto& [small, big] = kMissileAim[std::size_t(std::clamp(row, 0, 127))];
    const int aim_x = steep ? small : big, aim_y = steep ? big : small;
    return { west ? -aim_x : aim_x, north ? -aim_y : aim_y };
}

// The same along a direction in floats (a missile turned in flight).
inline std::pair<int, int> missile_aim_along(float dx, float dy) {
    const float across = std::abs(dx), down = std::abs(dy);
    const bool steep = across <= down;
    const float major = steep ? down : across, minor = steep ? across : down;
    const int row = major == 0 ? 0 : std::clamp(int(minor * 127 / major + 1e-4f), 0, 127);
    const auto& [small, big] = kMissileAim[std::size_t(row)];
    const int aim_x = steep ? small : big, aim_y = steep ? big : small;
    return { dx < 0 ? -aim_x : aim_x, dy < 0 ? -aim_y : aim_y };
}

// Charged Bolt's path (FUN_005c9290 -> path type 10, FUN_0067a240): from
// subtile (x, y), steps / 2 hops of two subtiles, each the 8-way way
// toward (to_x, to_y) (FUN_00678c10 -> DAT_006f1518's first) turned -1, 0
// or +1 by a draw on `seed` (low & 31 into -1, 0, 1, -1, ... with 31 at +1).
// The seed is {target x + the bolt's index, 666} (FUN_00650e40).
inline std::vector<std::pair<int, int>> wiggle_points(int x, int y, int to_x, int to_y, int steps, Rng seed) {
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

// A size-1 missile at subtile (x, y) strikes a unit of `size` at (unit_x,
// unit_y) (FUN_00641cb0): its own subtile at size 1, the plus at 2, the
// 3x3 at 3.
// ponytail: missiles of Size 2 and 3 (none in Act 1) test wider shapes.
inline bool missile_touches(int x, int y, int unit_x, int unit_y, int size) {
    const int across = std::abs(x - unit_x), down = std::abs(y - unit_y);
    if (size <= 1) return across == 0 && down == 0;
    if (size == 2) return across + down <= 1;
    return across <= 1 && down <= 1;
}

// A missile's dynamic path (unit +0x2c): 16.16 subtiles (+0 / +4), the unit
// vector (+0x6a / +0x6e), velocity (+0x7c), MaxVel << 8 (+0x84), Accel
// (+0x88) and its frame count (+0x8c); Charged Bolt's points (+0x9c, the
// index +0x24). `crossed`: the subtiles a frame entered (+0x1d4 / +0x1d8).
struct MissileFlight {
    std::uint32_t x = 0, y = 0;
    int aim_x = 0, aim_y = 4096;
    int velocity = 0, max_velocity = 0, accel = 0, accel_frames = 0;
    std::vector<std::pair<int, int>> points;
    std::size_t point = 0;
    std::vector<std::pair<int, int>> crossed;

    // FUN_0059fa30: from subtile (x, y)'s centre at (to_x, to_y)'s (path
    // type 4 straight on: FUN_006492f0 -> FUN_0064fe40), never at itself.
    static MissileFlight launch(int x, int y, int to_x, int to_y, int velocity, int max_velocity, int accel) {
        if (to_x == x && to_y == y) { ++to_x; ++to_y; }
        MissileFlight flight;
        flight.x = (std::uint32_t(x) << 16) + 0x8000; flight.y = (std::uint32_t(y) << 16) + 0x8000;
        std::tie(flight.aim_x, flight.aim_y) = missile_aim(flight.x, flight.y, (std::uint32_t(to_x) << 16) + 0x8000, (std::uint32_t(to_y) << 16) + 0x8000);
        flight.velocity = velocity; flight.max_velocity = max_velocity; flight.accel = accel;
        return flight;
    }
    [[nodiscard]] int subtile_x() const { return int(x >> 16); }
    [[nodiscard]] int subtile_y() const { return int(y >> 16); }

    // One frame (FUN_00650840 with 0x400): every fifth frame Accel joins
    // the velocity, up to MaxVel (then Accel stops) or down to 0; the step is
    // the unit vector x velocity x 16 >> 12. A points path snaps onto its
    // next point within a step and turns to the one after (FUN_00650090,
    // FUN_0064fe40), and is over past its last. The step's subtiles are
    // walked (FUN_00650150) and each tested (`blocked(x, y)`: FUN_0064ff90,
    // the 0x04 bit or 0x27 off the rooms): a blocked one snaps the missile
    // to the centre of the last free point, and the flight is over. False:
    // over (a stop snaps to its subtile's centre, FUN_006507b0).
    template <class Blocked>
    bool step(Blocked&& blocked) {
        crossed.clear();
        if (accel != 0 && ++accel_frames > 4) {
            velocity += accel;
            if (velocity > max_velocity) { velocity = max_velocity; accel = 0; }
            else if (velocity < 0) velocity = 0;
            accel_frames = 0;
        }
        const int speed = int(std::uint32_t(velocity) * 0x400u) >> 6;
        int step_x = int(std::uint32_t(aim_x) * std::uint32_t(speed)) >> 12, step_y = int(std::uint32_t(aim_y) * std::uint32_t(speed)) >> 12;
        if (step_x == 0 && step_y == 0) return stop();
        bool snapped = false;
        if (!points.empty() && point < points.size()) {
            const auto [point_x, point_y] = centre(points[point]);
            const int reach = std::max(std::abs(step_x), std::abs(step_y));
            if (std::abs(int(point_x - x)) <= reach && std::abs(int(point_y - y)) <= reach) {
                step_x = int(point_x - x); step_y = int(point_y - y); snapped = true;
            }
        }
        if (!walk(step_x, step_y, blocked)) return false;
        x += std::uint32_t(step_x); y += std::uint32_t(step_y);
        if (!snapped) return true;
        if (++point >= points.size()) return stop();
        retarget();
        return point < points.size() || stop();
    }

    // FUN_0064fe40: aim at the next point not under it (none left: over).
    void retarget() {
        for (; point < points.size(); ++point) {
            const auto [point_x, point_y] = centre(points[point]);
            if (point_x != x || point_y != y) { std::tie(aim_x, aim_y) = missile_aim(x, y, point_x, point_y); return; }
        }
    }

    bool stop() {
        x = (x & 0xffff0000u) + 0x8000; y = (y & 0xffff0000u) + 0x8000;
        return false;
    }

private:
    static std::pair<std::uint32_t, std::uint32_t> centre(std::pair<int, int> subtile) {
        return { (std::uint32_t(subtile.first) << 16) + 0x8000, (std::uint32_t(subtile.second) << 16) + 0x8000 };
    }
    // FUN_00650150: the step halved (FUN_00678f00) until each part is a
    // subtile at most, walked from here to the step's end subtile; each new
    // subtile tested and kept (ten at most).
    template <class Blocked>
    bool walk(int step_x, int step_y, Blocked&& blocked) {
        int part_x = step_x, part_y = step_y;
        while (part_x > 0x10000 || part_x < -0x10000) { part_x >>= 1; part_y >>= 1; }
        while (part_y > 0x10000 || part_y < -0x10000) { part_x >>= 1; part_y >>= 1; }
        const std::uint32_t end_x = ((x + std::uint32_t(step_x)) >> 16) & 0xffff, end_y = ((y + std::uint32_t(step_y)) >> 16) & 0xffff;
        std::uint32_t at_x = x, at_y = y, here_x = x >> 16, here_y = y >> 16;
        while (here_x != end_x || here_y != end_y) {
            const std::uint32_t next_x = at_x + std::uint32_t(part_x), next_y = at_y + std::uint32_t(part_y);
            const std::uint32_t there_x = next_x >> 16, there_y = next_y >> 16;
            if (there_x != here_x || there_y != here_y) {
                if (blocked(int(there_x), int(there_y))) {
                    x = (at_x & 0xffff0000u) + 0x8000; y = (at_y & 0xffff0000u) + 0x8000;
                    return false;
                }
                crossed.emplace_back(int(there_x), int(there_y));
                if (crossed.size() >= 10) break;
            }
            at_x = next_x; at_y = next_y; here_x = there_x; here_y = there_y;
        }
        return true;
    }
};

}  // namespace d2d::rules
