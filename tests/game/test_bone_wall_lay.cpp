// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Bone Wall through the REAL Fight on an open level: one piece on the point (FUN_005c58b0), then its two makers (missile do 13,
// FUN_005aeda0) lay a piece on each new subtile they enter, calc2 / 2 each (Skills.txt: 8 / 2 = 4) while they fly, across the
// caster's line, frame by frame, not all at once.

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
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <set>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kNecromancer = 2;
    auto made = new_character(*data, kNecromancer, "Headless", false, true, rng);
    Character character;
    character.character_class = kNecromancer;
    character.name = "Headless";
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
    int at_x = -1, at_y = -1;
    for (int y = 20; y < 400 && at_x < 0; ++y)
        for (int x = 20; x < 400 && at_x < 0; ++x)
            if (std::ranges::all_of(std::array<std::pair<int, int>, 5>{ { { 0, 0 }, { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 } } },
                                    [&](auto off) { return !missile_rooms(*data, open, float(x + off.first) + 0.5f, float(y + off.second) + 0.5f).empty(); }))
                at_x = x * 5 + 2, at_y = y * 5 + 2;
    assert(at_x >= 0);
    world.actor->player.x = (float(at_x) + 0.5f) / 5;
    world.actor->player.y = (float(at_y) + 0.5f) / 5;
    const auto& skill = *data->skills.get(data->skills.by_name.at("Bone Wall"));
    const Crowd crowd;
    std::uint32_t now = 1000 * kTickMs;
    const float point_x = (float(at_x + 10) + 0.5f) / 5, point_y = (float(at_y) + 0.5f) / 5;   // two cells east: the wall runs north-south
    world.fight.summon(skill, 1, point_x, point_y, now);
    const auto pieces = [&] { return std::ranges::count_if(world.actor->pets, [&](const auto& pet) { return pet.skill == skill.id; }); };
    assert(pieces() == 1 && world.fight.pending.size() == 2);
    assert(world.actor->pets.back().until == now + 610 * kTickMs);   // FUN_005e0390 / FUN_005e0400: the first think past frame 600
    std::size_t after_first_frame = 0;
    for (int frame = 0; frame < 40; ++frame) {
        now += kTickMs;
        world.fight.world(true, now, 0.04f, crowd);
        if (frame == 0) after_first_frame = std::size_t(pieces());
    }
    std::set<std::pair<int, int>> spots;
    for (const auto& pet : world.actor->pets)
        if (pet.skill == skill.id) {
            const int sub_x = int(std::floor(pet.monster.unit.x * 5)), sub_y = int(std::floor(pet.monster.unit.y * 5));
            assert(sub_x == at_x + 10);   // on the line across the caster's
            spots.emplace(sub_x, sub_y);
        }
    std::printf("Bone Wall: %zu pieces after the first frame, %zu in all\n", after_first_frame, spots.size());
    assert(spots.size() == std::size_t(pieces()) && pieces() == 1 + 2 * 4 && after_first_frame < 9);
    std::puts("OK: Bone Wall's makers lay a piece a new subtile (FUN_005aeda0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_bone_wall_lay: %s\n", error.what());
    return 1;
}
