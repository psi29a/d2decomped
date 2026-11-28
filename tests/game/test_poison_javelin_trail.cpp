// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Poison Javelin's trail (missile do 2, FUN_005ae400 -> FUN_005a9720): each subtile the javelin enters, a
// poisonjavcloud standing there for its Range (60 frames: poisonjav's SrvCalc1 is 0, so no SubLoop runs on),
// at the javelin's skill and level; a monster in one is poisoned. Needs the 1.14d game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAmazon, "Spears", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Spears";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    const auto& javelin = *data->skills.get(data->skills.by_name.at("Poison Javelin"));
    world.fight.skill_level = [&](int id) { return id == javelin.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    d2d::rules::Rng make_rng{ 3 };
    world.fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("zombie1")), actor.player.x + 3, actor.player.y + 3, make_rng, 0));
    world.fight.monsters.back().hit_points = world.fight.monsters.back().stats.hit_points = 100000;
    actor.cast_x = actor.player.x + 8; actor.cast_y = actor.player.y;
    world.fight.fire(javelin, now, world.fight.player_caster(javelin.id));
    const auto& cloud_row = data->missiles.at("poisonjavcloud");
    std::vector<std::pair<int, int>> spots;
    std::size_t seen = 0;
    for (int frame = 0; frame < 40; ++frame) {
        now += 40;
        world.tick({}, now, now - 40);
        for (const auto& missile : world.fight.missiles)
            if (missile.info == &cloud_row && std::ranges::none_of(spots, [&](const auto& spot) { return spot == std::pair(int(std::floor(missile.x * 5)), int(std::floor(missile.y * 5))); })) {
                spots.emplace_back(int(std::floor(missile.x * 5)), int(std::floor(missile.y * 5)));
                assert(missile.velocity_x == 0 && missile.velocity_y == 0 && missile.skill == javelin.id && missile.level == 1);
                assert(missile.dies - missile.born == std::uint32_t(cloud_row.range) * 40);
            }
        seen = std::max(seen, spots.size());
    }
    std::printf("Poison Javelin: %zu clouds along its flight\n", seen);
    assert(seen >= 5);
    std::puts("OK: a cloud on each subtile the javelin enters, standing its 60 frames (FUN_005ae400)");
    // The cloud alone: the first throw's missiles and the zombie's poison gone, the zombie far off while the javelin
    // flies, then put on a cloud's subtile once the javelin is spent: the poison is the cloud's (CodeRabbit on #815).
    auto& zombie = world.fight.monsters.front();
    world.fight.missiles.clear();
    zombie.poison_until = 0; zombie.poison_rate = 0;
    zombie.unit.x = actor.player.x; zombie.unit.y = actor.player.y + 8;
    world.fight.fire(javelin, now, world.fight.player_caster(javelin.id));
    const auto& javelin_row = data->missiles.at(javelin.srvmissile);
    const auto flying = [&] { return std::ranges::any_of(world.fight.missiles, [&](const Missile& missile) { return missile.info == &javelin_row; })
                                     || std::ranges::any_of(world.fight.pending, [&](const Missile& missile) { return missile.info == &javelin_row; }); };
    for (int frame = 0; frame < 60 && (frame == 0 || flying()); ++frame) { now += 40; world.tick({}, now, now - 40); }
    assert(!flying() && zombie.poison_until == 0);
    const auto cloud = std::ranges::find(world.fight.missiles, &cloud_row, &Missile::info);
    assert(cloud != world.fight.missiles.end());
    zombie.unit.x = cloud->x; zombie.unit.y = cloud->y;
    bool poisoned = false;
    for (int frame = 0; frame < 20 && !poisoned; ++frame) { now += 40; world.tick({}, now, now - 40); poisoned = now < zombie.poison_until; }
    assert(poisoned);
    std::puts("OK: a monster in its clouds is poisoned");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_poison_javelin_trail: %s\n", error.what());
    return 1;
}
