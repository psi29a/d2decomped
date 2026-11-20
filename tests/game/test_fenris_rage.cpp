// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fenris Rage (srvdofunc 137, FUN_005d26f0): a corpse it may raise
// (FUN_00645590: DD, not used) is used up, and the skill's aurastate goes on
// Fenris (FUN_00570590) for auralen frames with its aurastats: Fight::pet_cast
// puts the buff, pet_strike adds its damagepercent. A live target, or a corpse
// already used, does nothing. Synthetic rows: no game data.
#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.monster(0, "corpse");
    auto& rage = fixture.skill(314, "fenris rage");
    rage.srvdofunc = d2d::rules::ServerDoFunction::kFenrisRage;
    rage.aurastate = "fenris_rage";
    rage.auralen.code = { { d2d::rules::Calc::Const, 500 } };
    rage.aurastat[0] = d2d::d2s::kDamagePercent;
    rage.aura_calc[0].code = { { d2d::rules::Calc::Const, 100 } };
    auto& data = fixture.data;
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);

    Monster corpse;
    corpse.type = 0;
    corpse.hit_points = 0;
    corpse.mode = "DD";
    corpse.unit.x = corpse.unit.y = 10;
    Fight::Pet fenris;
    fenris.monster.type = 0;
    fenris.monster.hit_points = fenris.monster.stats.hit_points = 100;
    fenris.monster.unit.x = fenris.monster.unit.y = 10;
    fenris.cast = 314;
    fenris.target = 0;
    const Crowd crowd;

    fight.monsters = { corpse };
    fight.pet_cast(fenris, 1000, crowd);
    assert(fight.monsters[0].corpse_used);
    assert(fenris.buffs.size() == 1 && fenris.buffs[0].skill == 314 && fenris.buffs[0].until == 1000 + 500 * 40);
    assert(Fight::pet_buff_stat(fenris, d2d::d2s::kDamagePercent) == 100);

    // The corpse is used now: a second cast puts nothing more.
    fenris.buffs.clear();
    fenris.buff_stats.clear();
    fight.pet_cast(fenris, 2000, crowd);
    assert(fenris.buffs.empty());

    // A live target isn't a corpse.
    corpse.hit_points = 50;
    corpse.mode = "NU";
    fight.monsters = { corpse };
    fight.pet_cast(fenris, 3000, crowd);
    assert(fenris.buffs.empty() && !fight.monsters[0].corpse_used);
    std::puts("OK: Fenris Rage uses up a corpse and puts fenris_rage with its damagepercent");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
