// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Vine Attack's vines (missile do 26, FUN_005af980): every Param1 frames (9)
// SubMissile1 at its subtile (FUN_0056ede0), the plague vines trail, which
// carries its row's Skill (Plague Poppy) and poisons a monster standing in
// it. Needs the game data.

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
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: 1.14d's rows: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kDruid, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kDruid;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    world.level = &open;
    world.fight.enter(&open);
    std::uint32_t now = 1000 * kTickMs;
    world.tick({}, now, now - kTickMs);
    auto& fight = world.fight;
    fight.skill_level = [](int) { return 5; };
    fight.monsters.clear();
    const auto* attack = data->skills.get(data->skills.by_name.at("Vine Attack"));
    assert(attack && attack->srvdofunc == d2d::rules::ServerDoFunction::kVineAttack && attack->srvmissilea == "plague vines");
    const auto& vines = data->missiles.at("plague vines");
    assert(vines.srv_do == 26 && vines.param1 == 9 && vines.sub == "plague vines trail");
    const float start_x = world.actor->player.x + 3.f, start_y = world.actor->player.y;
    fight.launch(vines, *attack, 5, start_x, start_y, 1.f, 0.f, vines.range, now);
    const int poppy = data->skills.by_name.at("Plague Poppy");
    int trails = 0;
    float trail_x = 0, trail_y = 0;
    for (int frame = 0; frame < 19; ++frame) {
        now += kTickMs;
        world.tick({}, now, now - kTickMs);
        trails = 0;
        for (const auto& missile : fight.missiles)
            if (missile.info && missile.info == &data->missiles.at("plague vines trail")) { ++trails; assert(missile.skill == poppy); trail_x = missile.x; trail_y = missile.y; }
    }
    std::printf("plague vines: %d trail pieces in 19 frames\n", trails);
    assert(trails >= 2 && trails <= 3);                              // at ages 0, 9, 18
    // A zombie on the last piece is poisoned.
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    Monster foe;
    foe.type = int(zombie - data->monsters.types.begin());
    foe.hit_points = foe.stats.hit_points = 100000;
    foe.unit.x = trail_x; foe.unit.y = trail_y;
    fight.monsters.push_back(std::move(foe));
    for (int frame = 0; frame < 4; ++frame) { now += kTickMs; world.tick({}, now, now - kTickMs); }
    assert(fight.monsters[0].poison_until > now);
    std::printf("test_vine_trail: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_vine_trail: %s\n", error.what());
    return 1;
}
