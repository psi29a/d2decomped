// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Hit function 20, Lightning Fury (FUN_005ab370), through the REAL Fight::burst: an area walk
// (FUN_0056b7e0) of sHitPar1 subtiles (else aurarange, at least 1) that sends up to sHitPar2 (else calc1, at
// least 1) sub-missiles (FUN_005ab2a0), one to each unit its filter takes: the skill's aurafilter, else
// 0xa783. The walk is handed the owner (FUN_00552fd0; none, no burst): it skips only the owner, so the
// unit the bolt struck gets one too, and the filter's line (0x200) runs from the owner. Each case against
// game.exe's own, tools/emu/hit_fury.py (Missiles.txt row 206).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
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
#include <utility>

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

    // game.exe's walk (tools/emu/hit_fury.py): the missile on subtile (100, 200), in the room its owner
    // (98, 200), the unit it struck (101, 200), another monster (100, 203) and one out of reach (110, 200).
    struct Made { int row, hit_par1, hit_par2; std::uint32_t aura_filter; int aurarange, calc1; std::uint32_t filter; int made; std::array<int, 6> spots; int owned; };
    static constexpr Made kMade[] = {
#include "hit_fury_cases.inc"
    };
    const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    assert(fight.heroes.size() == 1);
    auto& hero = *fight.heroes.front();
    hero.player.x = centre(98);
    hero.player.y = centre(200);
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
    const auto burst = [&](GameData::MissileInfo& info, bool owned = true, int at_x = 100, int at_y = 200) {
        Missile bolt;
        bolt.info = &info;
        bolt.skill = skill_id;
        bolt.level = 1;
        bolt.friendly = true;
        bolt.caster = owned ? hero.id : hero.id + 1;       // a player no longer in the game: no owner
        bolt.x = centre(at_x);
        bolt.y = centre(at_y);
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
        assert(want.owned != 1 || want.filter == (want.aura_filter != 0 ? want.aura_filter : 0xa783));
        const auto bolt = burst(info, want.owned == 1);
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
    assert(checked == 66);
    // The filter is FUN_0056b3e0's (aura_filter.hpp): one for corpses only takes none of the living three.
    auto info = *fury_row;
    info.hit_par1 = 4;
    info.hit_par2 = 3;
    skill.aura_filter = 0xb783;
    burst(info);
    assert(fight.pending.empty());
    // The walk passes a noaura monster (MonStats flag 0x1b, FUN_0056b7e0 at 0x56b979).
    const auto no_aura = std::ranges::find_if(data->monsters.types, [](const auto& type) { return type.no_aura; });
    assert(no_aura != data->monsters.types.end());
    skill.aura_filter = 0;
    burst(info);
    const std::size_t all = fight.pending.size();
    fight.monsters[1].type = int(no_aura - data->monsters.types.begin());
    burst(info);
    assert(all == 2 && fight.pending.size() == 1);
    // The line (0x200 in the default 0xa783) runs from the owner, not the missile: FUN_0056b7e0 hands
    // FUN_0056b3e0 its own unit as the caster (0x56b95c), the owner here. A wall between the owner and the
    // monster stops it; one the missile sees past doesn't help.
    const auto wall = [&](int at_x, int at_y) { return open.blocked(centre(at_x), centre(at_y), 0x04); };
    const auto sees = [&](int from_x, int from_y, int to_x, int to_y) { return !d2d::rules::sight_blocked(from_x, from_y, 0, to_x, to_y, 0, wall); };
    const int width = open.ds1.width() * 5, height = open.ds1.height() * 5;
    int wall_x = -1, wall_y = -1;
    for (int at_y = 0; at_y < height && wall_x < 0; ++at_y)
        for (int at_x = 3; at_x + 3 < width && wall_x < 0; ++at_x)
            if (wall(at_x, at_y) && !wall(at_x - 3, at_y) && !wall(at_x + 3, at_y) && !wall(at_x + 3, at_y + 2)
                && !sees(at_x - 3, at_y, at_x + 3, at_y) && sees(at_x + 3, at_y + 2, at_x + 3, at_y)) {
                wall_x = at_x;
                wall_y = at_y;
            }
    assert(wall_x >= 0);
    fight.monsters.clear();
    fight.monsters.push_back(make_monster(*data, fallen, centre(wall_x + 3), centre(wall_y), monster_rng, 0));
    info.hit_par1 = 4;
    info.hit_par2 = 3;
    hero.player.x = centre(wall_x - 3);
    hero.player.y = centre(wall_y);
    burst(info, true, wall_x + 3, wall_y + 2);
    assert(fight.pending.empty());
    hero.player.x = centre(wall_x + 3);
    hero.player.y = centre(wall_y + 2);
    burst(info, true, wall_x + 3, wall_y + 2);
    assert(fight.pending.size() == 1);
    std::printf("OK: Lightning Fury's hit sends one to the unit it struck too, through the aurafilter, its line from the owner, as game.exe's (FUN_005ab370)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_lightning_fury: %s\n", error.what());
    return 1;
}
