// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Diablo's Bone Prison falls together (MonUMod 15 partydead, boneprison1..4 by
// FUN_005b21b0; hook 1 FUN_005a2d10): the later bones are minions of the first
// (FUN_005b31b0), so one of them dying takes the others and the first with it
// (FUN_0058f380 -> FUN_005a2cc0); the first has no owner and falls alone.
// Needs the game data.

#include <ai.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
#include <vector>

namespace filesystem = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const filesystem::path data_dir = mpq_dir ? filesystem::path(mpq_dir) : filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? filesystem::path(patch) : filesystem::path{}, 0x1234);
    assert(data);
    World world(&*data, -1, -1);
    auto& fight = world.fight;
    const auto prison = [&] {
        fight.monsters.clear();
        d2d::rules::Rng rng{ 3 };
        for (int k = 0; k < 4; ++k) {
            auto bone = make_monster(*data, 0x154 + k, 20.f + float(k), 20.f, rng, 0, true);
            bone.id = 100 + k;
            if (k > 0) bone.party_owner = 100;
            fight.monsters.push_back(std::move(bone));
        }
        for (auto& bone : fight.monsters) bone.fx_mode = bone.mode;
    };
    const auto dead = [&] { return int(std::ranges::count_if(fight.monsters, [](const Monster& bone) { return bone.mode == "DT"; })); };
    prison();
    fight.monsters[2].hit_points = 0;
    set_mode(*data, fight.monsters[2], "DT", 1000);
    fight.boss_events({}, 1000);
    assert(dead() == 4);                                  // the third bone takes the prison down
    prison();
    fight.monsters[0].hit_points = 0;
    set_mode(*data, fight.monsters[0], "DT", 1000);
    fight.boss_events({}, 1000);
    assert(dead() == 1);                                  // the first falls alone
    std::printf("bone prison: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_bone_prison_party: %s\n", error.what());
    return 1;
}
