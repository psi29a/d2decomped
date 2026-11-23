// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0052d240: an unheld room1 is released after the eleventh sweep; a
// depth-1 room held by a player resets its age and remains live.

#include <spawning.hpp>

#include <cassert>
#include <cstddef>
#include <vector>

using namespace d2d::game;

// Ages two live rooms through the sweeps and checks the release and the repopulation.
int main() {
    Spawning::LevelState state;
    state.rooms_up = { true, true };
    state.live = { true, true };
    state.idle_sweeps = { 10, 10 };
    state.order = { 0, 1 };
    state.client_order = { 0, 1 };

    assert(!age_live_room(state, 0, true));
    assert(age_live_room(state, 1, false));
    assert(state.live[0] && state.idle_sweeps[0] == 0);
    assert(!state.live[1] && state.rooms_up[1]);
    assert(state.order == std::vector<std::size_t>{ 0 });

    // A released, already-populated room can be brought up without clearing
    // the population bit; this is what prevents preset units rolling twice.
    assert(state.rooms_up[1] && !age_live_room(state, 1, false));
    return 0;
}
