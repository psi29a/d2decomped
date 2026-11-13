// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The Blade Sentinel's AI control at its start (FUN_005ea510, MonAI 102's
// init): +0x14 -1, +0x18 1, +0x1c 0. Fight::summon_one takes it from
// rules::blade_sentinel_init. Needs no game data.

#include <pets.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>

// Pins the start values against FUN_005ea510's three writes.
int main() try {
    const std::array<int, 3> start = d2d::rules::blade_sentinel_init();
    assert(start[0] == -1 && start[1] == 1 && start[2] == 0);
    std::printf("OK: the Blade Sentinel's init (FUN_005ea510)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
