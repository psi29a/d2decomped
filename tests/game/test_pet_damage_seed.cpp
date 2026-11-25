// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet's physical damage roll in FUN_0057b420 uses its own unit seed (+0x20)
// while the hit roll remains on the fight stream. Synthetic: no game data.

#include <combat.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    d2d::rules::Fighter pet;
    pet.phys_lo = 2 << 8;
    pet.phys_hi = 9 << 8;
    pet.attack_rating = 1000000;
    d2d::rules::Target target;
    target.level = 1;

    int hits = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        d2d::rules::Rng fight_seed{ seed * 17 }, pet_seed{ seed };
        auto expected_fight = fight_seed;
        auto expected_pet = pet_seed;
        const bool hit = expected_fight(100) < 95;
        const int expected_fine = hit ? pet.phys_lo + int(expected_pet(pet.phys_hi - pet.phys_lo)) : 0;
        const auto blow = d2d::rules::player_blow(pet, target, 99, fight_seed, {}, &pet_seed);
        assert(blow.hit == hit);
        assert(fight_seed.low == expected_fight.low && fight_seed.high == expected_fight.high);
        assert(pet_seed.low == expected_pet.low && pet_seed.high == expected_pet.high);
        if (hit) {
            assert(blow.phys_fine == expected_fine);
            ++hits;
        }
    }
    assert(hits > 0);
    std::puts("OK: a pet's physical roll uses its unit seed, apart from the fight's hit roll");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
