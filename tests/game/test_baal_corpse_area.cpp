// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Baal Corpse Explode's area (FUN_0056dcc0, 0x56dcc0, through FUN_0065aa40,
// 0x65aa40): dx² + dy² <= 40² in subtiles, only a corpse in DD (flag
// 0x1000), not the AI distance.

#include <ai.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::game;

int main() {
    Monster corpse;
    corpse.hit_points = 0;
    corpse.mode = "DD";
    assert(d2d::rules::ai_distance(35, 15) > 40);          // (15 + 70) / 2: the old test left it
    assert(baal_corpse_spent(corpse, 35, 15));             // 1225 + 225 <= 1600
    assert(baal_corpse_spent(corpse, 0, -40));
    assert(!baal_corpse_spent(corpse, 0, 41));
    assert(!baal_corpse_spent(corpse, 30, 30));            // 1800
    corpse.mode = "DT";
    assert(!baal_corpse_spent(corpse, 1, 1));              // still dying
    corpse.mode = "DD"; corpse.corpse_used = true;
    assert(!baal_corpse_spent(corpse, 1, 1));
    corpse.corpse_used = false; corpse.hit_points = 5;
    assert(!baal_corpse_spent(corpse, 1, 1));
    std::printf("baal corpse area: ok\n");
    return 0;
}
