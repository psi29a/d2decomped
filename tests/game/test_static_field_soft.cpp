// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Static Field's record (FUN_005c9800 -> FUN_005c96a0) carries result flags 0x4001 (0x5c97d2): no get-hit (4)
// and no knockback (8), and 0x4000 takes FUN_0057cee0's monster branch to event 4 alone. So a monster it takes
// a quarter of the life of stays out of GH, its event 4 raised. The REAL Fight::spot. Needs the game data.
#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
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
#include <string>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, 1, "Sparky", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Sparky";
    character.header = made.header;
    character.stats = made.stats;
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto& fight = world->fight;
    auto* actor = world->actor;
    fight.skill_level = [](int) { return 10; };
    const auto fallen = data->monsters.by_id.find("fallen1");
    fight.monsters.clear();
    fight.monsters.push_back(make_monster(*data, fallen->second, actor->player.x + 0.4f, actor->player.y, rng, 0, true, 1, open.id));
    auto& struck = fight.monsters.back();
    struck.stats.hit_points = struck.hit_points = 1000;
    const auto& skill = *data->skills.get(data->skills.by_name.at("Static Field"));
    fight.spot(skill, 1000 * kTickMs, fight.player_caster(skill.id));
    std::printf("Static Field: %d life left, mode %s, event 4 %d\n", struck.hit_points, std::string(struck.mode).c_str(), int(struck.struck_event));
    assert(struck.hit_points < 1000 && struck.mode != "GH" && struck.struck_event);
    std::puts("OK: Static Field's hit rolls no get-hit and raises event 4");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_static_field_soft: %s\n", error.what());
    return 1;
}
