// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--armor-hit CASES`: Frozen Armor's answer to a melee striker (aura event
// 2) through the REAL Fight::buff_events, the cases of tools/emu/armor_hit.py
// (game.exe's FUN_005cac50 -> FUN_0057c6c0 -> FUN_0057af80 / FUN_0057b230):
// `id chill freeze frames`, the ticks from now each ends (or `-`) and the
// calc1 frames d2d computed, one line each, to diff.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--armor-hit") {
        std::printf("SKIP: an oracle mode, run `test_armor_hit --armor-hit CASES` (see tools/emu/armor_hit.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: the oracle is 1.14d's game.exe: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    auto& cold_type = const_cast<d2d::rules::MonType&>(data->monsters.types[fallen->second]);
    const auto real_diff = cold_type.diff;
    const bool real_boss_column = cold_type.boss_column;
    const auto skill_id = [&](const char* name) {
        const auto found = data->skills.by_name.find(name);
        assert(found != data->skills.by_name.end());
        return int(found->second);
    };
    const int frozen_armor = skill_id("Frozen Armor"), shiver_armor = skill_id("Shiver Armor"), chilling_armor = skill_id("Chilling Armor");
    const auto* frozen_row = data->skills.get(frozen_armor);
    assert(frozen_row && frozen_row->aurastate == "frozenarmor");
    World world(&*data, -1, -1);
    world.enter(character);
    const World::Player start = *world.actor;
    d2d::rules::Rng spawn_rng{ 5 };
    const Monster fallen_made = make_monster(*data, int(fallen->second), world.actor->player.x, world.actor->player.y, spawn_rng, 0);
    const std::uint32_t now = 2000 * kTickMs;
    int shiver_points = 0, chilling_points = 0;
    world.fight.skill_base = [&](int id) { return id == shiver_armor ? shiver_points : id == chilling_armor ? chilling_points : 0; };
    std::ifstream case_file(argv[2]);
    std::string line;
    while (std::getline(case_file, line)) {
        std::istringstream fields(line);
        int id = 0, difficulty = 0, cold_effect = 0, res = 0, pierce = 0, level = 0, synergy = 0, kind = 0, bcol = 0, damage = 0;
        if (!(fields >> id >> difficulty >> cold_effect >> res >> pierce >> level >> synergy >> kind >> bcol >> damage)) continue;
        *world.actor = start;
        world.actor->psum[d2d::d2s::kPassiveColdPierce] = pierce;
        world.actor->self_states.push_back({ frozen_armor, level, now + 3000 * kTickMs });
        chilling_points = synergy / 2;
        shiver_points = synergy - chilling_points;
        world.fight.game_difficulty = difficulty;
        cold_type.diff[std::size_t(difficulty)].cold_effect = cold_effect;
        cold_type.boss_column = bcol != 0;
        Monster monster = fallen_made;
        monster.difficulty = difficulty;
        monster.boss = d2d::rules::Boss(kind);
        monster.stats.hit_points = monster.hit_points = 1000000;
        monster.life_frac = 0;
        monster.boss_res = {};
        monster.boss_res[4] = res - monster.target(*data).res[4];
        world.fight.monsters.clear();
        world.fight.add_monster(monster);
        const auto target = world.fight.target_of(0);
        Foe foe;
        foe.melee_by.push_back(world.fight.monsters.back().id);
        if (damage > 0) foe.damaged_by.push_back(world.fight.monsters.back().id);
        world.fight.buff_events(foe, now);
        const auto& struck = world.fight.monsters.back();
        const auto ticks = [&](std::uint32_t until) { return until > now ? std::to_string((until - now) / kTickMs) : std::string("-"); };
        std::printf("%d %s %s %d%s\n", id, ticks(struck.chill_until).c_str(), ticks(struck.stun_until).c_str(), world.fight.calc(*frozen_row, frozen_row->calc[0], level),
                    target.res[4] == res ? "" : " RESFAIL");
        cold_type.diff = real_diff;
        cold_type.boss_column = real_boss_column;
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
