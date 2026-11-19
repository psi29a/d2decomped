// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The gold box's spinner held (apps/d2d/spinner_step.hpp, FUN_004bbfb0): its
// step grows with the frames drawn (FUN_004bbe70's +0x40), not the time alone.
#include "spinner_step.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using d2d::client::spinner_step;
    assert(spinner_step(0, 0) == 1 && spinner_step(1000, 60) == 1);   // the first second: 1
    assert(spinner_step(1001, 64) == 2 && spinner_step(2001, 64) == 4 && spinner_step(3001, 64) == 16);
    assert(spinner_step(4096, 64) == 16 && spinner_step(4097, 64) == 128);   // past 0x1000: (ms >> 11) x draws
    assert(spinner_step(2500, 150) == 9 && spinner_step(2500, 60) == 3);     // a faster draw rate steps faster
    std::puts("OK: the spinner's step by its draws");
    return 0;
}
