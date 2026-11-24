// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's alignment through the frames: its own (MonStats Align 1: unit alignment 2, made so by
// make_monster) holds past Fight::monster_states, which puts a curse's on each frame. game.exe sets
// it only at events (Confuse's and Attract's end: 0, 0x5c3db0 / 0x5c3b00); the evil zombie's stays 0.
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
    assert(data && !data->town.walk.empty());

    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAmazon, "Aligned", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Aligned";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    // The Blood Moor as built (its rooms: the searches walk the near list), at a stretch of open ground.
    Level& moor = const_cast<Level&>(*data->level(d2d::rules::level_ids::kBloodMoor));
    float base_x = -1, base_y = -1;
    for (float y = 4.f; y < float(moor.ds1.height()) - 4.f && base_x < 0; y += 1.f)
        for (float x = 12.f; x < float(moor.ds1.width()) - 6.f && base_x < 0; x += 1.f) {
            bool clear = room_of(moor, x, y) >= 0;
            for (float dy = -1.f; clear && dy <= 1.f; dy += 0.2f)
                for (float dx = -10.f; clear && dx <= 3.f; dx += 0.2f) clear = !moor.blocked_here(x + dx, y + dy, 0x3c01);
            if (clear) { base_x = x; base_y = y; }
        }
    assert(base_x > 0);
    world->level = &moor;
    world->fight.enter(&moor);
    world->actor->player.x = base_x - 8.f; world->actor->player.y = base_y;
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    auto& fight = world->fight;
    const int rogue_type = int(data->mon_bin[270]);
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    fight.monsters.clear();
    auto rogue = make_monster(*data, rogue_type, base_x, base_y, rng, 0, true, 1, moor.id);
    assert(rogue.align == 2 && rogue.listed);
    rogue.id = 0x7ffff0;
    rogue.hit_points = rogue.stats.hit_points = 100000;
    fight.monsters.push_back(std::move(rogue));
    auto foe = make_monster(*data, int(zombie - data->monsters.types.begin()), base_x + 2.f, base_y, rng, 0, true, 1, moor.id);
    foe.id = 0x7ffff1;
    foe.hit_points = foe.stats.hit_points = 100000;
    fight.monsters.push_back(std::move(foe));
    for (int frame = 0; frame < 5; ++frame) {
        now += kTickMs;
        world->tick({}, now, now - kTickMs);
    }
    std::printf("after 5 frames: rogue2 %d, zombie %d\n", fight.monsters[0].align, fight.monsters[1].align);
    assert(fight.monsters[0].align == 2 && fight.monsters[1].align == 0);
    std::printf("test_monster_align: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_monster_align: %s\n", error.what());
    return 1;
}
