// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A guided missile cast at nothing (do 10, FUN_005db6d0, flags 0x420): its
// range is the frames to reach the point (flag 0x400, FUN_0056ee90:
// FUN_006417f0's subtile distance << 16 over velocity << 4) and its do (7,
// FUN_005ae780) steers only at a unit, so Bone Spirit flies to the point,
// past a monster beside its line, and ends there. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
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
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
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
    auto* actor = world.actor;
    fight.skill_level = [](int) { return 1; };
    actor->character.stats.values[d2d::d2s::kMana] = 100 << 8;
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    Monster foe;
    foe.type = int(zombie - data->monsters.types.begin());
    foe.hit_points = foe.stats.hit_points = 100000;
    foe.unit.x = actor->player.x + 2.f; foe.unit.y = actor->player.y + 2.f;      // off the line east
    fight.monsters.clear();
    fight.monsters.push_back(std::move(foe));
    const int spirit = data->skills.by_name.at("Bone Spirit");
    const float to_x = actor->player.x + 4.f, to_y = actor->player.y;
    assert(fight.cast_missile(spirit, to_x, to_y, now));
    const Missile* flown = nullptr;
    for (int frame = 0; frame < 80; ++frame) {
        now += kTickMs;
        world.tick({}, now, now - kTickMs);
        for (const auto& missile : fight.missiles) if (missile.friendly && missile.skill == spirit) flown = &missile;
        if (flown) break;
    }
    assert(flown);
    const auto& row = *flown->info;
    const int velocity = d2d::rules::missile_velocity(row.vel, row.vel_lev, 1);
    const int frames = int((std::int64_t(d2d::rules::ai_distance(20, 0)) << 16) / (std::int64_t(velocity) << 4));
    std::printf("Bone Spirit at nothing: %u frames (want %d), heading %.2f, %.2f\n", (flown->dies - flown->born) / kTickMs, frames, flown->velocity_x, flown->velocity_y);
    assert(int((flown->dies - flown->born) / kTickMs) == frames && flown->target < 0 && std::abs(flown->velocity_y) < 1e-3f);
    const auto born = flown->born;
    for (int frame = 0; frame < frames - 2; ++frame) { now += kTickMs; world.tick({}, now, now - kTickMs); }
    for (const auto& missile : fight.missiles)
        if (missile.friendly && missile.skill == spirit && missile.born == born) assert(std::abs(missile.velocity_y) < 1e-3f);   // never turned to the zombie
    std::printf("test_guided_no_target: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_guided_no_target: %s\n", error.what());
    return 1;
}
