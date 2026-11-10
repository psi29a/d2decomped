// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's get-hit rolls its own seed before MonStats2's GH mode is
// asked: FUN_0057cb00 rolls at 0x57cb95 / 0x57cbc0, then FUN_0046c140 at
// 0x57cbda. So a monster without the mode still rolls, and its seed moves
// as one with it does (issue #345). Needs the data; SKIPs without it.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
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
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    const int type = int(zombie->second);

    // 1600 life and blows of 120: between max / 16 and max / 8 for hit class 0,
    // so each one rolls 2, then 4 when the first roll lets it on.
    constexpr int kLife = 1600, kBlow = 120;
    int rolled = 0;
    for (const bool has_mode : { true, false }) {
        data->monsters.types[std::size_t(type)].can_get_hit = has_mode;
        for (std::uint32_t start = 1; start <= 64; ++start) {
            d2d::rules::Rng spawn{ 0x5eed };
            Monster target = make_monster(*data, type, 10.f, 10.f, spawn, 0);
            target.stats.hit_points = kLife;
            target.hit_points = kLife;
            target.max_life_frac = 0;
            target.life_frac = 0;
            target.seed = d2d::rules::Rng{ start };
            d2d::rules::Rng expected{ start };
            const bool big = d2d::rules::get_hit(std::int64_t(kLife) << 8, std::int64_t(kBlow) << 8, 0, false, expected);
            assert(!hurt(*data, target, kBlow, 1000));
            assert(target.seed.low == expected.low && target.seed.high == expected.high);     // rolled either way
            assert((target.mode == "GH") == (big && has_mode));
            rolled += target.seed.low != start ? 1 : 0;
        }
    }
    assert(rolled == 128);
    std::puts("OK: a monster's get-hit rolls its seed, with or without its GH mode");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_get_hit_seed: %s\n", error.what());
    return 1;
}
