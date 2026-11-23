// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A takeoff (FUN_005d9f70) hands the path FUN_00648690 with the MonStats row's +0x34 (Run) << 8: 16.16 subtiles a frame
// (FUN_006502d0), so Run / 16 subtiles = Run / 80 cells a frame. The flight is that pace's, not the sequence's time.

#include <ai.hpp>
#include <ai/units.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    assert(leap_cells_per_frame(0) == 0.f && leap_cells_per_frame(-3) == 0.f);
    assert(std::abs(leap_cells_per_frame(16) - 0.2f) < 1e-6f);     // Run 16: 1 subtile a frame
    Monster leaper;
    leaper.leap_from_x = 10; leaper.leap_from_y = 10;
    leaper.leap_to_x = 14; leaper.leap_to_y = 10;                  // 4 cells = 20 subtiles
    // Without a pace: along the sequence's time (the old flight).
    assert(std::abs(leap_done(leaper, 200, 800) - 0.25f) < 1e-6f);
    // Run 16 = 0.2 cells a frame: 5 frames (200 ms) fly 1 cell of the 4; 20 frames land it, well inside an 800 ms sequence's 20 frames.
    leaper.leap_pace = leap_cells_per_frame(16);
    assert(std::abs(leap_done(leaper, 200, 4000) - 0.25f) < 1e-5f);   // the sequence's span no longer sets it
    assert(std::abs(leap_done(leaper, 200, 100) - 0.25f) < 1e-5f);
    assert(leap_done(leaper, 800, 4000) == 1.f);
    assert(leap_done(leaper, 5000, 4000) == 1.f);
    // A faster Run (32): twice as far in the same time.
    leaper.leap_pace = leap_cells_per_frame(32);
    assert(std::abs(leap_done(leaper, 200, 4000) - 0.5f) < 1e-5f);
    std::puts("OK: a takeoff flies at MonStats Run << 8 (FUN_005d9f70 -> FUN_00648690)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_leap_pace: %s\n", error.what());
    return 1;
}
