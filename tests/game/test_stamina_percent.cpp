// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Max stamina's op 1 (FUN_00626200 case 1): skill_staminapercent (162, Battle Orders' and Vigor's states) and
// skill_passive_staminapercent (163) each add that % of own + the items' flat +stamina; the save keeps the own.
// Made-up rows (test_data.hpp): the rule, not the game's numbers.

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
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    test.data.rules.item_info["cm1"] = { .type = "scha", .kind = 0 };
    World world(&test.data, -1, -1);
    Level open;
    open.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    open.walk.assign(80 * 80, 0);
    world.level = &open;
    auto& fight = world.fight;
    fight.enter(&open);
    auto& hero = *world.actor;
    hero.character.character_class = 4;
    hero.character.stats.values[d2d::d2s::kMaxStamina] = hero.character.stats.values[d2d::d2s::kStamina] = std::int64_t(80) << 8;
    hero.character.stats.values[d2d::d2s::kLevel] = 1;
    constexpr std::uint32_t kNow = 26 * 40;
    d2d::d2s::Item charm;
    charm.code = "cm1"; charm.panel = 1; charm.identified = true; charm.props = { { d2d::d2s::kMaxStamina, 0, 20 } };
    hero.character.items.push_back(std::move(charm));
    hero.character.panel = panel_stats(test.data, hero.character.header, hero.character.items, hero.character.stats);
    fight.update_fighters(kNow);
    const std::int64_t own = fight.own_stats().values[d2d::d2s::kMaxStamina], flat = own + 20 * 256;
    assert(own == std::int64_t(80) << 8 && hero.character.stats.values[d2d::d2s::kMaxStamina] == flat);
    hero.boost = { 0, { { d2d::d2s::kSkillStaminaPercent, 50 } }, 1000 * 40 };
    fight.update_fighters(kNow);
    assert(hero.character.stats.values[d2d::d2s::kMaxStamina] == flat + flat * 50 / 100);
    assert(fight.own_stats().values[d2d::d2s::kMaxStamina] == own);
    hero.boost = {};
    hero.character.items.clear();
    fight.update_fighters(kNow);
    assert(hero.character.stats.values[d2d::d2s::kMaxStamina] == own);
    std::puts("test_stamina_percent: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_stamina_percent: %s\n", error.what());
    return 1;
}
