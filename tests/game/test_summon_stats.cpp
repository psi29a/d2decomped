// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon's stats, as tools/emu/summon_stats.py runs game.exe's
// FUN_005c4470: `--summon-stats` reads its cases (`skill lvl clvl life n
// (skill level)*n`) and prints each with `| life b: .. a: .. s: ..`: the
// pet's max life in 256ths (rules::summon_life on the case's rolled life),
// the passive stats and the aurastats rules::summon_stats gives (`stat=value`,
// by stat id, the non-zero ones) and the skills rules::summon_skill_levels
// gives (`skill=level`), each through the owner's calcs at the case's levels.
// Without arguments: summon_life on a few typed-in numbers, then, with the
// game data, a Necromancer's Raise Skeleton through the real
// Fight::summon_one (the corpse left out), its pet's max life to the 256th,
// and an Amazon's Decoy.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int summon_stats_cases(const GameData& game_data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int skill = 0, lvl = 0, clvl = 0, life = 0, count = 0;
        if (!(fields >> skill >> lvl >> clvl >> life >> count)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        std::map<int, int> levels;
        for (int k = 0; k < count; ++k) {
            int other = 0, other_level = 0;
            if (!(fields >> other >> other_level)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
            levels[other] = other_level;
        }
        levels[skill] = lvl;
        const auto* row = game_data.skills.get(skill);
        if (!row) { std::fprintf(stderr, "no skill %d\n", skill); return 1; }
        d2d::rules::CalcEnv env;
        const auto level_of = [&](int id) { const auto found = levels.find(id); return found == levels.end() ? 0 : found->second; };
        env.level = level_of;
        env.base_level = level_of;
        env.stat = [&](int stat) { return stat == 0xc ? clvl : 0; };
        env.clvl = clvl;
        const auto stats = d2d::rules::summon_stats(game_data.skills, *row, env, lvl);
        const auto maxhp = [&](const std::map<int, int>& part) { const auto found = part.find(7); return found == part.end() ? 0 : found->second; };
        const int max_life = d2d::rules::summon_life((life << 8) + maxhp(stats.passive) + maxhp(stats.aura), d2d::rules::eval_calc(game_data.skills, row->calc[0], env, skill, lvl));
        std::string out = std::to_string(max_life) + " b:";
        for (const auto& [stat, value] : stats.passive) if (value) out += " " + std::to_string(stat) + "=" + std::to_string(value);
        out += " a:";
        for (const auto& [stat, value] : stats.aura) if (value) out += " " + std::to_string(stat) + "=" + std::to_string(value);
        out += " s:";
        for (const auto& [given, given_level] : d2d::rules::summon_skill_levels(game_data.skills, *row, env, lvl)) out += " " + std::to_string(given) + "=" + std::to_string(given_level);
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

int life_checks() {
    using d2d::rules::summon_life;
    assert(summon_life(21 * 256, 0) == 21 * 256);
    assert(summon_life(181 * 256, 350) == 208512);                    // 814.5 points: the half kept
    assert(summon_life(0x100001, 300) == 0x100001 + 0x100001 / 100 * 300);   // over 0x100000: the hundredth first
    assert(summon_life(1000 * 256, 0x10001) == 1000 * 256 + 0x10001 / 100 * 1000 * 256);   // a pct over 0x10000 likewise
    assert(summon_life(4000 * 256, 3000) < 0);                        // 1,024,000 x 3,000 wraps in 32 bits, as game.exe's
    std::puts("OK: a summon's life");
    return 0;
}

// Raise Skeleton at 10 with Skeleton Mastery at 20 on a level-40
// Necromancer: a Normal skeleton's 21 life + 160 (maxhp 40960), + 350 %
// (calc1 par2 x 7) = 814.5 points, the half kept on the pet.
int raise_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Bones", false, true, rng);
    character.name = "Bones";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 40;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    const int raise = data.skills.by_name.at("Raise Skeleton"), mastery = data.skills.by_name.at("Skeleton Mastery");
    world->fight.skill_level = [&](int skill) { return skill == raise ? 10 : skill == mastery ? 20 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    const auto& row = *data.skills.get(raise);
    world->fight.summon_one(row, data.monsters.row(row.summon), 10, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    assert(actor->pets.size() == 1);
    const auto& pet = actor->pets.front().monster;
    assert(pet.max_life_fine() == 208512 && pet.life_fine() == 208512 && pet.stats.hit_points == 815);
    std::printf("OK: a raised skeleton's life %d/256\n", pet.max_life_fine());
    return 0;
}

// Decoy at 20 on a level-40 Amazon: its do (FUN_005dc000) sets its life to
// the owner's max life x calc3 / 100 (MulDiv) before FUN_005c4470, so calc1 %
// goes on that.
int decoy_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 0, "Twin", false, true, rng);
    character.name = "Twin";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 40;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    const int decoy = data.skills.by_name.at("Dopplezon");
    world->fight.skill_level = [&](int skill) { return skill == decoy ? 20 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    actor->character.stats.values[d2d::d2s::kMaxLife] = 333 * 256 + 77;
    const auto& row = *data.skills.get(decoy);
    const auto env = world->fight.calc_env();
    const int calc1 = d2d::rules::eval_calc(data.skills, row.calc[0], env, decoy, 20), calc3 = d2d::rules::eval_calc(data.skills, row.calc[2], env, decoy, 20);
    world->fight.summon_one(row, data.monsters.row(row.summon), 20, env, actor->player.x + 3, actor->player.y, 100000);
    assert(actor->pets.size() == 1 && calc1 > 0 && calc3 > 0);
    const int expect = d2d::rules::summon_life(d2d::rules::mul_div(333 * 256 + 77, calc3, 100), calc1);
    const auto& pet = actor->pets.front().monster;
    assert(pet.max_life_fine() == expect && pet.life_fine() == expect);
    std::printf("OK: a Decoy's life %d/256 (calc3 %d, calc1 %d)\n", expect, calc3, calc1);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--summon-stats") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--summon-stats needs D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--summon-stats needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return summon_stats_cases(*data);
    }
    if (life_checks() != 0) return 1;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("no game data: the summon check skipped"); return 0; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("no 1.14d data: the summon check skipped"); return 0; }
    return raise_check(*data) | decoy_check(*data);
} catch (const std::exception& error) {
    std::printf("test_summon_stats: %s\n", error.what());
    return 1;
}
