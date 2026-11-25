// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An Animate preset's tiles (Act 4's lava, #686): one frame shows at a time
// (FUN_0066d3b0: position += 0x50 a tick mod n << 8, frame position >> 8),
// drawn in its own place in the room's array (FUN_0066d440 adds frames 1..n-1
// past its end, FUN_004de730 draws the array in index order): the shown frame's
// own pick draws its tile, every other frame's draws nothing; a plain tile is
// itself. No game data.
#include <dt1.hpp>
#include <gamedata.hpp>
#include <level.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::game;

int main() try {
    std::array<d2d::dt1::Tile, 8> tiles{};
    const auto pick = [&](int tile, std::uint8_t frame, std::uint8_t frames) {
        Level::Pick made{ 1, 0, &tiles[std::size_t(tile)] };
        made.frame = frame; made.frames = frames;
        return made;
    };
    // A plain floor, then two lava tiles (A: 1, 2, 3; B: 4, 5, 6), their frames listed after everything.
    const std::vector<Level::Pick> cell{ pick(0, 0, 0), pick(1, 0, 3), pick(4, 0, 3), pick(7, 0, 0),
                                         pick(2, 1, 3), pick(5, 1, 3), pick(3, 2, 3), pick(6, 2, 3) };
    // At tick 0 every animated tile shows frame 0, in its own place; the later frames' picks draw nothing.
    assert(animated_tile(cell, 0, 0) == &tiles[0] && animated_tile(cell, 3, 123456) == &tiles[7]);
    assert(animated_tile(cell, 1, 0) == &tiles[1] && animated_tile(cell, 2, 0) == &tiles[4]);
    for (std::size_t index = 4; index < cell.size(); ++index) assert(animated_tile(cell, index, 0) == nullptr);
    // 0x50 a 40 ms tick: frame 1 from tick 4 (0x140 >> 8), frame 2 from tick 7 (0x230), back to 0 at tick 10 (0x320 % 0x300).
    // The shown frame draws at its own pick (4 / 5 for frame 1, 6 / 7 for frame 2), after the plain tile; frame 0's draws nothing.
    assert(animated_tile(cell, 1, 3 * 40) == &tiles[1] && animated_tile(cell, 2, 3 * 40) == &tiles[4]);
    assert(animated_tile(cell, 1, 4 * 40) == nullptr && animated_tile(cell, 2, 4 * 40) == nullptr);
    assert(animated_tile(cell, 4, 4 * 40) == &tiles[2] && animated_tile(cell, 5, 4 * 40) == &tiles[5]);
    assert(animated_tile(cell, 6, 7 * 40) == &tiles[3] && animated_tile(cell, 7, 7 * 40) == &tiles[6]);
    assert(animated_tile(cell, 4, 7 * 40) == nullptr && animated_tile(cell, 6, 4 * 40) == nullptr);
    assert(animated_tile(cell, 1, 10 * 40) == &tiles[1] && animated_tile(cell, 6, 10 * 40) == nullptr);
    // A 2-frame tile (A: 1, 2) before a 3-frame one (B: 4, 5, 6): at tick 7 A shows 7 * 0x50 % 0x200 >> 8 = 0, B its frame 2.
    const std::vector<Level::Pick> mixed{ pick(1, 0, 2), pick(4, 0, 3), pick(2, 1, 2), pick(5, 1, 3), pick(6, 2, 3) };
    assert(animated_tile(mixed, 0, 7 * 40) == &tiles[1] && animated_tile(mixed, 2, 7 * 40) == nullptr);
    assert(animated_tile(mixed, 4, 7 * 40) == &tiles[6] && animated_tile(mixed, 1, 7 * 40) == nullptr);
    assert(animated_tile(mixed, 3, 4 * 40) == &tiles[5] && animated_tile(mixed, 2, 4 * 40) == &tiles[2]);   // at tick 4 both show frame 1
    std::puts("OK: one lava frame at a time, drawn at its own pick, 0x50 a tick");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_animated_tiles: %s\n", error.what());
    return 1;
}
