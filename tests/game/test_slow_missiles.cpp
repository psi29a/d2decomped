// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Slow Missiles (state 0x57, slowmissiles): a CanSlow row (Missiles +4 bit 8) made by a monster under it flies at
// its velocity x the state's stat 161 (skill_handofathena, Slow Missiles' ln12: 33 at level 1) / 100, before the
// x 75 / 100 (FUN_0059fa30, 0x59fb3f..0x59fb6b); a row without CanSlow is as fast as ever. Needs the game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <missiles.hpp>
#include <rules.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    assert(d2d::rules::missile_velocity(25, 0, 1, 33) == ((25 << 8) * 33 / 100) * 75 / 100);
    assert(d2d::rules::missile_velocity(25, 0, 1) == (25 << 8) * 75 / 100);
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    assert(data->missiles.at("arrow").can_slow && !data->missiles.at("nova").can_slow);
    d2d::rules::Rng rng{ 3 };
    auto shooter = make_monster(*data, int(data->monsters.by_id.at("skeleton1")), 10.f, 10.f, rng, 0);
    const std::uint32_t now = 4000;
    const auto speed_of = [&](const char* row) {
        std::vector<Missile> missiles;
        andariel_missile(*data, shooter, row, 1.f, 0.f, now, missiles, 1, nullptr);
        assert(missiles.size() == 1);
        return std::hypot(missiles[0].velocity_x, missiles[0].velocity_y);
    };
    const float arrow = speed_of("arrow"), nova = speed_of("nova");
    const int slow = data->skills.by_name.at("Slow Missiles");
    shooter.states.push_back({ slow, 1, now + 4000 });
    const float slowed = speed_of("arrow"), nova_slowed = speed_of("nova");
    const auto& row = data->missiles.at("arrow");
    assert(slowed == missile_speed(d2d::rules::missile_velocity(row.vel, row.vel_lev, 1, 33)));
    assert(slowed < arrow && nova_slowed == nova);
    std::printf("arrow %.2f -> %.2f cells/s under Slow Missiles 1; nova (no CanSlow) %.2f\n", double(arrow), double(slowed), double(nova));
    std::puts("test_slow_missiles: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_slow_missiles: %s\n", error.what());
    return 1;
}
