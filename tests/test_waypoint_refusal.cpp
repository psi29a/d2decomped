// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The waypoint command's 10 s window after a hostile declaration
// (rules::waypoint_refused, FUN_0054c5d0 over FUN_0055b720's stamp) against
// stock 1.14d's, run by tools/emu/wp_refuse.py inc: every stamp and delta,
// the 32-bit clock's wrap included. No game data needed at runtime.
#include <waypoint_refusal.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>

int main() try {
    struct Case { std::uint32_t stamp, delta; int refused; };
    const Case cases[] = {
#include "wp_refuse_cases.inc"
    };
    for (const auto& row : cases) {
        const int refused = d2d::rules::waypoint_refused(row.stamp, row.stamp + row.delta) ? 1 : 0;
        if (refused != row.refused)
            std::fprintf(stderr, "FAIL: declared at %#x, 0x49 %#x later: d2d %d, game.exe %d\n", row.stamp, row.delta, refused, row.refused);
        assert(refused == row.refused);
    }
    assert(!d2d::rules::waypoint_refused(std::nullopt, 0));   // never declared: nothing refuses
    std::printf("OK: %zu waypoint commands as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_waypoint_refusal: %s\n", error.what());
    return 1;
}
