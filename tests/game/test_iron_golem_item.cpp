// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Iron Golem's item (do 57, FUN_005c5250): its check (FUN_005c32a0) wants a
// ground item of a metal base (Items bitfield1 & 2), identified; the do takes
// it off the floor and the golem wears it (FUN_005606b0) before its stats
// (FUN_005c50c0), so the item's damage and stats are the golem's. No metal
// item at the spot: no golem. Synthetic rows: no game data.
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& golem_type = fixture.monster(0, "irongolem");
    for (auto& per_difficulty : golem_type.diff) { per_difficulty.min_hp = per_difficulty.max_hp = 100; per_difficulty.a1_min = 5; per_difficulty.a1_max = 10; }
    auto& skill = fixture.skill(90, "IronGolem");
    skill.srvdofunc = d2d::rules::ServerDoFunction::kIronGolem;
    skill.summon = "irongolem";
    skill.pettype = "golem";
    skill.petmax.code = { { d2d::rules::Calc::Const, 1 } };
    auto& data = fixture.data;
    data.rules.item_base["axe"].bitfield1 = 2;
    data.rules.item_base["axe"].mindam = 4;
    data.rules.item_base["axe"].maxdam = 11;
    data.rules.item_info["axe"].type = "axe";
    data.rules.item_info["axe"].kind = 2;
    data.rules.types["axe"].body = { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm };
    data.rules.item_base["cap"].mindam = 0;  // no metal bit
    data.rules.item_info["cap"].type = "helm";
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    loot.ground_level = level;
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    hero.player.x = hero.player.y = 10;

    Loot::GroundItem cap;
    cap.id = 1;
    cap.item.code = "cap";
    cap.item.identified = true;
    cap.x = cap.y = 12.1f;
    loot.ground = { std::move(cap) };
    assert(fight.golem_item_at(12.1f, 12.1f) < 0);   // not metal
    fight.summon(skill, 1, 12.1f, 12.1f, 1000);
    assert(hero.pets.empty() && loot.ground.size() == 1);

    Loot::GroundItem axe;
    axe.id = 2;
    axe.item.code = "axe";
    axe.item.identified = false;
    axe.x = axe.y = 14.1f;
    loot.ground.push_back(std::move(axe));
    assert(fight.golem_item_at(14.1f, 14.1f) < 0);   // not identified
    loot.ground.back().item.identified = true;
    loot.ground.back().item.props.push_back({ d2d::d2s::kMaxDamage, 0, 3 });
    loot.ground.back().item.props.push_back({ d2d::d2s::kMaxLifePercent, 0, 20 });
    d2d::d2s::Item jewel;                                            // a socketed jewel's stats go with the axe
    jewel.code = "jew";
    jewel.props.push_back({ d2d::d2s::kMaxDamage, 0, 2 });
    loot.ground.back().item.socketed = true;
    loot.ground.back().item.sockets = 1;
    loot.ground.back().item.socketed_items.push_back(std::move(jewel));
    assert(fight.golem_item_at(14.1f, 14.1f) == 1);
    fight.summon(skill, 1, 14.6f, 14.3f, 2000);                     // a cast beside it, on another subtile
    assert(hero.pets.size() == 1 && loot.ground.size() == 1 && loot.ground[0].id == 1);   // the axe left the floor
    const auto& golem = hero.pets.front().monster;
    assert(golem.unit.x == 14.1f && golem.unit.y == 14.1f);          // made on the axe (FUN_005c5250)
    assert(golem.stats.a1_min == 5 + 4 && golem.stats.a1_max == 10 + 11 + 3 + 2);   // a one-hand weapon's damage, its max damage and the jewel's
    assert(golem.max_life_fine() == 120 * 256);                      // 100 life, the axe's 20 % (calc1 none)
    assert(!fight.golem_item);
    std::puts("OK: Iron Golem takes a metal ground item and wears it (FUN_005c5250)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
