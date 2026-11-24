// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's missile at the monsters (Fight::world's monster foes, FUN_005a8730): an evil owner's
// CollideFriend row (catapultchargedballbolt) strikes another evil monster though none is aligned
// or cursed; a physical one at a good monster (rogue2) comes off by that monster's physical
// resistance (FUN_0057bf80's percent: the foe's dr_pct), so an immune one takes none.
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
    auto made = new_character(*data, d2d::d2s::kAmazon, "Sides", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Sides";
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
    // Stand them still.
    const auto hold = [&] { for (auto& monster : fight.monsters) monster.next_act = 0xffffffffu; };
    const auto run = [&](int frames) {
        for (int frame = 0; frame < frames; ++frame) {
            now += kTickMs;
            hold();
            world->tick({}, now, now - kTickMs);
        }
    };
    // Two evil zombies, no rogue: the first's CollideFriend bolt at the second.
    fight.monsters[0] = make_monster(*data, fight.monsters[1].type, fight.monsters[1].unit.x + 1.f, fight.monsters[1].unit.y, rng, 0, true, 1, moor.id);
    fight.monsters[0].id = 0x7ffff2;
    fight.monsters[0].hit_points = fight.monsters[0].stats.hit_points = 100000;
    hold();
    fight.missiles.clear();
    andariel_missile(*data, fight.monsters[1], "catapultchargedballbolt", 2.f, 0.f, now, fight.missiles, 1, nullptr);
    assert(!fight.missiles.empty() && fight.missiles.back().owner == fight.monsters[1].id && fight.missiles.back().owner_align == 0);
    fight.missiles.back().src.a2_min = fight.missiles.back().src.a2_max = 40;   // its row's lightning is under a point at level 1
    run(20);
    std::printf("the evil bolt at its friend: its life %d\n", fight.monsters[0].hit_points);
    std::fflush(stdout);
    assert(fight.monsters[0].hit_points < 100000);

    // A physical arrow from the zombie at a good rogue, 0 % then 100 % physical resistance.
    const auto* arrow = &data->missiles.at("arrow");
    auto rogue_hit = [&](int resist) {
        auto target = make_monster(*data, rogue_type, fight.monsters[1].unit.x - 2.f, fight.monsters[1].unit.y, rng, 0, true, 1, moor.id);
        target.id = 0x7ffff3;
        target.hit_points = target.stats.hit_points = 100000;
        target.stats.armor_class = -1000;
        target.boss_res[0] = resist;
        fight.monsters[0] = std::move(target);
        hold();
        fight.missiles.clear();
        Missile shot;
        shot.info = arrow;
        shot.owner = fight.monsters[1].id; shot.owner_align = 0;
        shot.src.a2_min = shot.src.a2_max = 40;
        shot.src.no_hit_roll = shot.src.unblockable = true;          // it lands: no roll, no block
        shot.x = fight.monsters[1].unit.x - 1.f; shot.y = fight.monsters[1].unit.y;
        shot.velocity_x = -5.f;
        shot.aim_at = std::pair{ fight.monsters[1].unit.x - 5.f, fight.monsters[1].unit.y };
        shot.born = now; shot.dies = now + 40 * 40;
        fight.missiles.push_back(std::move(shot));
        run(20);
        return 100000 - fight.monsters[0].hit_points;
    };
    const int plain = rogue_hit(0), immune = rogue_hit(100);
    std::printf("the physical arrow at the rogue: %d at 0 %%, %d at 100 %%\n", plain, immune);
    std::fflush(stdout);
    assert(plain > 0 && immune == 0);
    std::printf("test_monster_foe_hits: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_monster_foe_hits: %s\n", error.what());
    return 1;
}
