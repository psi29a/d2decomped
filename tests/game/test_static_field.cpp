// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--static-field SEED N`: Static Field's hit on one monster (life,
// resistance, pierce, floor, calc1, calc2), the cases of
// tools/emu/static_field.py (game.exe's FUN_005c96a0 -> FUN_0057c6c0):
// the life after (256ths), or `skip`, one line each, to diff.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 4 || std::string_view(argv[1]) != "--static-field") {
        std::printf("SKIP: an oracle mode, run `test_static_field --static-field SEED N` (see tools/emu/static_field.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());

    std::uint32_t state = std::uint32_t(std::atoi(argv[2]));
    if (state == 0) state = 1;
    const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    d2d::rules::Rng rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    const Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
    const auto run = [&](const std::string& tag, int max_hp, int life, int res, int pierce, int floor_pct, int pct, int low) {
        Monster monster = made;
        monster.stats.hit_points = max_hp;
        monster.hit_points = (life + 255) >> 8;
        monster.life_frac = monster.hit_points * 256 - life;
        std::printf("%s %d %d %d %d %d %d %d ", tag.c_str(), max_hp, life, res, pierce, floor_pct, pct, low);
        const int fine = d2d::rules::static_field_fine(life, max_hp << 8, pct, low, floor_pct, res);
        if (fine < 0) { std::printf("skip\n"); return; }
        const int got = d2d::rules::resisted_fine(fine, res < 100 ? std::max(res - pierce, -100) : res);
        hurt(*data, monster, got >> 8, 0, got & 255);
        std::printf("%d\n", monster.alive() ? monster.hit_points * 256 - monster.life_frac : 0);
    };
    int id = 0;
    for (const int floor_pct : { 0, 33, 50 })
        for (const int res : { -150, -100, -99, -50, -1, 0, 1, 33, 50, 99, 100, 101, 150 })
            for (int max_hp = 1; max_hp <= 6; ++max_hp)
                for (int life = 1; life <= max_hp << 8; ++life) run("B" + std::to_string(id++), max_hp, life, res, 0, floor_pct, 25, 0);
    for (int i = 0; i < std::atoi(argv[3]); ++i) {
        const std::uint32_t wide = next(), which = next();
        const int max_hp = 1 + int(wide % (which % 4 == 0 ? 3000u : 300u));
        const int life = 1 + int(next() % std::uint32_t(max_hp << 8));
        const std::uint32_t bucket = next() % 100;
        int res;
        if (bucket < 10) res = 100 + int(next() % 51);
        else if (bucket < 30) res = -int(next() % 201);
        else res = int(next() % 100);
        const int pierce = res >= 100 || next() % 4 ? 0 : int(next() % 61);
        const int any_floor = int(next() % 101);
        const int floors[4] = { 0, 33, 50, any_floor };
        const int floor_pct = floors[next() % 4];
        const int pct = next() % 4 ? 25 : int(next() % 151);
        const int low = next() % 4 ? 0 : int(next() % 0x2000);
        run("R" + std::to_string(i), max_hp, life, res, pierce, floor_pct, pct, low);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
