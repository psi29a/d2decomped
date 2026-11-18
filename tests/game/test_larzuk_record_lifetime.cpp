// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Larzuk's quest-record made byte (record 0x1f data +0x15 in FUN_00587840)
// belongs to the game-wide Spawning state, so a second Harrogath level state
// cannot make him again.

#include <quests_act5.hpp>

#include <cassert>
#include <cstdio>

int main() {
    d2d::rules::MarkerRecords game_records;
    int makes = 0;
    const auto free_spot = [](int&, int&, int&) { return true; };
    const auto make_at = [&](int, int, int, int) { ++makes; return true; };

    assert(d2d::rules::larzuk_marker(game_records.larzuk_made, 0, 10, 20, free_spot, make_at));
    assert(game_records.larzuk_made && makes == 1);

    // A replacement ObjectRooms loads the same game record, rather than a
    // made byte stored in the old level object.
    bool rebuilt_level_made = game_records.larzuk_made;
    assert(!d2d::rules::larzuk_marker(rebuilt_level_made, 1, 30, 40, free_spot, make_at));
    assert(makes == 1);

    std::printf("OK: Larzuk's made byte survives a replacement level state\n");
}
