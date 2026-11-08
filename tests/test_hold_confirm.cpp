// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The controller hold (apps/d2d/hold_confirm.hpp). It does not run Town.
#include "hold_confirm.hpp"

#include <cassert>

using d2d::client::hold_confirm::Hold;
using d2d::client::hold_confirm::Step;

int main() {
    Hold hold;
    hold.delay = 0.40f;
    assert(hold.step(true, true, true, 7, 1, 0.f) == Step::Pending);
    assert(hold.step(false, true, true, 7, 1, 0.20f) == Step::Pending);
    assert(hold.step(false, false, true, 7, 1, 0.f) == Step::Click);

    assert(hold.step(true, true, true, 7, 1, 0.f) == Step::Pending);
    assert(hold.step(false, true, true, 7, 1, 0.40f) == Step::Act);
    assert(hold.step(false, true, true, 7, 1, 0.10f) == Step::Idle);
    assert(hold.step(false, false, true, 7, 1, 0.f) == Step::Idle);

    assert(hold.step(true, true, true, 7, 1, 0.f) == Step::Pending);
    assert(hold.step(false, true, true, 8, 1, 0.10f) == Step::Idle);
    assert(hold.step(false, false, true, 8, 1, 0.f) == Step::Idle);

    assert(hold.step(true, true, true, 7, 1, 0.f) == Step::Pending);
    assert(hold.step(false, true, true, 7, 2, 0.10f) == Step::Idle);
    assert(hold.step(false, false, true, 7, 2, 0.f) == Step::Idle);

    assert(hold.step(false, true, true, 7, 1, 0.50f) == Step::Idle);
    assert(hold.step(false, false, true, 7, 1, 0.f) == Step::Idle);

    assert(hold.step(true, true, false, 7, 1, 0.f) == Step::Idle);
    return 0;
}
