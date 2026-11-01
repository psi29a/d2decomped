// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--resist CASES`: a monster's resistance after a curse and the element
// damage it lets through, the cases of tools/emu/sk_resist.py (game.exe's
// FUN_005c3540, FUN_0057be00, FUN_0057c6c0), one line each, to diff:
// `id adj effres dealt`. adj is the REAL Fight::target_of (the monster's curse
// carries the case's one aurastat value); dealt is the REAL
// rules::missile_blow (256ths: damage x 256 + frac) for magic, fire, lightning
// and cold; physical and poison print "-" (whole-point / per-tick paths).

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
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
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
    if (argc < 3 || std::string_view(argv[1]) != "--resist") {
        std::printf("SKIP: an oracle mode, run `test_sk_resist --resist CASES` (see tools/emu/sk_resist.py)\n");
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
    const auto lower = data->skills.by_name.find("Lower Resist");
    assert(lower != data->skills.by_name.end());
    // Lower Resist's row carries the case's one aurastat; the curse path reads it.
    auto& curse_row = const_cast<d2d::rules::Skill&>(*data->skills.get(int(lower->second)));
    World world(&*data, -1, -1);
    world.enter(character);
    d2d::rules::Rng spawn_rng{ 5 };
    const Monster made_monster = make_monster(*data, int(fallen->second), world.actor->player.x, world.actor->player.y, spawn_rng, 0);
    // case elem: 0 physical, 1 magic, 2 fire, 3 lightning, 4 cold, 5 poison
    static constexpr std::array<int, 6> kStat{ 36, 37, 39, 41, 43, 45 };       // -> aurastat (ItemStatCost ids)
    static constexpr std::array<int, 6> kEtype{ -1, 4, 0, 1, 2, 3 };          // -> Skill::etype order
    std::ifstream cases(argv[2]);
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        long long case_id = 0;
        int elem = 0, base = 0, value = 0, pierce = 0, roll = 0;
        if (!(fields >> case_id >> elem >> base >> value >> pierce >> roll) || elem < 0 || elem > 5) continue;
        const auto index = std::size_t(elem);
        curse_row.aurastat.fill(-1);
        curse_row.aurastat[0] = kStat[index];
        curse_row.aura_calc[0].code = { { d2d::rules::Calc::Const, value } };
        Monster monster = made_monster;
        monster.boss_res = {};
        monster.boss_res[index] = base - monster.target(*data).res[index];
        monster.curse = { int(lower->second), 1, 0 };
        world.fight.monsters.clear();
        world.fight.add_monster(monster);
        const auto target = world.fight.target_of(0);
        const int etype = kEtype[index];
        int res = target.res[index];
        std::printf("%lld %d ", case_id, res - base);
        if (etype >= 0 && etype < 4 && res < 100) res = std::max(res - pierce, -100);
        std::printf("%d ", res);
        if (etype < 0 || etype == 3) { std::printf("-\n"); continue; }
        std::array<int, 4> pierces{};
        if (etype < 4) pierces[std::size_t(etype)] = pierce;
        d2d::rules::Rng blow_rng;
        const auto damage = d2d::rules::MissileDamage{ .etype = etype, .elo = roll, .ehi = roll };
        const auto blow = d2d::rules::missile_blow(damage, target, pierces, blow_rng);
        std::printf("%d\n", blow.damage * 256 + blow.frac);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
