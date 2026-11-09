// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A fetish's AI command 0xe (FUN_0058f730 from its shaman raising a corpse,
// monster-ai.md "AI commands"): the Fetish think (FUN_005f53e0) takes it
// with its unit found by id (FUN_00552f60: the corpse, dead or raised), a
// walk at that unit (FUN_005dec80(unit, 0)) at pace 50; the command is
// popped either way. The unit gone: popped, no walk at it. Needs the data;
// SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto fetish = data->monsters.by_id.find("fetish1");
    assert(fetish != data->monsters.by_id.end() && data->monsters.types[fetish->second].ai_name == "Fetish");
    Level here;
    here.id = d2d::rules::level_ids::kSpiderForest;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 4.1f; player[0].y = 8.1f;                       // 20 subtiles west: a target, not in melee
    d2d::rules::Rng rng(7);
    const auto sent = [&](bool about) {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(fetish->second), 8.1f, 8.1f, spawn, 0), make_monster(*data, int(fetish->second), 8.1f, 12.1f, spawn, 0) };
        pack[1].id = 77;
        pack[1].hit_points = 0; pack[1].mode = "DD";                // the corpse, not raised yet
        pack[1].gone = !about;
        pack[0].aware = true;
        pack[0].ai_command = 14; pack[0].command_corpse = 77;
        monster_update(*data, here, pack[0], player, rng, 40000, 0.04f, crowd, shots, pack);
        return pack[0];
    };
    const Monster walking = sent(true);                           // still a corpse: walked at all the same
    assert(walking.ai_command == 0 && walking.mode == "WL");
    assert(std::floor(walking.unit.goal_y * 5) == 60.f && std::floor(walking.unit.goal_x * 5) == 40.f);   // at the corpse, not the player
    const Monster dropped = sent(false);
    assert(dropped.ai_command == 0 && std::floor(dropped.unit.goal_y * 5) != 60.f);
    std::puts("OK: a fetish's corpse command walks it at that unit; one gone is dropped");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_fetish_corpse_command: %s\n", error.what());
    return 1;
}
