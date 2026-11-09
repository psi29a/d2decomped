// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A search type 2 MonAI (the MonAI table 0x73ca18 + ai x 16; FUN_005b1650
// case 2: FUN_005dd7f0 alone) thinks with no target found, where type 1
// stands or wanders. Nihlathak (128, FUN_005ee5d0) with no one about: hit,
// he teleports (Skill1); else he stands 25. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
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
    const auto row = data->monsters.by_id.find("nihlathakboss");
    assert(row != data->monsters.by_id.end() && data->monsters.types[row->second].ai_name == "Nihlathak");
    const int teleport = data->skills.by_name.at(data->monsters.types[row->second].skill[0]);
    assert(teleport >= 0);
    Level here;
    here.id = d2d::rules::level_ids::kHallsOfVaught;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 1.1f; player[0].y = 1.1f;                       // far past any aidist: no target
    d2d::rules::Rng rng(7);
    const auto thinks = [&](bool hit) {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(row->second), 12.1f, 12.1f, spawn, 0) };
        pack[0].aware = true;
        if (hit) pack[0].left_mode = "GH";
        monster_update(*data, here, pack[0], player, rng, 40000, 0.04f, crowd, shots, pack);
        return pack[0];
    };
    const Monster hit = thinks(true);
    assert(hit.skill == teleport && hit.chase == -1);             // teleports, at no one
    const Monster calm = thinks(false);
    assert(calm.mode == "NU" && calm.next_act == 40000 + 25 * 40);   // stands 25
    std::puts("OK: Nihlathak thinks with no target: hit he teleports, else stands 25");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_think_untargeted: %s\n", error.what());
    return 1;
}
