// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Hit function 20, Lightning Fury (FUN_005ab370), through the REAL Fight::burst: an area walk
// (FUN_0056b7e0) of sHitPar1 subtiles (else aurarange, at least 1) that sends up to sHitPar2 (else calc1, at
// least 1) sub-missiles (FUN_005ab2a0), one to each unit its filter takes: the skill's aurafilter, else
// 0xa783. The walk skips only the owner, so the unit the bolt struck gets one too. Each case against
// game.exe's own, tools/emu/hit_fury.py (Missiles.txt row 206).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
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

    // game.exe's walk (tools/emu/hit_fury.py): the missile on subtile (100, 200), in the room the unit it
    // struck (101, 200), another monster (100, 203) and one out of reach (110, 200).
    struct Made { int row, hit_par1, hit_par2; std::uint32_t aura_filter; int aurarange, calc1; std::uint32_t filter; int made; std::array<int, 6> spots; };
    static constexpr Made kMade[] = {
#include "hit_fury_cases.inc"
    };
    const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const int skill_id = data->skills.by_name.at("Lightning Fury");
    assert(skill_id == 35);
    auto& skill = const_cast<d2d::rules::Skill&>(*data->skills.get(skill_id));
    const int fallen = data->monsters.row("fallen1");
    assert(fallen >= 0);
    d2d::rules::Rng monster_rng{ 3 };
    fight.monsters.clear();
    for (const auto& [subtile_x, subtile_y] : { std::array{ 101, 200 }, std::array{ 100, 203 }, std::array{ 110, 200 } }) {
        Monster monster = make_monster(*data, fallen, centre(subtile_x), centre(subtile_y), monster_rng, 0);
        assert(monster.targetable());
        fight.monsters.push_back(std::move(monster));
    }
    const auto* fury_row = [&]() -> const GameData::MissileInfo* {
        for (const auto& [name, row] : data->missiles)
            if (row.hit_func == 20) return &row;
        return nullptr;
    }();
    assert(fury_row && data->missiles.count(fury_row->hit_sub) == 1);
    const auto burst = [&](GameData::MissileInfo& info) {
        Missile bolt;
        bolt.info = &info;
        bolt.skill = skill_id;
        bolt.level = 1;
        bolt.friendly = true;
        bolt.x = centre(100);
        bolt.y = centre(200);
        bolt.target = 0;                                   // it struck the first monster
        fight.pending.clear();
        fight.burst(bolt, now);
        return bolt;
    };
    int checked = 0;
    for (const auto& want : kMade) {
        auto info = *fury_row;
        info.hit_par1 = want.hit_par1;
        info.hit_par2 = want.hit_par2;
        skill.aura_filter = want.aura_filter;
        skill.aurarange.code = { { d2d::rules::Calc::Const, want.aurarange } };
        skill.calc[0].code = { { d2d::rules::Calc::Const, want.calc1 } };
        assert(want.filter == (want.aura_filter != 0 ? want.aura_filter : 0xa783));
        const auto bolt = burst(info);
        assert(int(fight.pending.size()) == want.made);
        for (std::size_t k = 0; k < fight.pending.size(); ++k) {
            const auto& got = fight.pending[k];
            assert(got.info->name == info.hit_sub);
            const float aim_x = float(want.spots[2 * k] - 100), aim_y = float(want.spots[2 * k + 1] - 200), span = std::hypot(aim_x, aim_y);
            const float speed = std::hypot(got.velocity_x, got.velocity_y);
            assert(speed > 0 && std::abs(got.velocity_x / speed - aim_x / span) < 1e-4f && std::abs(got.velocity_y / speed - aim_y / span) < 1e-4f);
            assert(got.x == bolt.x && got.y == bolt.y);
        }
        ++checked;
    }
    assert(checked == 64);
    // The filter is FUN_0056b3e0's (aura_filter.hpp): one for corpses only takes none of the living three.
    auto info = *fury_row;
    info.hit_par1 = 4;
    info.hit_par2 = 3;
    skill.aura_filter = 0xb783;
    burst(info);
    assert(fight.pending.empty());
    std::printf("OK: Lightning Fury's hit sends one to the unit it struck too, through the aurafilter, as game.exe's (FUN_005ab370)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_lightning_fury: %s\n", error.what());
    return 1;
}
