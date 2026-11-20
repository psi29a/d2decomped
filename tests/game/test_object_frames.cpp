// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An object's frame rate (FUN_00624390, 0x624558): Sync keeps FrameDelta;
// without it, less 1/16 plus a roll under 1/8 on the unit's seed, 0..0x7fff.

#include <gamedata.hpp>

#include <cassert>
#include <cstdint>
#include <utility>

using namespace d2d::game;

// Checks the Sync, roll and per-unit cache rules of the frame rate, and the peek.
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

    Npc object;
    object.root = "objects";
    object.x = 12.5f;
    object.y = 7.5f;
    object.frame_delta[0] = object.frame_delta[1] = 256;
    d2d::rules::Rng client_seed{ std::uint32_t(object.x * 5) << 16 | std::uint32_t(object.y * 5) };
    const int first = 240 + client_seed(32);
    const int second = 240 + client_seed(32);
    assert(object_frame_delta(object, 0) == first);
    assert(object_frame_delta(object, 0) == first);       // drawing the same mode does not step again
    assert(object_frame_delta(object, 1) == second);      // a mode set consumes the next client-seed draw
    assert(object_frame_delta(object, 1) == second);
    assert(object_frame_delta(object, 0) == 240 + client_seed(32));

    // A timing query (object_op_ticks) peeks: it neither changes the cached
    // mode nor steps the seed, however often it is asked.
    Npc timed = std::move(object);
    timed.client_seeded = false;
    timed.client_mode = -1;
    const int peeked = object_frame_delta_peek(timed, 1);
    assert(object_frame_delta_peek(timed, 1) == peeked);
    assert(!timed.client_seeded && timed.client_mode == -1);
    assert(object_frame_delta(timed, 1) == peeked);        // the draw that follows is the rate it peeked
    assert(object_frame_delta_peek(timed, 1) == peeked);
    const auto seed_after = timed.client_seed.low;
    (void)object_frame_delta_peek(timed, 2);
    assert(timed.client_mode == 1 && timed.client_seed.low == seed_after);
    return 0;
}
