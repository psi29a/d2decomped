// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An enemy aura's cost (FUN_005cf3a0, FUN_00644b10): paid, with nomanaregen
// (state 0x55) on, only when the pulse put its target state (+0x82) on a
// unit (FUN_005cedc0's count); else 0x55 goes off. Sanctuary has no target
// state, so its one point is never paid; an aura with one takes it from a
// player who has the mana. Synthetic rows: no game data.
#include <ai.hpp>
#include <cues.hpp>
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
    fixture.monster(0, "undead").undead = true;
    auto& data = fixture.data;
    for (const int id : { 1, 2 }) {
        auto& aura = fixture.skill(id, id == 1 ? "sanctuary" : "taker");
        aura.srvdofunc = d2d::rules::ServerDoFunction::kEnemyAura;
        aura.aura = true;
        aura.etype = 4;                          // magic
        aura.emin = aura.emax = 1;
        aura.mana = 1;
        aura.manashift = 8;                      // one point a pulse (256ths)
        aura.aurarange.code = { { d2d::rules::Calc::Const, 10 } };
        aura.perdelay_calc.code = { { d2d::rules::Calc::Const, 50 } };
        if (id == 2) {                           // a target state with a stat
            aura.auratarget = "conviction";
            aura.aurastat[0] = d2d::d2s::kFireResist;
            aura.aura_calc[0].code = { { d2d::rules::Calc::Const, -50 } };
        }
    }
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
    fight.skill_level = [](int) { return 1; };
    hero.player.x = hero.player.y = 10;
    Monster undead;
    undead.type = 0;
    undead.hit_points = undead.stats.hit_points = 100000;
    undead.unit.x = undead.unit.y = 10.5f;
    auto& mana = hero.character.stats.values[d2d::d2s::kMana];

    // Sanctuary: the pulse lands, nothing paid, nomanaregen off.
    fight.monsters = { undead };
    mana = 100 << 8;
    hero.no_mana_regen = true;
    hero.aura = 1;
    hero.aura_next = 0;
    fight.aura_pulse(1000);
    assert(mana == 100 << 8 && !hero.no_mana_regen);

    // An aura with a target state put on the undead: one point paid, nomanaregen on.
    hero.aura = 2;
    hero.aura_next = 0;
    fight.aura_pulse(5000);
    assert(mana == 99 << 8 && hero.no_mana_regen);

    // Not enough mana: no target state's stats, nothing paid, 0x55 off.
    mana = 100;
    hero.aura_next = 0;
    fight.aura_pulse(9000);
    assert(mana == 100 && !hero.no_mana_regen);
    std::puts("OK: an enemy aura's cost is paid only for a target state put (FUN_005cf3a0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
