// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// When a click's own target search runs: rules::skill_search_goes against game.exe's
// FUN_00467880 run by its own code (tools/emu/search_gate.py writes the cases): a held
// frame of the left button without shift while the player walks runs no search.
#include <click_spot.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    struct Case { std::uint32_t flags; int walking; int ran; };
    constexpr Case cases[]{
#include "search_gate_cases.inc"
    };
    int differ = 0;
    for (const auto& sample : cases) {
        const bool goes = skill_search_goes(sample.flags, sample.walking != 0);
        if (goes != (sample.ran != 0)) {
            std::printf("differ: flags %#x walking %d: game.exe %d, d2d %d\n", sample.flags, sample.walking, sample.ran, int(goes));
            ++differ;
        }
    }
    std::printf("%zu cases, %d differ\n", sizeof(cases) / sizeof(cases[0]), differ);
    assert(differ == 0);
    return differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_search_gate: %s\n", error.what());
    return 1;
}
