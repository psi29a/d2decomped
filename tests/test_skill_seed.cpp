// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// First-time controller setup is a UX decision, not an original-game claim.
#include "skillpad.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <string>

int main() try {
    using namespace d2d::client::skillpad;
    const std::array<int, 8> hotkeys{39, 36, 54, 42, 39, -1, 44, 45};
    std::array<bool, 7> enabled{};
    enabled.fill(true);
    const auto seeded = initial_sheet(36, 54, hotkeys, enabled);
    assert(seeded.main == 36 && seeded.slot(false, 5) == 54);
    assert(seeded.slot(false, 0) == 39 && seeded.slot(false, 1) == 42);
    assert(seeded.slot(false, 2) == 44 && seeded.slot(false, 3) == 45);
    assert(seeded.slot(false, 4) == kEmpty);
    assert(read(write(seeded)) == seeded);
    assert(write(seeded).find("initialized 1") != std::string::npos);
    enabled[0] = enabled[5] = enabled[6] = false;
    const auto remapped = initial_sheet(36, 54, hotkeys, enabled);
    assert(remapped.main == kEmpty && remapped.slot(false, 5) == kEmpty);
    assert(remapped.slot(false, 0) == kEmpty && remapped.slot(false, 1) == 39);
    const auto legacy = read("main 54\ns0 -1\n");
    assert(legacy.slot(false, 0) == kEmpty && legacy.trigger(true) == 54);
    assert(read(write(legacy)) == legacy);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_skill_seed: %s\n", error.what());
    return 1;
}
