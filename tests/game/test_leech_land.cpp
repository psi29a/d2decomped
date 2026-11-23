// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fight::land credits the player's leech (Blow::life / Blow::mana, in 256ths) only when it is
// positive: FUN_0057a980 and FUN_0057aa00 write nothing for a negative amount (a wrapped mul_div can
// leave one), and the stats stay under their maxima. No game data: typed in.

#include <ai/units.hpp>
#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;
using namespace d2d::d2s;

int main() try {
    GameData game_data;
    game_data.monsters.types.resize(1);                              // hurt() reads the struck monster's row
    const Level* level = nullptr;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    cues.game_data = &game_data;                                   // land cues the leech sound by name
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    auto& stats = hero.character.stats.values;
    constexpr std::int64_t kPoint = 256;                           // life and mana are kept in 256ths
    stats[kMaxLife] = 100 * kPoint; stats[kMaxMana] = 100 * kPoint;

    Monster monster;
    monster.type = 0;
    monster.hit_points = 1000;
    fight.add_monster(std::move(monster));

    const auto land = [&](int life, int mana, std::int64_t life_now, std::int64_t mana_now) {
        stats[kLife] = life_now * kPoint; stats[kMana] = mana_now * kPoint;
        fight.monsters[0].hit_points = 1000;
        d2d::rules::Blow blow;
        blow.hit = true;
        blow.life = life; blow.mana = mana;
        assert(fight.land(0, blow, true, 1000, -1));
    };
    land(-300, -300, 50, 50);
    assert(stats[kLife] == 50 * kPoint && stats[kMana] == 50 * kPoint);              // a negative leech takes nothing away
    land(0, 0, 50, 50);
    assert(stats[kLife] == 50 * kPoint && stats[kMana] == 50 * kPoint);              // nothing leeched: nothing written
    land(768, -768, 50, 50);
    assert(stats[kLife] == 53 * kPoint && stats[kMana] == 50 * kPoint);              // each stat on its own
    land(-768, 1024, 50, 50);
    assert(stats[kLife] == 50 * kPoint && stats[kMana] == 54 * kPoint);
    land(1 << 20, 1 << 20, 90, 95);
    assert(stats[kLife] == 100 * kPoint && stats[kMana] == 100 * kPoint);            // capped at the maxima
    std::printf("ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
