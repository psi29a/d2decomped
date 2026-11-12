// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00555da0: a drop's landing subtile is blocked by walls (0x01), items
// (0x200), and on this level a unit footprint. Pattern 1 / 2 stamp 0x1000,
// pattern 3 / 4 stamp 0x2000 (UnitShape::bit, FUN_0064ea90): the subtile,
// or the plus for pattern 2 / 4. Pattern 0 and 5 stamp nothing. Those bits
// are not in the walk grid. Another level's drop does not see this crowd.
// ponytail: one level is searched, no neighbours'.

#include <ai.hpp>
#include <cues.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

namespace {

constexpr int kWidth = 8;

int subtile(float value) { return int(std::floor(value * 5)); }

float centre(int sub) { return (float(sub) + 0.5f) / 5; }

Level open_level() {
    Level level;
    level.ds1 = d2d::ds1::Map(kWidth, kWidth, 1, 1);
    level.walk.assign(std::size_t(kWidth * 5) * std::size_t(kWidth * 5), 0);
    return level;
}

struct Bench {
    GameData data;
    Level level = open_level();
    const Level* shown = nullptr;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot;
    Bench() : shown(&level), loot{ &data, shown, rng, cues } { loot.ground_level = &level; }
};

std::pair<int, int> landed(Loot& loot, int from_x, int from_y, const Level* onto = nullptr) {
    const auto [spot_x, spot_y] = loot.drop_at(centre(from_x), centre(from_y), onto);
    return { subtile(spot_x), subtile(spot_y) };
}

}  // namespace

int main() try {
    // The search starts at (x + 2, y + 3). A clear grid lands there.
    {
        Bench bench;
        assert(landed(bench.loot, 10, 10) == std::pair(12, 13));
    }
    // A 0x1000 written into the walk grid is not a unit. The grid is static
    // collision; the drop still lands on that subtile.
    {
        Bench bench;
        bench.level.walk[std::size_t(13) * std::size_t(kWidth * 5) + 12] = 0x1000;
        assert(landed(bench.loot, 10, 10) == std::pair(12, 13));
        bench.level.walk[std::size_t(13) * std::size_t(kWidth * 5) + 12] = 0x2000;
        assert(landed(bench.loot, 10, 10) == std::pair(12, 13));
    }
    // FUN_00555da0: pattern 1 stamps 0x1000 on its subtile, so the start is
    // refused and the next free subtile (one step left) is taken.
    {
        Bench bench;
        UnitState body{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 1, 1, 0x3c01 } };
        const Crowd crowd{ { &body } };
        bench.loot.standing = &crowd;
        assert(body.shape.bit() == 0x1000);
        assert(landed(bench.loot, 10, 10) == std::pair(11, 13));
    }
    // Pattern 3 (the merc, the cow, a pet) stamps 0x2000 on its subtile.
    {
        Bench bench;
        UnitState merc{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 3, 1, 0x3401 } };
        const Crowd crowd{ { &merc } };
        bench.loot.standing = &crowd;
        assert(merc.shape.bit() == 0x2000);
        assert(landed(bench.loot, 10, 10) == std::pair(11, 13));
    }
    // Pattern 2 stamps 0x1000 on the plus (the subtile and its four
    // neighbours). The ring's corners stay free; FUN_0064dea0's scan takes
    // the first of those, (11, 12).
    {
        Bench bench;
        UnitState large{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 2, 3, 0x3c01 } };
        const Crowd crowd{ { &large } };
        bench.loot.standing = &crowd;
        assert(large.shape.box() && large.shape.bit() == 0x1000);
        assert(landed(bench.loot, 10, 10) == std::pair(11, 12));
    }
    // Pattern 4 stamps 0x2000 on the plus, the same shape.
    {
        Bench bench;
        UnitState large{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 4, 3, 0x3c01 } };
        const Crowd crowd{ { &large } };
        bench.loot.standing = &crowd;
        assert(large.shape.box() && large.shape.bit() == 0x2000);
        assert(landed(bench.loot, 10, 10) == std::pair(11, 12));
    }
    // Pattern 0 and 5 stamp nothing (SizeX 0, a vulture aloft).
    {
        Bench bench;
        UnitState chicken{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 0, 0, 0x3c01 } };
        UnitState aloft{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 5, 1, 0 } };
        const Crowd crowd{ { &chicken, &aloft } };
        bench.loot.standing = &crowd;
        assert(!chicken.shape.stamps() && !aloft.shape.stamps());
        assert(landed(bench.loot, 10, 10) == std::pair(12, 13));
    }
    // Walls (0x01) and an item already there (0x200) still block.
    {
        Bench bench;
        bench.level.walk[std::size_t(13) * std::size_t(kWidth * 5) + 12] = 0x01;
        assert(landed(bench.loot, 10, 10) == std::pair(11, 13));
        bench.level.walk[std::size_t(13) * std::size_t(kWidth * 5) + 12] = 0;
        Loot::GroundItem lying;
        lying.x = centre(12);
        lying.y = centre(13);
        bench.loot.ground.push_back(lying);
        assert(landed(bench.loot, 10, 10) == std::pair(11, 13));
    }
    // A drop onto another level does not see this level's crowd.
    {
        Bench bench;
        UnitState body{ .x = Fine::centre(12), .y = Fine::centre(13), .shape = UnitShape{ 1, 1, 0x3c01 } };
        const Crowd crowd{ { &body } };
        bench.loot.standing = &crowd;
        Level other = open_level();
        assert(landed(bench.loot, 10, 10, &other) == std::pair(12, 13));
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_drop_at: %s\n", error.what());
    return 1;
}
