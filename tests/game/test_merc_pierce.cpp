// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A merc's Pierce missile (FUN_0059f940, an owner of type 1): charges from
// its gear's skill_pierce + item_pierce, seeded by its stat 0x148. A hit
// spends one while the merc still has either stat (FUN_005adf10,
// FUN_005ada80), and the player's stats do not stand in for the merc's.
// No game data: the row, the skill and the gear are typed in.

#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <missiles.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    game_data.skills.rows.resize(2);
    game_data.skills.rows[1].id = 1;
    auto& row = game_data.missiles["pierce"];
    auto& arrow_row = game_data.missiles["arrow"];
    row.pierce = true;
    row.collide_kill = true;
    arrow_row.pierce = true;
    game_data.monsters.types.resize(1);

    const Level* level = nullptr;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    d2d::d2s::Item belt;
    belt.location = d2d::d2s::item_location::kEquipped;
    belt.slot = d2d::d2s::body_location::kBelt;
    belt.props.push_back({ .stat = d2d::d2s::kItemPierce, .value = 20 });
    belt.props.push_back({ .stat = d2d::d2s::kSkillPierce, .value = 40 });
    hero.character.merc_items.push_back(belt);
    hero.merc_pierce_idx = 1;                                           // {1, 666} under 60: 3 draws land
    hero.pierce_idx = 1;
    hero.psum[d2d::d2s::kSkillPierce] = 40;
    hero.psum[d2d::d2s::kItemPierce] = 20;

    const auto& skill = game_data.skills.rows[1];
    fight.casting_merc = false;
    Missile players = fight.launch(row, skill, 1, 0, 0, 1.f, 0.f, row.range, 7000);
    fight.pending.pop_back();
    assert(players.pierce == 3 && !players.by_merc);                    // the player's own stats and stat 0x148

    hero.psum[d2d::d2s::kSkillPierce] = 0;
    hero.psum[d2d::d2s::kItemPierce] = 0;
    fight.casting_merc = true;
    Missile arrow = fight.launch(row, skill, 1, 0, 0, 1.f, 0.f, row.range, 7000);
    fight.pending.pop_back();
    fight.casting_merc = false;
    assert(arrow.by_merc && arrow.pierce == 3 && d2d::rules::pierce_charges(60, 1) == 3);

    Missile plain;
    plain.info = &arrow_row;
    fight.seed_pierce(plain, true);                                     // the merc's own arrow, the same seed
    assert(plain.pierce == 3);

    Monster monster;
    monster.type = 0;
    monster.hit_points = 100;
    monster.stats.hit_points = 100;
    fight.monsters.push_back(monster);
    for (int left = 2; left >= 0; --left) assert(!fight.skill_missile_hits(arrow, 0, 7000) && arrow.pierce == left);
    assert(fight.skill_missile_hits(arrow, 0, 7000));

    hero.character.merc_items.clear();
    hero.psum[d2d::d2s::kSkillPierce] = 40;
    hero.psum[d2d::d2s::kItemPierce] = 20;
    fight.casting_merc = true;
    Missile bare = fight.launch(row, skill, 1, 0, 0, 1.f, 0.f, row.range, 7000);
    fight.pending.pop_back();
    fight.casting_merc = false;
    assert(bare.pierce == 0);                                           // chance 0: FUN_0059f940 sets nothing
    bare.pierce = 2;
    assert(fight.skill_missile_hits(bare, 0, 7000) && bare.pierce == 2);   // the player's stats don't arm a merc's missile

    std::puts("OK: a merc's Pierce charges are its gear's and its stat 0x148");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_merc_pierce: %s\n", error.what());
    return 1;
}
