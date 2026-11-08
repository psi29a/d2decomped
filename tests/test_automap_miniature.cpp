// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The town miniatures (rules/automap_miniature.hpp): FUN_004591a0's grids,
// worked by hand from its constants. Lut Gholein 5 x 4 of 160 x 100 less
// the cels at 0x6d6608; the Pandemonium Fortress 2 x 2 of 136 x 90;
// Harrogath 2 x 2 of 180 x 170; each round the level's middle.
#include <automap_miniature.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using d2d::rules::MiniatureCell;

bool same(const std::vector<MiniatureCell>& got, const std::vector<MiniatureCell>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t index = 0; index < got.size(); ++index)
        if (got[index].cel != want[index].cel || got[index].x != want[index].x || got[index].y != want[index].y) return false;
    return true;
}

}  // namespace

int main() {
    using d2d::rules::automap_miniature;
    using d2d::rules::miniature_cel_file;
    static_assert(miniature_cel_file(0x28) == 1 && miniature_cel_file(0x67) == 2 && miniature_cel_file(0x6d) == 3);
    static_assert(miniature_cel_file(1) == 0 && miniature_cel_file(0x29) == 0);
    assert(automap_miniature(1, 0, 10, 10).empty());   // FUN_004591a0 is fatal (0x751) on any other level
    std::printf("OK: cel files\n");

    // Lut Gholein round (0, 0), LutW (file 1): cels 0..19 from (-453, -119)
    // (x: -(160 * 5 / 2 + 0x35); y: -(100 * 4 / 2) + 0x51), 160 x 100 a step;
    // 0, 10, 15, 16, 19 skipped where they'd sit.
    const std::vector<MiniatureCell> lut_west{ { 1, -293, -119 }, { 2, -133, -119 }, { 3, 27, -119 }, { 4, 187, -119 },
                                               { 5, -453, -19 },  { 6, -293, -19 },  { 7, -133, -19 }, { 8, 27, -19 }, { 9, 187, -19 },
                                               { 11, -293, 81 },  { 12, -133, 81 },  { 13, 27, 81 },   { 14, 187, 81 },
                                               { 17, -133, 181 }, { 18, 27, 181 } };
    assert(same(automap_miniature(0x28, 1, 0, 0), lut_west));
    // LutN (any other file): cels 20..39 on the same grid, less 20, 25, 30, 35, 36, 39.
    const auto lut_north = automap_miniature(0x28, 2, 0, 0);
    assert(lut_north.size() == 14 && lut_north.front().cel == 21 && lut_north.front().x == -293 && lut_north.back().cel == 38 && lut_north.back().y == 181);
    std::printf("OK: Lut Gholein, both files\n");

    // The Pandemonium Fortress: (-(136 + -3), -(90) + 50) = (-133, -40).
    assert(same(automap_miniature(0x67, 0, 0, 0), { { 0, -133, -40 }, { 1, 3, -40 }, { 2, -133, 50 }, { 3, 3, 50 } }));
    // Harrogath: (-(180 + -20), -(170) + 155) = (-160, -15).
    assert(same(automap_miniature(0x6d, 0, 0, 0), { { 0, -160, -15 }, { 1, 20, -15 }, { 2, -160, 155 }, { 3, 20, 155 } }));
    std::printf("OK: the Pandemonium Fortress and Harrogath\n");

    // The middle in act tiles goes to client px (FUN_00643310), then / 10:
    // (10, 4) -> (480, 560) -> (48, 56); (0, 3) -> (-240, 120) -> (-24, 12).
    assert(automap_miniature(0x67, 0, 10, 4).front().x == 48 - 133 && automap_miniature(0x67, 0, 10, 4).front().y == 56 - 40);
    assert(automap_miniature(0x67, 0, 0, 3).front().x == -24 - 133 && automap_miniature(0x67, 0, 0, 3).front().y == 12 - 40);
    std::printf("OK: placed round the level's middle\n");
    return 0;
}
