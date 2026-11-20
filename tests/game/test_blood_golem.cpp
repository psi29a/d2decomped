// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Blood Golem's life steal, as tools/emu/blood_golem.py runs game.exe's
// FUN_005c6530: `--blood-golem` reads its cases (`target drain align phys
// tlife owner olife omax glife gmax lvl`, 256ths) and prints each with
// `| owner_life golem_life`: rules::blood_golem_steal at Blood Golem's
// Params (the steal % its dm of Param1 / Param2 at the level).
// Without arguments, with the game data: a Necromancer's Blood Golem striking
// an evil zombie through the real Fight::pet_strike: both heal.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <pets.hpp>
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
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int blood_golem_cases(const GameData& data) {
    const auto& golem = *data.skills.get(data.skills.by_name.at("BloodGolem"));
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int target = 0, drain = 0, align = 0, phys = 0, target_life = 0, has_owner = 0, owner_life = 0, owner_max = 0, golem_life = 0, golem_max = 0, level = 0;
        if (!(fields >> target >> drain >> align >> phys >> target_life >> has_owner >> owner_life >> owner_max >> golem_life >> golem_max >> level)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto lives = d2d::rules::blood_golem_steal(target == 1, drain, align == 0, phys, target_life, d2d::rules::calc_dm(golem.par[0], golem.par[1], level),
                                                         golem.par[2], has_owner != 0, { owner_life, golem_life }, owner_max, golem_max);
        std::printf("%s | %d %d\n", line.c_str(), lives.owner_life, lives.golem_life);
    }
    return 0;
}

// A level-30 Necromancer at 100 points with his Blood Golem (at 5) at 50
// points; the golem's melee blow of 100 physical on an evil zombie: Param3 %
// of the steal to him, the rest to the golem (FUN_005c6530).
int strike_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Bleeder", false, true, rng);
    character.name = "Bleeder";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    character.stats.values[d2d::d2s::kLevel] = 30;
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    const int golem_id = data.skills.by_name.at("BloodGolem");
    const auto& golem_row = *data.skills.get(golem_id);
    world->fight.skill_level = [&](int skill) { return skill == golem_id ? 5 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    actor->character.stats.values[d2d::d2s::kMaxLife] = 400 * 256;
    actor->character.stats.values[d2d::d2s::kLife] = 100 * 256;
    world->fight.summon_one(golem_row, data.monsters.row(golem_row.summon), 5, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    auto& pet = actor->pets.back();
    assert(pet.level == 5 && pet.monster.max_life_fine() > 50 * 256);
    pet.monster.set_life_fine(50 * 256);
    auto& zombie = world->fight.monsters.emplace_back();
    zombie.type = data.monsters.row("zombie1");
    zombie.id = 90000;
    zombie.stats.hit_points = zombie.hit_points = 5000;
    zombie.unit.x = actor->player.x + 4; zombie.unit.y = actor->player.y;
    d2d::rules::Blow blow{ .hit = true };
    blow.damage = 100;
    blow.phys_fine = 100 * 256;
    const auto expect = d2d::rules::blood_golem_steal(true, data.monsters.types[std::size_t(zombie.type)].diff[0].drain, true, 100 * 256, 5000 * 256,
                                                      d2d::rules::calc_dm(golem_row.par[0], golem_row.par[1], 5), golem_row.par[2], true,
                                                      { 100 * 256, 50 * 256 }, 400 * 256, pet.monster.max_life_fine());
    world->fight.pet_strike(pet, world->fight.monsters.size() - 1, blow, 100000);
    assert(expect.owner_life > 100 * 256 && expect.golem_life > 50 * 256);
    assert(actor->character.stats.values[d2d::d2s::kLife] == expect.owner_life && pet.monster.life_fine() == expect.golem_life);
    // A good (align 1) one gives nothing (FUN_006259b0); a zombie at 3 points gives off those 3 alone (held to its life
    // before the blow, 0x5c65b4).
    const auto before = std::pair{ actor->character.stats.values[d2d::d2s::kLife], pet.monster.life_fine() };
    zombie.align = 1;
    world->fight.pet_strike(pet, world->fight.monsters.size() - 1, blow, 100000);
    assert(actor->character.stats.values[d2d::d2s::kLife] == before.first && pet.monster.life_fine() == before.second);
    zombie.align = 0;
    zombie.hit_points = 3;
    pet.monster.set_life_fine(50 * 256);
    actor->character.stats.values[d2d::d2s::kLife] = 100 * 256;
    const auto capped = d2d::rules::blood_golem_steal(true, data.monsters.types[std::size_t(zombie.type)].diff[0].drain, true, 3 * 256, 3 * 256,
                                                      d2d::rules::calc_dm(golem_row.par[0], golem_row.par[1], 5), golem_row.par[2], true,
                                                      { 100 * 256, 50 * 256 }, 400 * 256, pet.monster.max_life_fine());
    world->fight.pet_strike(pet, world->fight.monsters.size() - 1, blow, 100000);
    assert(actor->character.stats.values[d2d::d2s::kLife] == capped.owner_life && pet.monster.life_fine() == capped.golem_life && capped.golem_life < expect.golem_life);
    std::printf("OK: a Blood Golem's hit heals its owner to %d/256 and itself to %d/256; none off a good one; a 3-point zombie's 3 alone\n", expect.owner_life, expect.golem_life);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--blood-golem") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fputs("--blood-golem needs D2_MPQ_DIR\n", stderr); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fputs("--blood-golem needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        return blood_golem_cases(*data);
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    return strike_check(*data);
} catch (const std::exception& error) {
    std::printf("test_blood_golem: %s\n", error.what());
    return 1;
}
