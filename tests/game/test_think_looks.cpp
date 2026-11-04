// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A think's looks (monster-ai.md, SandRaider FUN_005f0700): with the
// game's MPQs (D2_MPQ_DIR; 1.14d with D2_PATCH_INSTALLER), a sand raider
// (aip5 18, aip6 0) at its 18th think plays Overlay.txt row 0x2e once on
// itself (FUN_00621e40), past it puts state 0x5b (red) on; its count over
// (state 0) takes it off. Overlay rows are kept by row (GameData::overlay_rows).

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <algorithm>
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
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    assert(data->overlay_rows.size() > 0x96 && data->overlay_rows[0x2e] && data->overlay_rows[0x96]);

    const auto raider = data->monsters.by_id.find("sandraider1");
    assert(raider != data->monsters.by_id.end());
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    const Crowd crowd;
    d2d::rules::Rng rng(11);
    std::vector<Monster> pack{ make_monster(*data, int(raider->second), 20.f, 20.f, rng, 0) };
    auto& monster = pack[0];
    monster.aware = true;
    std::array<Foe, 1> foes{};
    foes[0].x = 23.f; foes[0].y = 20.f;
    std::vector<Missile> missiles;
    bool flashed = false, red = false;
    std::uint32_t now = 40 * 1000;
    for (int tick = 0; tick < 3000 && !(flashed && red); ++tick, now += 40) {
        monster.hit_points = monster.stats.hit_points;           // never hurt: no walk to its kin
        monster_update(*data, open, monster, foes, rng, now, 0.04f, crowd, missiles, pack);
        if (monster.flash_overlay == 0x2e && monster.flash_at == now) { flashed = true; assert(monster.ai_state == 18 && !(monster.looks & 2)); }
        if (monster.looks & 2) { red = true; assert(flashed && (monster.looks & 1) == 0); }
    }
    assert(flashed && red);
    std::printf("OK: a sand raider's 18th think flashes overlay 0x2e, then state 0x5b (red) is on\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
