// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-sources: apps/d2d/teleport_aim.cpp
// test-includes: apps/d2d
#include "teleport_aim.hpp"
#include "touch_input.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client;
    const auto half = teleport_stick_drag(1.f, 0.f, .5f);
    const auto full = teleport_stick_drag(1.f, 0.f, 1.f);
    assert(half.first * 2.f == full.first && half.second == 0 && full.second == 0);
    assert(teleport_stick_drag(1.f, 0.f, 2.f) == full);
    assert(teleport_stick_drag(1.f, 0.f, -1.f).first == 0);
    TeleportAim aim;
    aim.preview = touch::Target{ 54, -1, 5.f, 6.f, false, true };
    assert(!aim.matches(aim.preview));
    aim.display();
    assert(aim.matches(aim.preview));
    auto changed = aim.preview;
    changed.x += .2f;
    assert(!aim.matches(changed));
    changed = aim.preview; changed.valid = false;
    assert(!aim.matches(changed));
    changed = aim.preview; changed.skill = 42;
    assert(!aim.matches(changed));
    aim.clear();
    assert(!aim.matches(changed) && aim.button == -1 && !aim.shown);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_teleport_aim: %s\n", error.what());
    return 1;
}
