// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Whether a death makes a corpse by the live corpse count: FUN_0057f700's
// branch at 0x57f730, frozen from tools/emu/corpse_cap.py.

#include <wear.hpp>

#include <cassert>
#include <cstdio>
#include <iterator>

namespace {

struct Case { int corpses, made; };

constexpr Case kCases[] = {
#include "corpse_cap_cases.inc"
};

}  // namespace

int main() {
    for (const auto& test : kCases) assert(d2d::rules::death_makes_corpse(test.corpses) == (test.made != 0));
    std::printf("OK: %zu corpse-count cases match game.exe\n", std::size(kCases));
}
