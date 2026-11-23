// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0056b7e0 / FUN_006492a0: pulse range uses integer subtiles.
// Synthetic rows keep the geometry regression runnable without MPQs.
#include <ai/units.hpp>
#include <cues.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.monster(0, "living");
    fixture.monster(1, "undead").undead = true;
    auto& data = fixture.data;
    for (int index = 0; index < 4; ++index) {
        auto& skill = fixture.skill(index, "pulse");
        skill.srvdofunc = index == 2 ? d2d::rules::ServerDoFunction::kHolyFreeze
                                   : d2d::rules::ServerDoFunction::kEnemyAura;
        skill.aura = true;
        skill.etype = index;
        if (index == 3) skill.etype = 4;
        skill.emin = skill.emax = 10;
        skill.hitshift = 8;
        skill.aura_filter = index == 3 ? 0xe786 : 0x4a283;
        skill.aurarange.code = {{d2d::rules::Calc::Const, 5}};
    }
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    constexpr int width = 150;
    arena.walk.resize(width * width);
    const Level* level = &arena;
    d2d::rules::Rng random{7};
    Cues cues;
    cues.game_data = &data;
    Loot loot{&data, level, random, cues};
    Fight fight{&data, level, random, loot, cues};
    Fight::Hero hero;
    fight.act_as(hero);
    Monster target;
    target.type = 0;
    target.hit_points = target.stats.hit_points = 100000;
    auto pulse = *data.skills.get(2);
    const auto cell = [](float offset) { return (50.f + offset) / 5.f; };
    const auto hits = [&](float caster_x, float caster_y, float target_x, float target_y, bool wall = false) {
        target.unit.x = cell(target_x);
        target.unit.y = cell(target_y);
        arena.walk[50 * width + 52] = wall ? 0x04 : 0;
        fight.monsters = {target};
        fight.enemy_pulse(pulse, 1, {}, cell(caster_x), cell(caster_y), {}, fight.player_foe_id(), 1000);
        return fight.monsters.front().hit_points < 100000;
    };
    assert(hits(0, 0, 4, 0));
    assert(hits(0, 0, 5, 0));
    assert(!hits(0, 0, 6, 0));
    assert(!hits(0, 0, 4, 0, true));
    for (int index = 0; index < 4; ++index) {
        pulse = *data.skills.get(index);
        target.type = index == 3 ? 1 : 0;
        assert(hits(0.1f, 0, 5.9f, 0));
        assert(!hits(0.9f, 0.9f, 4, 4));
    }
    target.type = 0;
    assert(!hits(0, 0, 4, 0));
    pulse = *data.skills.get(2);
    hits(0.1f, 0, 5.9f, 0);
    auto inside = fight.monsters.front();
    inside.hit_points = 100000;
    auto outside = inside;
    outside.unit.x = cell(6);
    fight.monsters = {std::move(inside), std::move(outside)};
    fight.enemy_pulse(pulse, 1, {}, cell(0.1f), cell(0), {}, fight.player_foe_id(), 1000);
    assert(fight.monsters[0].hit_points < 100000 && fight.monsters[1].hit_points == 100000);
    pulse.aurarange.code = {{d2d::rules::Calc::Const, 0}};
    assert(!hits(0, 0, 0, 0));
    const auto game_seed = random;
    const auto target_seed = fight.monsters.front().seed;
    const bool shatter = fight.monsters.front().shatter;
    fight.enemy_pulse(pulse, 1, {}, cell(0), cell(0), {}, fight.player_foe_id(), 1000);
    assert(random.low == game_seed.low && random.high == game_seed.high);
    assert(fight.monsters.front().seed.low == target_seed.low && fight.monsters.front().seed.high == target_seed.high);
    assert(fight.monsters.front().shatter == shatter);
    hero.merc = target;
    hero.merc->unit.x = cell(0.1f);
    hero.merc->unit.y = cell(0);
    hero.merc_aura = hero.merc_ids[0] = 2;
    hero.merc_levels[0] = 1;
    for (const auto [offset, expected] : std::array<std::pair<float, bool>, 2>{{{5.9f, true}, {6.f, false}}}) {
        target.unit.x = cell(offset);
        target.unit.y = cell(0);
        fight.monsters = {target};
        hero.merc_aura_next = 0;
        fight.aura_pulse(1000);
        assert((fight.monsters.front().hit_points < 100000) == expected);
    }
    std::puts("OK: synthetic aura pulse radius, filters, seeds and mercenary dispatch");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
