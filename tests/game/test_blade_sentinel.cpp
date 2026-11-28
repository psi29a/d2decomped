// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Blade Sentinel in a game (do 44, FUN_005d6020): the cast makes its bladecreeper on the caster's subtile,
// of the caster's level, its line from there to the point (FUN_0058ef40); its think (MonAI 102,
// FUN_005ea540) makes the blade once, which rides it (missile do 20, FUN_005a99e0), walks it to the point
// first (+0x18 1), and past its last frame (Skill1's calc4 on) it dies; the blade ends with it. Needs the
// 1.14d game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
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
    auto made = new_character(*data, d2d::d2s::kAssassin, "Blades", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAssassin;
    character.name = "Blades";
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
    const auto& sentinel = *data->skills.get(data->skills.by_name.at("Blade Sentinel"));
    world.fight.skill_level = [&](int id) { return id == sentinel.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    actor.pets.clear();
    const int from_x = int(std::floor(actor.player.x * 5)), from_y = int(std::floor(actor.player.y * 5));
    actor.cast_x = actor.player.x + 4; actor.cast_y = actor.player.y;
    world.fight.fire(sentinel, now, world.fight.player_caster(sentinel.id));
    assert(actor.pets.size() == 1);
    auto& pet = actor.pets.front();
    assert(data->monsters.types[std::size_t(pet.monster.type)].id == "bladecreeper");
    assert(int(std::floor(float(pet.monster.unit.x) * 5)) == from_x && int(std::floor(float(pet.monster.unit.y) * 5)) == from_y);
    assert(pet.monster.stats.level == int(actor.character.stats.get(d2d::d2s::kLevel)));
    assert(pet.lined && pet.line[0] == std::pair(from_x, from_y) && pet.line[1] == std::pair(int(std::floor(actor.cast_x * 5)), from_y));
    std::puts("OK: the cast makes its bladecreeper on the caster, of its level, its line to the point (FUN_005d6020)");
    const int id = pet.monster.id;
    const auto blade = [&]() -> const Missile* {
        const auto found = std::ranges::find(world.fight.missiles, id, &Missile::rider);
        return found == world.fight.missiles.end() ? nullptr : &*found;
    };
    float furthest = float(pet.monster.unit.x);
    bool rode = true, seen = false;
    int frames = 0;
    for (; frames < 200 && std::ranges::any_of(actor.pets, [&](const Fight::Pet& one) { return one.monster.id == id && one.monster.alive(); }); ++frames) {
        now += 40;
        world.tick({}, now, now - 40);
        const auto alive = std::ranges::find(actor.pets, id, [](const Fight::Pet& one) { return one.monster.id; });
        if (alive == actor.pets.end() || !alive->monster.alive()) break;
        furthest = std::max(furthest, float(alive->monster.unit.x));
        if (const auto* missile = blade()) {
            seen = true;
            const int lag_x = std::abs(int(std::floor(missile->x * 5)) - int(std::floor(float(alive->monster.unit.x) * 5)));
            rode = rode && lag_x <= 1;                       // its subtile as of its do, the pet's walk after
        }
    }
    std::printf("Blade Sentinel: lived %d frames, out to %.2f cells (the point %.2f)\n", frames, double(furthest), double(actor.cast_x));
    assert(seen && rode);
    assert(furthest > actor.player.x + 2);                    // it walked out toward the point
    const int last = d2d::rules::eval_calc(data->skills, sentinel.calc[3], world.fight.calc_env(), sentinel.id, 1);
    assert(frames >= last && frames <= last + 20);            // at its first think past calc4's frames (one after the walk it's on)
    for (int k = 0; k < 15; ++k) { now += 40; world.tick({}, now, now - 40); }
    assert(blade() == nullptr);
    std::puts("OK: its blade rides it, it walks its line, and past calc4's frames it dies, the blade with it (FUN_005ea540, do 20)");
    // A zombie on the line: the blade riding past strikes it (its row's NextHit, the player's damage).
    d2d::rules::Rng make_rng{ 3 };
    world.fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("zombie1")), actor.player.x + 2, actor.player.y, make_rng, 0));
    const int full = world.fight.monsters.back().hit_points;
    world.fight.fire(sentinel, now, world.fight.player_caster(sentinel.id));
    for (int k = 0; k < 60 && world.fight.monsters.front().hit_points == full; ++k) { now += 40; world.tick({}, now, now - 40); }
    assert(world.fight.monsters.front().hit_points < full);
    std::puts("OK: a monster on its line is struck by the blade");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_blade_sentinel: %s\n", error.what());
    return 1;
}
