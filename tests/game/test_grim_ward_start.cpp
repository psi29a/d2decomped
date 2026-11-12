// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Hit function 26, Grim Ward's start (FUN_005ab8d0), through the REAL Fight::burst: one sub-missile on the
// missile's spot (flags 0x8001), its Range sHitPar1 when that is above 0, else the skill's calc1 at least 5.
// Each case against game.exe's own, tools/emu/hit_grim_ward.py (Missiles.txt rows 249, 252 and 255).

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

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
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(*data, 0, "Headless", false, true, rng);
    character.character_class = 0;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    world->level = &open;
    world->fight.enter(&open);
    world->fight.skill_level = [](int) { return 1; };
    auto& fight = world->fight;
    const std::uint32_t now = 1000 * kTickMs;

    // game.exe's missile for each sHitPar1 and calc1 (tools/emu/hit_grim_ward.py).
    struct Made { int row, hit_par1, calc1, flags, range; };
    static constexpr Made kMade[] = {
#include "hit_grim_ward_cases.inc"
    };
    const int skill_id = data->skills.by_name.at("Grim Ward");
    auto& skill = const_cast<d2d::rules::Skill&>(*data->skills.get(skill_id));
    int rows = 0, checked = 0;
    for (const auto& [name, row] : data->missiles) {
        if (row.hit_func != 26) continue;
        ++rows;
        assert(data->missiles.count(row.hit_sub) == 1);
        for (const auto& want : kMade) {
            if (want.row != kMade[0].row) continue;   // the rows differ only in their sub-missile
            assert(want.flags == 0x8001);
            auto info = row;
            info.hit_par1 = want.hit_par1;
            skill.calc[0].code = { { d2d::rules::Calc::Const, want.calc1 } };
            Missile start;
            start.info = &info;
            start.skill = skill_id;
            start.level = 1;
            start.friendly = true;
            start.x = start.y = 20.1f;
            fight.pending.clear();
            fight.burst(start, now);
            assert(fight.pending.size() == 1);
            const auto& got = fight.pending.front();
            assert(got.info->name == row.hit_sub && got.x == start.x && got.y == start.y);
            assert(got.dies == now + std::uint32_t(want.range * 40));
            ++checked;
        }
    }
    assert(rows == 3 && checked == 3 * 60);
    std::printf("OK: Grim Ward's start lasts sHitPar1, else calc1 at least 5, as game.exe's (FUN_005ab8d0)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_grim_ward_start: %s\n", error.what());
    return 1;
}
