// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Dark Wanderer's spot (FUN_005bd0d0, 0x5bd0d0): 20 subtiles north of
// where he's made, else 11 north (2 east, 2 west), else 8, else 3, the first
// he has a clear line to (FUN_006229f0: from his subtile and size to a
// size-2 end, mask 0x3c01), found once and kept (a3q7's record +2). A wall
// 7 north cuts the lines to the 20 and 11 spots though both are open
// themselves (the 8 one's line stops 2 short of it, its end's size): he
// heads for the 8 one, and keeps it once past the wall. A walk again
// (state 2, FUN_005ea130) is a player's path (FUN_005de190(1): type 7, its
// search pather under 18 off): round a wall the first walk's toward and
// wall pathers don't clear in 5 steps.
// Needs the data; SKIPs without it.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
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
    const auto wanderer = data->monsters.by_id.find("darkwanderer");
    assert(wanderer != data->monsters.by_id.end());
    Level here;
    here.id = d2d::rules::level_ids::kSpiderForest;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    here.walk.assign(80 * 80, 0);
    for (int at_x = 30; at_x <= 50; ++at_x) here.walk[53 * 80 + std::size_t(at_x)] = 0x0001;   // 7 north of him
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 6.1f; player[0].y = 12.1f;                      // 10 subtiles west: under 20, he walks
    d2d::rules::Rng rng(7);
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(*data, int(wanderer->second), 8.1f, 12.1f, spawn, 0) };   // subtile (40, 60)
    auto& him = pack[0];
    him.aware = true;
    monster_update(*data, here, him, player, rng, 40000, 0.04f, crowd, shots, pack);
    assert(him.wanderer_spot == std::pair(40, 52));               // the 8 one: the 20 and 11 lines cross the wall
    assert(std::floor(him.unit.goal_y * 5) == 52.f);
    him.unit.y = 9.1f;                                            // past the wall: the 20 line is clear from here
    him.ai_state = 1;
    him.next_act = 0;
    monster_update(*data, here, him, player, rng, 80000, 0.04f, crowd, shots, pack);
    assert(him.wanderer_spot == std::pair(40, 52) && std::floor(him.unit.goal_y * 5) == 52.f);   // kept

    // Back south of the wall, 8 off his spot: the first walk can't reach it,
    // a walk again goes round.
    for (int at_x = 30; at_x <= 50; ++at_x) here.walk[53 * 80 + std::size_t(at_x)] = 0;
    for (int at_x = 36; at_x <= 44; ++at_x) here.walk[56 * 80 + std::size_t(at_x)] = 0x0001;
    him.unit.x = 8.1f; him.unit.y = 12.1f;
    him.mode = "NU"; him.ai_state = 1; him.next_act = 0;
    monster_update(*data, here, him, player, rng, 120000, 0.04f, crowd, shots, pack);
    assert(him.path_type == 0 && !him.steps.empty() && him.steps.back() != std::pair(40, 52));
    him.mode = "NU"; him.ai_state = 2; him.ai_state2 = 0; him.next_act = 0;
    monster_update(*data, here, him, player, rng, 160000, 0.04f, crowd, shots, pack);
    assert(him.path_type == 7 && him.next_path_type == 0 && him.ai_state2 == 1 && !him.steps.empty() && him.steps.back() == std::pair(40, 52));
    std::puts("OK: the Dark Wanderer's spot takes a clear line and is kept; he walks again by a player's path");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_dark_wanderer_spot: %s\n", error.what());
    return 1;
}
