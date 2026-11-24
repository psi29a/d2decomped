// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The wanderer in play: rogue2 (MonStats Align 1, so unit alignment 2, in list 8) thinks
// GoodNpcRanged (FUN_005e7ac0): its second target (FUN_005ddc30) an evil monster under 20 off, it
// shoots at it (A1, MissA1 rogue1); the evil monster's search takes it from list 8 and fights it.
// Its alignment holds past the states' pass (Fight::monster_states). Its arrows strike the monsters
// hostile to it and pass the players' side by (FUN_005a8730, the monsters as the missiles' foes).
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
    auto made = new_character(*data, d2d::d2s::kAmazon, "Wanderer", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Wanderer";
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
    auto* actor = world->actor;
    const int rogue_type = int(data->mon_bin[270]);
    assert(data->monsters.types[std::size_t(rogue_type)].ai_name == "GoodNpcRanged");
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    fight.monsters.clear();
    auto rogue = make_monster(*data, rogue_type, base_x, base_y, rng, 0, true, 1, moor.id);
    assert(rogue.align == 2 && rogue.listed);
    rogue.id = 0x7ffff0;
    rogue.hit_points = rogue.stats.hit_points = 100000;
    rogue.stats.armor_class = -1000;                                // the zombie's swings land
    fight.monsters.push_back(std::move(rogue));
    auto foe = make_monster(*data, int(zombie - data->monsters.types.begin()), base_x + 2.f, base_y, rng, 0, true, 1, moor.id);
    foe.id = 0x7ffff1;
    foe.hit_points = foe.stats.hit_points = 100000;
    fight.monsters.push_back(std::move(foe));
    bool shot = false;
    for (int frame = 0; frame < 600 && !(fight.monsters[1].hit_points < 100000 && fight.monsters[0].hit_points < 100000); ++frame) {
        now += kTickMs;
        actor->player.x = base_x - 8.f; actor->player.y = base_y;   // the player stands off
        world->tick({}, now, now - kTickMs);
        shot |= fight.monsters[0].mode == "A1";
    }
    std::printf("the rogue %s (the zombie's life %d); the zombie %s it (its life %d)\n", shot ? "shot" : "didn't shoot", fight.monsters[1].hit_points,
                fight.monsters[0].hit_points < 100000 ? "hit" : "didn't hit", fight.monsters[0].hit_points);
    std::fflush(stdout);
    assert(shot && fight.monsters[0].hit_points < 100000 && fight.monsters[1].hit_points < 100000);

    // Its arrows pass the player by (FUN_005a8730: only a unit hostile to the owner): the player
    // stands between it and a harmless zombie, the rogue shooting through.
    fight.monsters[0].unit.x = base_x - 1.6f; fight.monsters[0].unit.y = base_y;
    fight.monsters[0].next_act = 0;
    auto& harmless = fight.monsters[1];
    harmless.unit.x = base_x + 1.6f; harmless.unit.y = base_y;
    harmless.next_act = 0xffffffffu;                                 // it never thinks: it doesn't swing at the player
    auto& life = actor->character.stats.values[d2d::d2s::kLife];
    const auto full = life;
    int shots = 0;
    bool was_shooting = false;
    for (int frame = 0; frame < 300; ++frame) {
        now += kTickMs;
        actor->player.x = base_x; actor->player.y = base_y;
        world->tick({}, now, now - kTickMs);
        fight.monsters[1].next_act = 0xffffffffu;
        const bool shooting = fight.monsters[0].mode == "A1";
        shots += shooting && !was_shooting;
        was_shooting = shooting;
    }
    std::printf("through the player: %d shots, its life %d of %d\n", shots, int(life >> 8), int(full >> 8));
    std::fflush(stdout);
    assert(shots > 0 && life == full);

    // The player's own arrow passes it by too (FUN_005a8730 for the player's side): sent through
    // the rogue, it flies on to the zombie behind it (land() has always refused it the rogue's life,
    // but the arrow was spent there).
    const auto* arrow = &data->missiles.at("arrow");
    auto send = [&](const Monster& toward) {
        Missile sent;
        sent.info = arrow;
        sent.friendly = true;
        sent.caster = actor->id;
        sent.x = toward.unit.x - 1.f; sent.y = toward.unit.y;
        sent.velocity_x = 5.f;
        sent.aim_at = std::pair{ toward.unit.x + 3.f, toward.unit.y };
        sent.born = now; sent.dies = now + 40 * 40;
        sent.min = sent.max = 5; sent.attack_rating = 100000;
        fight.missiles.push_back(std::move(sent));
    };
    fight.monsters[0].next_act = fight.monsters[1].next_act = 0xffffffffu;   // both stand
    fight.monsters[1].unit.x = fight.monsters[0].unit.x + 1.f; fight.monsters[1].unit.y = fight.monsters[0].unit.y;
    fight.monsters[1].stats.armor_class = -1000;
    const int rogue_life = fight.monsters[0].hit_points, zombie_life = fight.monsters[1].hit_points;
    fight.missiles.clear();                                          // the rogue's arrows still in flight
    send(fight.monsters[0]);
    for (int frame = 0; frame < 20; ++frame) {
        now += kTickMs;
        actor->player.x = base_x - 12.f; actor->player.y = base_y;
        fight.monsters[0].next_act = fight.monsters[1].next_act = 0xffffffffu;
        world->tick({}, now, now - kTickMs);
    }
    std::printf("the player's arrows: the rogue's life %d -> %d, the zombie's %d -> %d\n", rogue_life, fight.monsters[0].hit_points, zombie_life, fight.monsters[1].hit_points);
    std::fflush(stdout);
    assert(fight.monsters[0].hit_points == rogue_life && fight.monsters[1].hit_points < zombie_life);
    std::printf("test_wanderer_fight: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_wanderer_fight: %s\n", error.what());
    return 1;
}
