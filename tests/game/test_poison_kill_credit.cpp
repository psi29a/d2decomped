// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster that dies to poison is its poison's owner's kill (the state list's owner, FUN_0057ac50 ->
// FUN_005a6920 -> FUN_0057e7b0): in a two-player game the second player's poison, the first far off, gives
// the second the experience; a monster's poison gives nobody any. Made-up rows (test_data.hpp): the rule,
// not the game's numbers.

#include <ai/units.hpp>
#include <character.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    test.monster(1, "rotter");
    test.data.exp_next = { 500, 1500, 3750 };
    test.data.exp_ratio = { 1024, 1024, 1024 };
    World world(&test.data, -1, -1);
    Level open;
    open.ds1 = d2d::ds1::Map(32, 32, 1, 1);
    open.walk.assign(160 * 160, 0);
    world.level = &open;
    world.fight.enter(&open);
    Character character;
    character.character_class = 0;
    character.name = "Venomous";
    character.stats.values[d2d::d2s::kLevel] = 1;
    character.stats.values[d2d::d2s::kLife] = character.stats.values[d2d::d2s::kMaxLife] = std::int64_t(50) << 8;
    world.actor->character = character;
    auto& second = world.join(character);
    auto& lead = world.lead();
    const std::uint32_t now = 40000;
    world.fight.monsters.clear();
    lead.player.x = 10; lead.player.y = 10;
    second.player.x = 60; second.player.y = 60;                       // far apart: no sharing (0x1900)
    const auto poisoned = [&](int owner) {
        Monster rotter;
        rotter.type = 1; rotter.id = 0x700 + int(world.fight.monsters.size());
        rotter.unit.x = 60.5f; rotter.unit.y = 60.f;
        rotter.hit_points = 1; rotter.stats.hit_points = 100; rotter.stats.level = 1; rotter.stats.exp = 100;
        rotter.poison_rate = 1.0; rotter.poison_until = now + 4000; rotter.poison_owner = owner;
        world.fight.monsters.push_back(rotter);
        return world.fight.monsters.size() - 1;
    };
    const auto exp_of = [](const Fight::Hero& hero) { return hero.character.stats.values[d2d::d2s::kExp]; };
    const auto lead_before = exp_of(lead), second_before = exp_of(second);
    const auto index = poisoned(second.id);
    world.fight.monster_dots(now, 0.04f);
    assert(!world.fight.monsters[index].alive());
    assert(exp_of(second) > second_before && exp_of(lead) == lead_before);
    std::printf("the second player's poison kill: +%lld experience to them, none to the lead\n", static_cast<long long>(exp_of(second) - second_before));
    const auto both_before = exp_of(second);
    const auto monster_kill = poisoned(-1);
    world.fight.monster_dots(now, 0.04f);
    assert(!world.fight.monsters[monster_kill].alive() && exp_of(second) == both_before && exp_of(lead) == lead_before);
    std::puts("test_poison_kill_credit: ok (a monster's poison credits nobody)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_poison_kill_credit: %s\n", error.what());
    return 1;
}
