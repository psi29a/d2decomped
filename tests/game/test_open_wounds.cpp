// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Open Wounds end to end, against game.exe's values (tools/emu/open_wounds.py game, FUN_005bfe60): a player's blow
// with stat 135 at 100 through the REAL player_blow on Fight::target_of (Monster::target's `special`), Fight::land,
// then Fight::monster_dots over the wound: the life a fallen loses as a plain monster, a champion, a unique, a
// superunique and a minion. game.exe hands FUN_0056e970 v (256ths a frame) for 200 frames: 200 v / 256 points.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: game.exe 1.14d's values: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Wounder", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Wounder";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    d2d::rules::Rng spawn_rng{ 5 };
    const Monster fallen_made = make_monster(*data, int(fallen->second), world.actor->player.x, world.actor->player.y, spawn_rng, 0);

    using d2d::rules::Boss;
    struct Case { Boss kind; int level, fine; };           // fine: game.exe's v for that level and data word
    constexpr Case kCases[] = { { Boss::none, 2, 49 },        { Boss::champion, 2, 24 }, { Boss::unique, 2, 24 },
                                { Boss::superunique, 2, 24 }, { Boss::minion, 2, 49 },   { Boss::none, 30, 436 },
                                { Boss::unique, 30, 218 },    { Boss::none, 61, 1426 },  { Boss::champion, 61, 713 } };
    d2d::rules::Fighter wounding = d2d::rules::simple_fighter(1, 1, 1000000);
    wounding.open_wounds = 100;
    const std::uint32_t now = 1000 * kTickMs;
    for (const auto& one : kCases) {
        Monster monster = fallen_made;
        monster.boss = one.kind;
        monster.stats.hit_points = monster.hit_points = 1000000;
        monster.life_frac = 0;
        monster.stats.regen = 0;
        world.fight.monsters.clear();
        world.fight.add_monster(std::move(monster));
        d2d::rules::Rng roll{ 3 };
        auto blow = d2d::rules::player_blow(wounding, world.fight.target_of(0), one.level, roll);
        for (int i = 0; i < 20 && !blow.hit; ++i) blow = d2d::rules::player_blow(wounding, world.fight.target_of(0), one.level, roll);   // 95 % to hit
        assert(blow.hit && blow.bleed);
        world.fight.land(0, blow, true, now, -1);
        const int before = world.fight.monsters[0].hit_points;
        for (std::uint32_t frame = 0; frame < 250; ++frame) world.fight.monster_dots(now + frame * kTickMs, float(kTickMs) / 1000.f);
        const int lost = before - world.fight.monsters[0].hit_points;
        std::printf("kind %d level %d: %d points (game.exe %d)\n", int(one.kind), one.level, lost, 200 * one.fine / 256);
        assert(lost == 200 * one.fine / 256);
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_open_wounds: %s\n", error.what());
    return 1;
}
