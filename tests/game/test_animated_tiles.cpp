// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An Animate preset's tiles (Act 4's lava, #686): one frame shows at a time
// (FUN_0066d3b0: position += 0x50 a tick mod n << 8, frame position >> 8),
// drawn in frame 0's place; a later frame's own pick draws nothing; a plain
// tile is itself. Two animated tiles of one layer in a cell each take their
// own run of frames. No game data.
#include <dt1.hpp>
#include <gamedata.hpp>

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
    assert(animated_tile(cell, 0, 0) == &tiles[0] && animated_tile(cell, 3, 123456) == &tiles[7]);
    for (std::size_t index = 4; index < cell.size(); ++index) assert(animated_tile(cell, index, 0) == nullptr);
    // 0x50 a 40 ms tick: frame 1 from tick 4 (0x140 >> 8), frame 2 from tick 7 (0x230), back to 0 at tick 10 (0x320 % 0x300).
    assert(animated_tile(cell, 1, 3 * 40) == &tiles[1] && animated_tile(cell, 2, 3 * 40) == &tiles[4]);
    assert(animated_tile(cell, 1, 4 * 40) == &tiles[2] && animated_tile(cell, 2, 4 * 40) == &tiles[5]);
    assert(animated_tile(cell, 1, 7 * 40) == &tiles[3] && animated_tile(cell, 2, 7 * 40) == &tiles[6]);
    assert(animated_tile(cell, 1, 10 * 40) == &tiles[1]);
    std::puts("OK: one lava frame at a time in frame 0's place, 0x50 a tick; each animated tile its own frames");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_animated_tiles: %s\n", error.what());
    return 1;
}
