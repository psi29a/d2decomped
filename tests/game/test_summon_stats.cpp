// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon's stats, as tools/emu/summon_stats.py runs game.exe's
// FUN_005c4470: `--summon-stats` reads its cases (`skill lvl clvl life n
// (skill level)*n`) and prints each with `| life b: .. a: .. s: ..`: the
// pet's max life in 256ths (rules::summon_life on the case's rolled life),
// the passive stats and the aurastats rules::summon_stats gives (`stat=value`,
// by stat id, the non-zero ones) and the skills rules::summon_skill_levels
// gives (`skill=level`), each through the owner's calcs at the case's levels.
// `--summon-resist` and `--summon-level` do the same for
// tools/emu/summon_resist.py and summon_level.py.
// Without arguments: summon_life on a few typed-in numbers, then, with the
// game data, a Necromancer's Raise Skeleton through the real
// Fight::summon_one (the corpse left out), its pet's max life to the 256th,
// an Amazon's Decoy and a Revive above its owner's level.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
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

// tools/emu/summon_resist.py's cases (`value absorb_fire absorb_light
// absorb_cold`), each with `| stat=value ..`: the resists a raised skeleton
// or a golem gets from its owner's Summon Resist (rules::summon_resist).
int summon_resist_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int value = 0, fire = 0, light = 0, cold = 0;
        if (!(fields >> value >> fire >> light >> cold)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        std::string out;
        for (const auto& [stat, added] : d2d::rules::summon_resist(value, fire, light, cold)) out += (out.empty() ? "" : " ") + std::to_string(stat) + "=" + std::to_string(added);
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

// tools/emu/summon_level.py's cases (`clvl forced bonus difficulty
// expansion`), each with `| level ac tohit`: the level d2d gives the pet
// (rules::summon_level) and its MonLvl row's defense and attack rating at the
// difficulty.
int summon_level_cases(const GameData& game_data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int clvl = 0, forced = 0, bonus = 0, difficulty = 0, expansion = 0;
        if (!(fields >> clvl >> forced >> bonus >> difficulty >> expansion)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        const int level = d2d::rules::summon_level(clvl, forced, bonus);
        const auto& row = game_data.monsters.lvl[std::min<std::size_t>(std::size_t(level), game_data.monsters.lvl.size() - 1)];
        std::printf("%s | %d %d %d\n", line.c_str(), level, row.armor_class[std::size_t(difficulty)], row.to_hit[std::size_t(difficulty)]);
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
// (calc1 par2 x 7) = 814.5 points, the half kept on the pet; Summon Resist
// at 10 adds its passive_summon_resist to the skeleton's resists.
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
    const int resist = data.skills.by_name.at("Summon Resist");
    world->fight.skill_level = [&](int skill) { return skill == raise ? 10 : skill == mastery ? 20 : skill == resist ? 10 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    const auto& row = *data.skills.get(raise);
    world->fight.summon_one(row, data.monsters.row(row.summon), 10, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    assert(actor->pets.size() == 1);
    const auto& pet = actor->pets.front().monster;
    assert(pet.max_life_fine() == 208512 && pet.life_fine() == 208512 && pet.stats.hit_points == 815 && pet.stats.level == 40);
    // Summon Resist at 10: its passive_summon_resist as the skeleton's resists (FUN_005c40d0).
    int summon_resist = 0;
    for (const auto& passive : d2d::rules::passive_stats(data.skills, world->fight.calc_env()))
        if (passive.stat == d2d::rules::kPassiveSummonResist) summon_resist += passive.value;
    const auto& base_res = data.monsters.types[std::size_t(pet.type)].diff[0].res;
    const auto& pet_res = actor->pets.front().res;
    assert(summon_resist > 0 && pet_res[0] == base_res[2] + summon_resist && pet_res[3] == base_res[5] + summon_resist);
    // A Clay Golem gets it too (FUN_005c50c0); a Revive doesn't (FUN_005c56c0 has no FUN_005c40d0).
    const auto& golem_row = *data.skills.get(data.skills.by_name.at("Clay Golem"));
    world->fight.summon_one(golem_row, data.monsters.row(golem_row.summon), 5, world->fight.calc_env(), actor->player.x - 3, actor->player.y, 100000);
    const auto& golem = actor->pets.back();
    const auto& golem_base = data.monsters.types[std::size_t(golem.monster.type)].diff[0].res;
    assert(actor->pets.size() == 2 && golem.res[3] == golem_base[5] + summon_resist);
    // FUN_005c49e0: the skill level as the bonus (the golem at 5: 40 x 3 / 4 + 5 = 35), and the golem's own defense
    // kept with MonLvl's added.
    const auto& golem_type = data.monsters.types[std::size_t(golem.monster.type)];
    assert(golem.monster.stats.level == 35 && golem.monster.stats.armor_class == golem_type.diff[0].armor_class + data.monsters.lvl[35].armor_class[0]);
    // Its attack rating: MonLvl's (stat 0x13, 0x5c4adf) plus the swing's A1TH as written (FUN_005a4f50).
    const auto clay_stats = d2d::rules::summon_stats(data.skills, golem_row, world->fight.calc_env(), 5);
    const int clay_to_hit = clay_stats.passive.contains(d2d::d2s::kToHit) ? clay_stats.passive.at(d2d::d2s::kToHit) : 0;
    assert(golem.monster.stats.to_hit == golem_type.diff[0].a1_th + data.monsters.lvl[35].to_hit[0] + clay_to_hit);
    // A Charged Bolt Sentry (FUN_005d5e10 0x5d5fc1) passes its skill level too: 40 x 3 / 4 + 5.
    const auto& sentry_row = *data.skills.get(data.skills.by_name.at("Charged Bolt Sentry"));
    world->fight.summon_one(sentry_row, data.monsters.row(sentry_row.summon), 5, world->fight.calc_env(), actor->player.x - 3, actor->player.y - 3, 100000);
    assert(actor->pets.back().monster.stats.level == 35);
    // A Fire Golem absorbs fire (its aurastat item_absorbfire_percent): its fire resist gets none of it.
    const auto& fire_row = *data.skills.get(data.skills.by_name.at("FireGolem"));
    world->fight.summon_one(fire_row, data.monsters.row(fire_row.summon), 10, world->fight.calc_env(), actor->player.x, actor->player.y + 3, 100000);
    const auto& fire = actor->pets.back();
    const auto& fire_base = data.monsters.types[std::size_t(fire.monster.type)].diff[0].res;
    const auto fire_stats = d2d::rules::summon_stats(data.skills, fire_row, world->fight.calc_env(), 10);
    const auto own = [&](int stat) { int sum = 0; for (const auto* part : { &fire_stats.passive, &fire_stats.aura }) if (part->contains(stat)) sum += part->at(stat); return sum; };
    assert(own(d2d::d2s::kAbsorbFirePercent) > 0);
    assert(fire.res[0] == fire_base[2] + own(d2d::d2s::kFireResist) && fire.res[1] == fire_base[3] + summon_resist && fire.res[3] == fire_base[5] + summon_resist);
    // A Raven (do 114) at 5: its level forced to max(calc2, 1) (0x5c6a29), calc2 ulvl + par1 + lvl, above the
    // owner's (the Druid's do-119 summons force ulvl alone).
    const int raven_id = data.skills.by_name.at("Raven");
    const auto& raven_row = *data.skills.get(raven_id);
    const int calc2 = std::max(d2d::rules::eval_calc(data.skills, raven_row.calc[1], world->fight.calc_env(), raven_id, 5), 1);
    world->fight.summon_one(raven_row, data.monsters.row(raven_row.summon), 5, world->fight.calc_env(), actor->player.x + 3, actor->player.y + 3, 100000);
    assert(calc2 > 40 && actor->pets.back().monster.stats.level == calc2);
    std::printf("OK: a raised skeleton's life %d/256, Summon Resist %d on it and the golems, a Fire Golem's fire left, a Raven at %d\n", actor->pets.front().monster.max_life_fine(), summon_resist, calc2);
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

// Revive at 1 on a level-3 Necromancer raising a Normal Andariel (level 12): its do (FUN_005c56c0
// 0x5c57f6) takes her life x 3 / 12 (MulDiv) and her level down to 3 before FUN_005c4470's calc1 %.
int revive_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Raiser", false, true, rng);
    character.name = "Raiser";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 3;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    const int revive = data.skills.by_name.at("Revive");
    world->fight.skill_level = [&](int skill) { return skill == revive ? 1 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    const auto& row = *data.skills.get(revive);
    const int andariel = data.monsters.row("andariel");
    auto copy = world->fight.rng;
    const auto rolled = d2d::rules::monster_stats(data.monsters, andariel, 0, copy);
    const int calc1 = d2d::rules::eval_calc(data.skills, row.calc[0], world->fight.calc_env(), revive, 1);
    world->fight.summon_one(row, andariel, 1, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    assert(actor->pets.size() == 1 && rolled.level > 3);
    const int expect = d2d::rules::summon_life(d2d::rules::mul_div(rolled.hit_points << 8, 3, rolled.level), calc1);
    const auto& pet = actor->pets.front().monster;
    assert(pet.stats.level == 3 && pet.max_life_fine() == expect);
    std::printf("OK: a revived Andariel's life %d/256 (her %d at %d, down to 3)\n", expect, rolled.hit_points, rolled.level);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--summon-resist") return summon_resist_cases();
    if (argc > 1 && (std::string_view(argv[1]) == "--summon-stats" || std::string_view(argv[1]) == "--summon-level")) {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fprintf(stderr, "--summon-stats / --summon-level need D2_MPQ_DIR\n"); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fprintf(stderr, "--summon-stats / --summon-level need the 1.14d data (D2_PATCH_INSTALLER)\n"); return 1; }
        return std::string_view(argv[1]) == "--summon-level" ? summon_level_cases(*data) : summon_stats_cases(*data);
    }
    if (life_checks() != 0) return 1;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("no game data: the summon check skipped"); return 0; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("no 1.14d data: the summon check skipped"); return 0; }
    return raise_check(*data) | decoy_check(*data) | revive_check(*data);
} catch (const std::exception& error) {
    std::printf("test_summon_stats: %s\n", error.what());
    return 1;
}
