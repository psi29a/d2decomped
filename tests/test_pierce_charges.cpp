// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A Pierce row's charges (rules::pierce_charges) against stock 1.14d's
// FUN_0059f940, run by tools/emu/pierce_charges.py (pierce_charges_cases.inc):
// draws on {pierce_idx, 666} while under skill_pierce + item_pierce, 4 at most.
#include <missiles.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    struct Case { int skill, item; std::uint32_t seed; int charges; };
    const Case cases[] = {
#include "pierce_charges_cases.inc"
    };
    for (const auto& row : cases) {
        const int charges = d2d::rules::pierce_charges(row.skill + row.item, row.seed);
        if (charges != row.charges)
            std::fprintf(stderr, "FAIL: skill %d item %d seed %u: d2d %d, game.exe %d\n", row.skill, row.item, row.seed, charges, row.charges);
        assert(charges == row.charges);
    }
    std::printf("OK: %zu pierce charges as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pierce_charges: %s\n", error.what());
    return 1;
}
