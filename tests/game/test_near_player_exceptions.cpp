// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
// FUN_00466870 accepts FUN_00464f70's same-party TargetAlly exception and
// FUN_00464ec0's own-pet TargetPet exception after FUN_00465c60 refuses.
#include "session/near_players.hpp"

#include <search_enemy.hpp>

#include <cassert>
#include <cstdio>

int main() {
    assert(d2d::client::near_player_hit(false, true));
    assert(!d2d::client::near_player_hit(false, false));
    const d2d::rules::NearUnit allied{
        0, 52, 52, 1, false, d2d::client::near_player_hit(false, true), false
    };
    const d2d::rules::NearUnit dead_allied{
        0, 52, 52, 0, false, d2d::client::near_player_hit(false, true), false
    };
    assert(d2d::rules::near_found(allied, false, false, false, false, 52, 52));
    assert(!d2d::rules::near_found(dead_allied, false, false, false, false, 52, 52));

    assert(d2d::client::skill_cursor_exception(true, false, 101, 101, false));
    assert(!d2d::client::skill_cursor_exception(true, false, 202, 101, true));
    assert(d2d::client::skill_cursor_exception(false, true, 202, 101, true));
    assert(!d2d::client::skill_cursor_exception(false, true, 202, 101, false));
    std::puts("OK: TargetAlly and TargetPet exceptions match FUN_00464f70 / FUN_00464ec0");
}
