// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The party screen's hostile button (rules::hostile_toggle, game.exe 1.14d
// FUN_005a5e50) against stock game.exe's own run of it, frozen by
// tools/emu/hostile_toggle.py inc: the town, level 9 and 60 s stamp tests,
// the 32-bit clock's wrap included. No game data needed at runtime.
#include <hostile_toggle.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    struct Case { int in_room, level_id, self_level, other_level, other_again, hostile; std::uint32_t stamp, now, on; int outcome; };
    const Case cases[] = {
#include "hostile_toggle_cases.inc"
    };
    for (const auto& row : cases) {
        d2d::rules::HostileToggleInput in;
        in.in_room = row.in_room != 0; in.room_level_id = row.level_id; in.self_level = row.self_level;
        in.other_level = row.other_level; in.other_level_again = row.other_again; in.hostile = row.hostile != 0;
        in.pair_stamp = row.stamp; in.now = row.now; in.requested = row.on;
        const int got = int(d2d::rules::hostile_toggle(in));
        if (got != row.outcome)
            std::fprintf(stderr, "FAIL: room %d level %d levels %d/%d hostile %d stamp %#x now %#x on %u: d2d %d, game.exe %d\n", row.in_room, row.level_id,
                         row.self_level, row.other_level, row.hostile, row.stamp, row.now, row.on, got, row.outcome);
        assert(got == row.outcome);
    }
    std::printf("OK: %zu hostile buttons as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hostile_toggle: %s\n", error.what());
    return 1;
}
