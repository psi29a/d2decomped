// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's landed poison record reaches a monster foe through Fight::take_blows.
// FUN_0057ac50 uses the same state-2 hpregen rule for every target kind: the
// stronger per-tick poison replaces the old poison and its end; a weaker one
// does not. No game data: the record values are typed in.

#include <ai/units.hpp>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace d2d::game;

namespace d2d::game {

void apply_monster_foe_poison(Monster& target, int poison, int poison_ticks, std::uint32_t now_ms);

}  // namespace d2d::game

int main() {
    Monster monster;
    monster.hit_points = monster.stats.hit_points = 100;

    apply_monster_foe_poison(monster, 2048, 100, 1000);
    assert(std::abs(monster.poison_rate - 0.2) < 0.000001);
    assert(monster.poison_until == 5000);

    apply_monster_foe_poison(monster, 1024, 100, 2000);
    assert(std::abs(monster.poison_rate - 0.2) < 0.000001);
    assert(monster.poison_until == 5000);

    apply_monster_foe_poison(monster, 2048, 200, 3000);
    assert(std::abs(monster.poison_rate - 0.2) < 0.000001);
    assert(monster.poison_until == 11000);

    std::printf("test_monster_foe_poison: ok\n");
    return 0;
}
