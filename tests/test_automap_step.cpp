// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// When the automap takes in what's been drawn (rules/automap_step.hpp,
// FUN_00459020's octagon on client px, past 0x4f, paused two frames by a
// teleport's FUN_00459140), and the cel picks' seed
// (0x96c8c8: {0, 0x29a} by FUN_0061fcf0, rules::Rng as FUN_0045c3e0), worked
// by hand.
#include <automap_step.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::rules::automap_moved;
    using d2d::rules::AutomapStep;
    AutomapStep step;
    assert(automap_moved(step, 80, 0) && step.last_x == 80 && step.last_y == 0);   // from (0, 0): (0 + 160) / 2 = 80
    assert(!automap_moved(step, 159, 0) && step.last_x == 80);                      // 79 across: 79
    assert(automap_moved(step, 160, 0));
    AutomapStep diagonal;
    assert(!automap_moved(diagonal, 40, 40));                                         // (40 + 80) / 2 = 60
    assert(!automap_moved(diagonal, 53, 53));                                         // 79
    assert(automap_moved(diagonal, 54, 54) && diagonal.last_x == 54);               // 81
    AutomapStep down;
    assert(!automap_moved(down, 0, 79) && automap_moved(down, 0, 80));               // (0 + 158) / 2 = 79, then 80
    assert(automap_moved(down, -20, 159));                                            // from (0, 80): (20 + 158) / 2 = 89
    std::printf("OK: the 80 px step\n");

    // A teleport (FUN_00459140: 0x7a51a4 = 2): the next two frames count
    // down and keep the last spot, the third measures from it.
    using d2d::rules::automap_frame;
    AutomapStep teleport;
    assert(automap_frame(teleport, 80, 0) && teleport.last_x == 80);
    d2d::rules::automap_pause(teleport);
    assert(!automap_frame(teleport, 1000, 0) && teleport.pause == 1 && teleport.last_x == 80);
    assert(!automap_frame(teleport, 1000, 0) && teleport.pause == 0 && teleport.last_x == 80);
    assert(automap_frame(teleport, 1000, 0) && teleport.last_x == 1000);
    assert(!automap_frame(teleport, 1000, 0));                                        // there: no step
    std::printf("OK: the teleport's pause\n");

    // The seed's first draws: 666, then 666 * 0x6ac690c5 = (277, 3365183618).
    d2d::rules::Rng seed;
    assert(seed(3) == 0 && seed.low == 666 && seed.high == 0);
    assert(seed(4) == 2 && seed.low == 3365183618u && seed.high == 277);
    assert(seed(1) == 0 && seed.low == 3979650335u);                                 // a one-cel record steps it too
    assert(seed(0) == 0 && seed.low == 3979650335u);                                 // a bound of 0 doesn't
    std::printf("OK: the cel seed\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_automap_step: %s\n", error.what());
    return 1;
}
