// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// A mouse button held on the field (apps/d2d/hand_hold.hpp): game.exe's click
// map has a held kind for each hand, 1 left (FUN_00462a20) and 4 right
// (FUN_00462c20), and the right's goes on only while 0x7a0654 holds the press
// the field took (FUN_0044c180 / FUN_0044c2c0).
#include "hand_hold.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <initializer_list>

using namespace d2d::client::hand_hold;

int main() try {
    // A latched attack goes on for either hand, Stand Still or not.
    for (const bool left : { true, false })
        for (const bool stand_still : { true, false }) {
            const Held latched = held(left, true, stand_still);
            assert(latched == Held::kLatched);
        }

    // Nothing latched: the left walks, or casts in place with Stand Still (Shift);
    // the right always casts at the cursor, as the left does with Stand Still.
    assert(held(true, false, false) == Held::kWalk);
    assert(held(true, false, true) == Held::kSkill);
    assert(held(false, false, false) == Held::kSkill);
    assert(held(false, false, true) == Held::kSkill);

    // 0x7a0654: a press on the field latches, a press on the UI doesn't,
    // the latch lasts while the button stays down and goes when it's let go.
    assert(right_latch(false, true, true, false));
    assert(!right_latch(false, true, true, true));
    assert(!right_latch(true, true, true, true));        // a new press on the UI drops the old one
    assert(right_latch(true, false, true, false));
    assert(right_latch(true, false, true, true));         // dragged over the UI, the hold goes on
    assert(!right_latch(true, false, false, false));
    assert(!right_latch(false, false, true, false));      // held without a field press: nothing
    assert(right_latch(false, true, false, false));       // a tap inside one frame: the press still counts this frame
    // A modal (Help, Configure Controls) clears the button: the latch goes with it.
    assert(!right_latch(true, false, false, false));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hand_hold: %s\n", error.what());
    return 1;
}
