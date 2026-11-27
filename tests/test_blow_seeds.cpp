// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Whose seed a blow's rolls step. The to-hit is the attacker's (FUN_0057d9b0,
// 0x57db35: ECX the attacker, a missile's owner), as are the damage, the
// crits (FUN_0057b7d0 0x57b8d0..0x57b916), Crushing Blow (FUN_005bffc0) and
// Open Wounds (FUN_005bfe60); the block (FUN_0057dfb0 0x57e021), Weapon
// Block, dodge, avoid and evade (FUN_0057dd60 from 0x57ddb6) the defender's
// (ESI, the struck unit); a missile's damage its own (FUN_005a89a0 0x5a89f5).
// rules::player_blow and rules::monster_blow, no game data.
#include <combat.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using d2d::rules::Rng;

namespace {

// How many steps take `from` to `after`, up to 8; -1 past that.
int steps(Rng from, const Rng& after) {
    for (int count = 0; count <= 8; ++count) {
        if (from.low == after.low && from.high == after.high) return count;
        from.next();
    }
    return -1;
}

}   // namespace

int main() try {
    // A monster's swing at a unit that blocks it: the to-hit on the attacker's seed (95 % to-hit, the
    // seed picked to pass), the block on the defender's.
    d2d::rules::Fighter defender = d2d::rules::simple_fighter(1, 2, 0, 0);
    defender.block = 100;
    d2d::rules::MonStats attacker;
    attacker.to_hit = 100000; attacker.a1_min = 2; attacker.a1_max = 5; attacker.level = 30;
    Rng monster_seed{ 7 }, struck_seed{ 11 };
    const Rng monster_before = monster_seed, struck_before = struck_seed;
    auto taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, attacker, false, monster_seed, false, false, false, &struck_seed);
    if (!taken.blocked) {
        // The to-hit missed (its 5 %): the defender never rolled.
        assert(steps(monster_before, monster_seed) == 1 && steps(struck_before, struck_seed) == 0);
        std::printf("OK: a monster's missed swing steps only its own seed\n");
    } else {
        assert(steps(monster_before, monster_seed) == 1 && steps(struck_before, struck_seed) == 1);
        std::printf("OK: a monster's blocked swing: the to-hit on its seed, the block on the defender's\n");
    }
    // No block, a hit: the to-hit and the damage on the attacker's, nothing on the defender's (dodge 0).
    defender.block = 0;
    monster_seed = Rng{ 3 }; struck_seed = Rng{ 5 };
    const Rng hit_before = monster_seed, unstruck_before = struck_seed;
    taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, attacker, false, monster_seed, false, false, false, &struck_seed);
    assert(taken.hit && steps(hit_before, monster_seed) == 2 && steps(unstruck_before, struck_seed) == 0);
    std::printf("OK: a monster's landed swing: the to-hit and the damage on its own seed\n");
    // A missile's: its damage on its own seed, the to-hit on its owner's.
    Rng owner{ 21 }, own{ 23 };
    struck_seed = Rng{ 25 };
    const Rng owner_before = owner, own_before = own;
    taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, attacker, false, owner, true, false, false, &struck_seed, &own);
    assert(taken.hit && steps(owner_before, owner) == 1 && steps(own_before, own) == 1);
    std::printf("OK: a monster's missile: the to-hit on its owner's seed, the damage on its own\n");

    // A player's swing at a blocking monster: the block on the monster's seed; a set crit rolls nothing.
    d2d::rules::Fighter player = d2d::rules::simple_fighter(10, 20, 100000, 0);
    player.critical = 50; player.deadly = 50;
    d2d::rules::Target target;
    target.block = 100; target.level = 1; target.hit_points = target.max_hp = 1000;
    Rng player_seed{ 31 }, monster{ 37 };
    const Rng player_before = player_seed, target_before = monster;
    auto blow = d2d::rules::player_blow(player, target, 30, player_seed, {}, nullptr, &monster);
    assert(blow.blocked && steps(player_before, player_seed) == 1 && steps(target_before, monster) == 1);
    std::printf("OK: a player's blocked swing: the to-hit on its seed, the block on the monster's\n");
    target.block = 0;
    d2d::rules::Swing swing;
    swing.missile = true;
    swing.crit = true;
    Rng missile_seed{ 41 };
    player_seed = Rng{ 43 };
    const Rng shot_before = player_seed, missile_before = missile_seed;
    blow = d2d::rules::player_blow(player, target, 30, player_seed, swing, &missile_seed, &monster);
    assert(blow.hit && blow.deadly);                                  // the crit as it was made
    assert(steps(shot_before, player_seed) == 1);                     // only the to-hit
    assert(steps(missile_before, missile_seed) == 1);                 // the physical
    std::printf("OK: a player's missile: the to-hit on the owner's seed, the physical on its own, its crit as made\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
