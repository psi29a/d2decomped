// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The think driver's step 3 (FUN_005b13e0). First sight (FUN_005b1140): a
// champion with a player under 20 off stands 20 and sends its taunt (unit
// event 0x10), once. The leash: on a level with MonSpcWalk (the Jail's 1),
// an isMelee walker whose target is farther with no clear line (0x805,
// FUN_005dc640) takes the nearest other enemy in a clear line (FUN_005dd0b0
// mode 0xb). A Skeleton walks at its target when aip1 is 100. Needs the
// data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;

constexpr std::uint32_t kFrameMs = 40;          // a game frame

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto skeleton = data->monsters.by_id.find("skeleton1");
    assert(skeleton != data->monsters.by_id.end());
    auto& body = const_cast<d2d::rules::MonType&>(data->monsters.types[skeleton->second]);
    body.diff[0].aip[0] = 100;                                    // it always walks at its target
    assert(body.melee && data->level_mon[std::size_t(d2d::rules::level_ids::kJailLevel1)].spc_walk == 1);
    Level here;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    d2d::rules::Rng rng(7);
    constexpr std::uint32_t kNow = 1000 * kFrameMs;
    const auto made = [&](d2d::rules::Boss boss) {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(skeleton->second), 3.1f, 8.1f, spawn, 0) };   // subtile (15, 40)
        pack[0].aware = true;
        pack[0].boss = boss;
        return pack;
    };

    // First sight: a champion, the player 15 off.
    here.id = d2d::rules::level_ids::kBloodMoor;
    std::array<Foe, 1> player{};
    player[0].x = 6.1f; player[0].y = 8.1f;
    auto pack = made(d2d::rules::Boss::champion);
    monster_update(*data, here, pack[0], player, rng, kNow, 0.04f, crowd, shots, pack);
    assert(pack[0].first_sight && pack[0].taunt_sound && pack[0].mode == "NU" && pack[0].next_act == kNow + 20 * kFrameMs);
    pack[0].taunt_sound = false;
    monster_update(*data, here, pack[0], player, rng, kNow + 20 * kFrameMs, 0.04f, crowd, shots, pack);
    assert(!pack[0].taunt_sound && pack[0].mode == "WL");         // once: it walks at it now
    pack = made(d2d::rules::Boss::none);                          // a plain one walks at once
    monster_update(*data, here, pack[0], player, rng, kNow, 0.04f, crowd, shots, pack);
    assert(!pack[0].first_sight && pack[0].mode == "WL");

    // The leash: the player behind a wall (subtile column 22), a pet 20 below in a clear line.
    here.id = d2d::rules::level_ids::kJailLevel1;
    for (int at_y = 0; at_y < 80; ++at_y) here.walk[std::size_t(at_y) * 80 + 22] = 0x0001;
    std::array<Foe, 2> two{};
    two[0].x = 6.1f; two[0].y = 8.1f;                             // subtile (30, 40): 15 off
    two[1].x = 3.1f; two[1].y = 12.1f; two[1].pet = true; two[1].threat = 5;   // subtile (15, 60): 20 off
    pack = made(d2d::rules::Boss::none);
    monster_update(*data, here, pack[0], two, rng, kNow, 0.04f, crowd, shots, pack);
    assert(pack[0].chase == 1 && pack[0].mode == "WL");
    std::puts("OK: first sight stands 20 once with its taunt; the leash takes the enemy in a clear line");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_think_step3: %s\n", error.what());
    return 1;
}
