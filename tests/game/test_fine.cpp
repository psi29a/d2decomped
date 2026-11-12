// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A unit's place is game.exe's 16.16 subtiles (path +0 / +4, Fine): a walk
// steps it as rules::walk_frame does from wherever it stands, a centre is
// exact, and it reads as cells on its own subtile even where a float can't
// hold it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missiles.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    assert(Fine::centre(105).raw == (105u << 16) + 0x8000 && Fine::centre(105).subtile() == 105);
    assert(float(Fine::centre(105)) == (105.f + 0.5f) / 5);
    assert(Fine(21.1f).raw == Fine::centre(105).raw);                 // cells in: the nearest 1/65536 subtile

    // Read as cells, a place stays on its subtile, both ends of it, far out too.
    for (const int sub : { 3, 105, 355, 1000, 4000 })
        for (const std::uint32_t part : { 0u, 1u, 0x7fffu, 0xfffeu, 0xffffu }) {
            Fine fine;
            fine.raw = (std::uint32_t(sub) << 16) + part;
            assert(int(std::floor(float(fine) * 5)) == sub);
        }

    // A walk from off a centre steps from exactly there (nothing snaps it on).
    Level level;
    level.ds1 = d2d::ds1::Map(40, 40, 1, 1);
    level.walk.assign(std::size_t(200 * 200), 0);
    UnitState unit;
    unit.x.raw = (100u << 16) + 0x8003; unit.y = Fine::centre(100);
    std::uint32_t x = unit.x.raw, y = unit.y.raw;
    std::size_t point = 0;
    const std::pair<int, int> points[] = { { 110, 100 } };
    assert(d2d::rules::walk_frame(x, y, points, point, 0x400, [](int, int) { return false; }));
    unit.path = { { float(Fine::centre(110)), float(Fine::centre(100)) } };
    assert(follow_path(level, unit, 0x400));
    assert(unit.x.raw == x && unit.y.raw == y && x != (100u << 16) + 0x8000 + 0x4000);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_fine: %s\n", error.what());
    return 1;
}
