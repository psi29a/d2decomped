// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet is a monster unit: a turn its walk left going goes on a step a frame while it stands (the client's
// FUN_00463390 -> FUN_00648640 each frame: turn_frame), as a monster's does, not snapped or left. The REAL
// Fight::pets_turn through World::tick, a raised skeleton beside its owner. Needs the game data; SKIPs without it.
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Bones", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Bones";
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
    const auto& raise = *data->skills.get(data->skills.by_name.at("Raise Skeleton"));
    world.fight.skill_level = [&](int id) { return id == raise.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    world.fight.summon_one(raise, int(data->monsters.by_id.at(raise.summon)), 1, world.fight.calc_env(), actor.player.x + 1, actor.player.y, now, -1);
    assert(actor.pets.size() == 1);
    auto& unit = actor.pets.front().monster.unit;
    // Standing, its walk's turn left at 32 with the goal at 56: 8 a step (kDiffDirections of 24).
    unit.walking = false; unit.path.clear();
    unit.facing.now = 32; unit.facing.aim(56); unit.dir = missile_direction(32, 16);   // as a walk's aim leaves it
    const int step = d2d::rules::kDiffDirections[std::size_t((56 - 32) & 0x3f)];
    now += 40;
    world.tick({}, now, now - 40);
    std::printf("after a frame: now %d, goal %d, walking %d, dir %d\n", int(unit.facing.now), int(unit.facing.goal), int(unit.walking), unit.dir);
    std::fflush(stdout);
    assert(unit.facing.goal == 56 && unit.facing.now == 32 + step);  // a step on, not snapped to 56 or left at 32
    std::puts("OK: a standing pet's turn goes on a step a frame (turn_frame in Fight::pets_turn)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_turn: %s\n", error.what());
    return 1;
}
