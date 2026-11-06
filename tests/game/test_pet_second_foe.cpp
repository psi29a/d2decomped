// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005ddc30 (0x5ddc30) for a pet (the Vines', the Sentries' foe2):
// FUN_005dd0b0 mode 6 (FUN_005dcbd0, 0x5dcbd0) over the near rooms'
// monsters takes the nearest of threat 2 or more under 0x31 over a nearer
// one of less threat (a pet is a monster: FUN_005dd510 takes the primary),
// where the mode 5 search would take the nearer one. Needs the game data.

#include <ai.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->rooms.empty()) { std::printf("SKIP: no Blood Moor\n"); return D2D_TEST_SKIPPED; }
    // Two monster types: one of threat under 2, one of 2 or more.
    int weak = -1, strong = -1;
    for (std::size_t row = 0; row < data->monsters.types.size() && (weak < 0 || strong < 0); ++row) {
        const auto& type_info = data->monsters.types[row];
        if (type_info.size != 1) continue;
        d2d::rules::Rng made{ 1 };
        if (!make_monster(*data, int(row), 10.f, 10.f, made, 0).targetable()) continue;
        if (weak < 0 && type_info.threat < 2 && type_info.threat >= 0) weak = int(row);
        if (strong < 0 && type_info.threat >= 2) strong = int(row);
    }
    assert(weak >= 0 && strong >= 0);
    World world(&*data, -1, -1);
    world.level = moor;
    auto& fight = world.fight;
    // An open spot: a room with no wall between the three spots.
    for (const auto& room : moor->rooms) {
        const float at_x = float(room.x) + float(room.width) / 2, at_y = float(room.y) + float(room.height) / 2;
        if (moor->blocked(at_x, at_y, 0x805) || moor->blocked(at_x + 1, at_y, 0x805) || moor->blocked(at_x + 3, at_y, 0x805)) continue;
        // Both lines clear (mask 4, as pet_second_foe tests them), else another room.
        const auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        const auto wall = [&](int sub_x, int sub_y) { return moor->blocked((float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5, 0x04); };
        const auto line_clear = [&](int row, float foe_x) {
            return !d2d::rules::sight_blocked(sub(foe_x), sub(at_y), data->monsters.types[std::size_t(row)].size, sub(at_x), sub(at_y), 1, wall);
        };
        if (!line_clear(weak, at_x + 1) || !line_clear(strong, at_x + 3)) continue;
        d2d::rules::Rng rng{ 3 };
        fight.monsters.clear();
        // The strong one first, the near weak one newest: a pick by order alone would take the weak one.
        Monster far_one = make_monster(*data, strong, at_x + 3, at_y, rng, 0), near_one = make_monster(*data, weak, at_x + 1, at_y, rng, 0);
        near_one.align = far_one.align = 0;
        fight.monsters.push_back(std::move(far_one));
        fight.monsters.push_back(std::move(near_one));
        int gap = 0;
        assert(fight.pet_second_foe(at_x, at_y, 1, gap) == 0 && gap > 0 && gap < 0x31);
        fight.monsters[0].hit_points = 0;   // no primary: the secondary
        assert(fight.pet_second_foe(at_x, at_y, 1, gap) == 1);
        std::printf("pet_second_foe: ok (gap %d)\n", gap);
        return 0;
    }
    std::printf("SKIP: no open room in the Blood Moor\n");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pet_second_foe: %s\n", error.what());
    return 1;
}
