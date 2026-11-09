// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Cry Help (do 128, FUN_005d1800) from an Overseer at the player: on S1's
// action frame its srvoverlay (Skills.txt +0x4e: curseattract, Overlay.txt
// row 133) goes on the unit (FUN_00621e40), played once: Foe::flashed, which
// Fight keeps as the player's flash_overlay. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
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
    const int cry = data->skills.by_name.at("Cry Help");
    assert(data->skills.get(cry)->srvoverlay == "curseattract");
    const auto overseer = data->monsters.by_id.find("overseer1");
    assert(overseer != data->monsters.by_id.end());
    Level here;
    here.id = d2d::rules::level_ids::kBloodyFoothills;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 10.1f; player[0].y = 8.1f;
    d2d::rules::Rng rng(7);
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(*data, int(overseer->second), 8.1f, 8.1f, spawn, 0) };
    auto& caller = pack[0];
    caller.aware = true;
    caller.chase = 0;
    caller.skill = cry;
    constexpr std::uint32_t kStart = 40000;
    set_mode(*data, caller, "S1", kStart);
    caller.next_act = caller.mode_until + 40;                    // no think till the cast's done
    for (std::uint32_t now = kStart; now < caller.mode_until && player[0].flashed < 0; now += 40)
        monster_update(*data, here, caller, player, rng, now, 0.04f, crowd, shots, pack);
    assert(player[0].flashed == 133);
    std::puts("OK: Cry Help puts curseattract (row 133) on the player, once");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_cry_help_overlay: %s\n", error.what());
    return 1;
}
