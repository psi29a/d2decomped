// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster whose life rolls 0 (FUN_00573cb0 keeps it; a rat's MonStats
// has no HP) lives and thinks in game.exe, whose dead test is the mode
// (FUN_005541b0). d2d holds it at one point, `lifeless`, and any landed
// hit kills it, even one of no damage: game.exe's life <= 0 after the hit
// (0x57c8e2). A rolled monster with life takes a hit of no damage. Needs
// the data; SKIPs without it.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    const auto rat = data->monsters.by_id.find("rat");
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(rat != data->monsters.by_id.end() && zombie != data->monsters.by_id.end());
    d2d::rules::Rng spawn{ 0x5eed };
    Monster critter = make_monster(*data, int(rat->second), 10.f, 10.f, spawn, 0);
    assert(critter.lifeless && critter.alive() && critter.hit_points == 1);
    assert(hurt(*data, critter, 0, 1000) && !critter.alive() && critter.mode == "DT");
    Monster walker = make_monster(*data, int(zombie->second), 10.f, 10.f, spawn, 0);
    assert(!walker.lifeless && walker.hit_points > 0);
    assert(!hurt(*data, walker, 0, 1000) && walker.alive());
    std::puts("OK: a zero-life roll lives at one point, and any landed hit kills it");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_zero_life: %s\n", error.what());
    return 1;
}
