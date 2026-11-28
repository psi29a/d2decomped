// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The pet foes after pets_turn (Fight::rebind_pet_foes): the foes were built before it, holding their pets' seeds;
// a pet it drops shifts the rest down, so each foe is found its own pet again by id, and one whose pet is gone isn't
// there to strike. No game data.

#include <ai/units.hpp>
#include <fight.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace d2d::game;

int main() {
    std::vector<Fight::Pet> pets(3);
    for (int k = 0; k < 3; ++k) { pets[std::size_t(k)].monster.id = 10 + k; pets[std::size_t(k)].monster.seed = d2d::rules::Rng{ std::uint32_t(100 + k) }; }
    std::vector<Foe> foes(4);
    foes[0].id = 1;                                               // the player: not a pet foe
    for (std::size_t k = 0; k < 3; ++k) { foes[k + 1].pet = true; foes[k + 1].alive = true; foes[k + 1].id = pets[k].monster.id; foes[k + 1].seed = &pets[k].monster.seed; }
    std::erase_if(pets, [](const Fight::Pet& pet) { return pet.monster.id == 10; });   // pets_turn drops the first: the rest move down
    Fight::rebind_pet_foes(std::span(foes).subspan(1), pets);
    assert(foes[1].seed == nullptr && !foes[1].alive);            // its pet gone
    assert(foes[2].seed == &pets[0].monster.seed && foes[2].seed->low == 101);
    assert(foes[3].seed == &pets[1].monster.seed && foes[3].seed->low == 102 && foes[3].alive);
    std::puts("test_rebind_pet_foes: ok");
    return 0;
}
