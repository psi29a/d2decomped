// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Charge's fixed-point velocity (FUN_005cf6b0) after FUN_00620e80's run
// bonus and chill's stat-67 penalty, captured by tools/emu/charge_velocity.py.

#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    struct Case { int walk, run, velocity_percent, chilled, param1, expected; };
    constexpr Case cases[] = {
#include "charge_velocity_cases.inc"
    };
    for (const auto& row : cases)
        assert(d2d::rules::charge_velocity(row.walk, row.run, row.velocity_percent, row.chilled != 0, row.param1) == row.expected);
    std::printf("OK: %zu Charge velocities as FUN_005cf6b0\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_charge_velocity: %s\n", error.what());
    return 1;
}
