// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Bone Wall's makers (FUN_005c58b0, do 60): two bonewallmaker missiles from the first piece's subtile across the caster's line,
// each with calc2 / 2 pieces; none in a town or for a count of 1 or less. bone_wall_cases.inc is game.exe's own launch on
// 2000 casts (tools/emu/bone_wall.py). The makers' laying (do 13, FUN_005aeda0) rides on d2d's missile flight.

#include <pets.hpp>

#include <cstdio>
#include <iterator>
#include <utility>

namespace {

struct Case { int caster_x, caster_y, piece_x, piece_y, calc2, town, count, a_x, a_y, b_x, b_y; };
constexpr Case kCases[] = {
#include "bone_wall_cases.inc"
};

}   // namespace

int main() {
    int bad = 0;
    for (const auto& one : kCases) {
        const auto makers = d2d::rules::bone_wall_makers(one.caster_x, one.caster_y, one.piece_x, one.piece_y, one.calc2, one.town != 0);
        const bool same = makers.count == one.count
                       && (one.count == 0 || (makers.aims[0] == std::pair{ one.a_x, one.a_y } && makers.aims[1] == std::pair{ one.b_x, one.b_y }));
        if (!same && ++bad <= 5)
            std::printf("FAIL: caster %d,%d piece %d,%d calc2 %d town %d: d2d %d (%d,%d) (%d,%d), game.exe %d (%d,%d) (%d,%d)\n", one.caster_x, one.caster_y,
                        one.piece_x, one.piece_y, one.calc2, one.town, makers.count, makers.aims[0].first, makers.aims[0].second, makers.aims[1].first,
                        makers.aims[1].second, one.count, one.a_x, one.a_y, one.b_x, one.b_y);
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu Bone Wall casts as FUN_005c58b0 launches them\n", std::size(kCases));
    return 0;
}
