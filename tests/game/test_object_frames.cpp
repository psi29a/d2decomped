// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An object's frame rate (FUN_00624390, 0x624558): Sync keeps FrameDelta;
// without it, less 1/16 plus a roll under 1/8 on the unit's seed, 0..0x7fff.

#include <gamedata.hpp>

#include <cassert>
#include <cstdint>

using namespace d2d::game;

int main() {
    assert(object_frame_delta(256, true, d2d::rules::Rng{ 7 }) == 256);
    d2d::rules::Rng seed{ 7 };
    auto roll = seed;
    assert(object_frame_delta(256, false, seed) == 256 - 16 + roll(32));
    for (std::uint32_t low = 0; low < 64; ++low) {
        const int rate = object_frame_delta(256, false, d2d::rules::Rng{ low });
        assert(rate >= 240 && rate < 272);
    }
    for (std::uint32_t low = 0; low < 64; ++low) assert(object_frame_delta(0x7fff, false, d2d::rules::Rng{ low }) <= 0x7fff);
    return 0;
}
