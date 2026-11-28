// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Venom on a Shadow (Skills 278: stats 57, 58, 101 on it, Fight::pet_buffs):
// FUN_0057b7d0 rolls the unit's whole poison stats 57 / 58 and takes a
// positive stat 101 as the length before stat 59 (tools/emu/venom_length.py),
// so the owner's copied gear poison and Venom's run together over Venom's
// length (Fight::shadow_fighter), in its swing and its kick. Synthetic rows:
// no game data.

#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    using d2d::rules::Fighter;
    // The owner's gear: 1-2 poison a frame (256ths) over 100 frames, as make_fighter keeps it.
    d2d::rules::StatSum gear{};
    gear[d2d::d2s::kPoisonMinDamage] = 256; gear[d2d::d2s::kPoisonMaxDamage] = 512; gear[d2d::d2s::kPoisonLength] = 100;
    const Fighter owner = d2d::rules::make_fighter({}, nullptr, nullptr, gear, {}, d2d::d2s::Stats{}, {}, 0, {});
    assert(owner.poison_rate == (std::pair{ 256, 512 }) && owner.poison_len == 100);
    assert(owner.elem[3] == (std::pair{ 100, 200 }));

    Fight::Pet pet;
    pet.mirror = true;
    // Without Venom on the Shadow its blow is the owner's poison as it is.
    const Fighter plain = Fight::shadow_fighter(pet, owner);
    assert(plain.elem[3] == owner.elem[3] && plain.poison_len == 100);

    // Venom's state: 4-8 a frame and length 250 (stat 101). The old branch read stat 59, which Venom
    // never sets, and so added nothing; the whole 5-10 a frame now runs over 250 frames.
    pet.buff_stats[d2d::d2s::kPoisonMinDamage] = 1024;
    pet.buff_stats[d2d::d2s::kPoisonMaxDamage] = 2048;
    pet.buff_stats[d2d::d2s::kPoisonOverrideLength] = 250;
    const Fighter venom = Fight::shadow_fighter(pet, owner);
    assert(venom.poison_len == 250);
    assert(venom.elem[3] == (std::pair{ 1280 * 250 / 256, 2560 * 250 / 256 }));
    std::printf("OK: a Shadow's Venom: %d-%d poison over %d frames\n", venom.elem[3].first, venom.elem[3].second, venom.poison_len);

    // Its kick is built the same way (FUN_0057b7d0 for the kick's record too): a bare foot, Venom alone.
    const Fighter kick = Fight::shadow_fighter(pet, Fighter{});
    assert(kick.poison_len == 250 && kick.elem[3] == (std::pair{ 1000, 2000 }));

    // A poison mastery (stat 332) on the owner scales the total after the length, as make_fighter's.
    gear[d2d::d2s::kPassivePoisonMastery] = 50;
    const Fighter mastered = Fight::shadow_fighter(pet, d2d::rules::make_fighter({}, nullptr, nullptr, gear, {}, d2d::d2s::Stats{}, {}, 0, {}));
    assert(mastered.elem[3] == (std::pair{ 1250 * 150 / 100, 2500 * 150 / 100 }));
    std::puts("OK: Venom's stat 101 is the Shadow's poison length, its 57 / 58 joined to the gear's");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_shadow_venom: %s\n", error.what());
    return 1;
}
