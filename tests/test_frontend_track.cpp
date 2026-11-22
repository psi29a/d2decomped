// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_00514990's frontend playlist pick: track 0 after FUN_005148f0
// turns it on (0x514a21), else the roll stepping past played tracks.
#include <title_rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using d2d::client::frontend_track;
    std::array<bool, 8> played{};
    bool restart = true;
    assert(frontend_track(played, restart, 5) == 0 && !restart && played[0]);
    assert(frontend_track(played, restart, 5) == 5);
    assert(frontend_track(played, restart, 13) == 6);   // 13 % 8 = 5, played: step to 6
    assert(frontend_track(played, restart, 0) == 1);    // 0 played: step to 1
    for (int roll = 0; roll < 4; ++roll) frontend_track(played, restart, 0);
    assert(played == (std::array<bool, 8>{ true, true, true, true, true, true, true, true }));
    assert(frontend_track(played, restart, 3) == 3 && !played[0]);   // all played: start over
    restart = true;   // back from a game: the flags clear and track 0 plays
    assert(frontend_track(played, restart, 3) == 0 && !played[3]);
    std::puts("frontend track: ok");
}
