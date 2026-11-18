// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005dd7f0 chooses a chest trap's nearest live player-list entry under
// Aidist 35; FUN_005fb5b0 / FUN_005fb900 / FUN_005fb9b0 then require that
// picked entry to be within aip1.

#include <monsters.hpp>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using d2d::rules::SearchFoe;
    using d2d::rules::trap_target;

    const std::array<SearchFoe, 2> nearest{{ { 25, false, false, false }, { 10, false, false, false } }};
    assert(trap_target(nearest, 25) == 1);

    const std::array<SearchFoe, 1> boundary{{ { 25, false, false, false } }};
    assert(trap_target(boundary, 25) == 0);
    assert(trap_target(boundary, 20) == -1);

    const std::array<SearchFoe, 2> dead_player_live_pet{{ { 4, false, false, true }, { 12, true, false, false } }};
    assert(trap_target(dead_player_live_pet, 20) == 1);

    const std::array<SearchFoe, 1> search_limit{{ { 35, false, false, false } }};
    assert(trap_target(search_limit, 40) == -1);

    std::puts("OK: chest trap target search and aip1 gate");
    return 0;
}
