// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player's burners (missile do 5: Fire Wall's flames), through the real Fight::missile_tick. Each flame
// is its own collide (FUN_005ae1f0 -> FUN_00641cb0, then FUN_005adf10): the first unit its footprint finds,
// that one alone; no list spans missiles (FUN_0064a470 is the missile's own last unit). So two flames on
// one monster burn it twice a frame, and one flame on two monsters burns one: the newest (a room's list head).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

using namespace d2d::game;

int main() try {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, 1, "Headless", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    auto& fight = world->fight;
    fight.enter(&open);
    fight.skill_level = [](int) { return 20; };
    fight.skill_base = fight.skill_level;
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    const int fire_wall = data->skills.by_name.at("Fire Wall");
    const auto& flame = data->missiles.at("firewall");
    const int zombie = data->monsters.row("zombie1");
    const float at_x = world->actor->player.x + 4, at_y = world->actor->player.y;
    const auto put = [&](int count) {
        fight.monsters.clear();
        d2d::rules::Rng spawn{ 0x5eed };
        for (int k = 0; k < count; ++k) {
            auto monster = make_monster(*data, zombie, at_x, at_y, spawn, 0);
            monster.id = 100 + k;
            monster.hit_points = monster.stats.hit_points = 100000;
            fight.monsters.push_back(std::move(monster));
        }
    };
    const auto flames = [&](int count) {
        fight.missiles.clear();
        for (int k = 0; k < count; ++k) {
            Missile missile{ &flame, at_x, at_y, 0, 0, 0, now, now + 100 * kTickMs, {} };
            missile.friendly = true; missile.caster = world->actor->id; missile.skill = fire_wall; missile.level = 20;
            fight.missiles.push_back(std::move(missile));
        }
        now += kTickMs;
        fight.missile_tick(now);
    };
    const auto lost = [&](std::size_t index) { return 100000 - fight.monsters[index].hit_points; };
    put(1);
    flames(1);
    const int one = lost(0);
    put(1);
    flames(2);
    const int two = lost(0);
    assert(one > 0 && two > one);                                   // the second flame burns too
    put(2);
    flames(1);
    assert(lost(1) > 0 && lost(0) == 0);                            // one flame, one unit: the newest
    std::printf("OK: one flame %d, two flames %d; a flame burns one unit\n", one, two);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
