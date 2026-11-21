// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Clay Golem's slow, as tools/emu/clay_slow.py runs game.exe's FUN_005c6760:
// `--clay-slow` reads its cases (`slow type kind`) and prints each with
// `| put velocity rate length state`: rules::clay_slow (a kind 1..3 is a
// champion, unique or superunique, monster data +0x16 & 0xc), state 0x18 for
// rules::kClaySlowFrames.
// `--item-slow` reads tools/emu/item_slow.py's cases (`slow type kind boss
// hireling`, FUN_005c0310: the slow on the unit the golem strikes) and prints
// d2d's, which has none (nothing put).
// Without arguments: the rule's holds; with the game data, a Necromancer's
// Clay Golem through the real Fight::clay_slows and monster_states.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
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

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int slow_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long slow = 0, type = 0, kind = 0;
        if (!(fields >> slow >> type >> kind)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const int held = d2d::rules::clay_slow(int(slow), int(type), type == 1 && kind >= 1 && kind <= 3);
        if (held == 0) std::printf("%s | 0 0 0 0 0\n", line.c_str());
        else {
            const int cut = int(0u - unsigned(held));   // neg, as 0x5c67e9 (wraps at INT_MIN)
            std::printf("%s | 1 %d %d %d 24\n", line.c_str(), cut, cut, d2d::rules::kClaySlowFrames);
        }
    }
    return 0;
}

int item_slow_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long slow = 0, type = 0, kind = 0, boss = 0, hireling = 0;
        if (!(fields >> slow >> type >> kind >> boss >> hireling)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::printf("%s | 0 0 0 0 0\n", line.c_str());
    }
    return 0;
}

void rule_check() {
    using d2d::rules::clay_slow;
    assert(clay_slow(0, 1, false) == 0 && clay_slow(40, 2, false) == 0 && clay_slow(40, 4, false) == 0);
    assert(clay_slow(63, 1, false) == 63 && clay_slow(95, 1, false) == 90 && clay_slow(63, 1, true) == 50 && clay_slow(63, 0, false) == 50);
    assert(clay_slow(-20, 1, false) == -20);   // held from above only: a negative one speeds it up
    std::puts("OK: the slow is held to 50 (a player, a champion, unique or superunique) or 90 (another monster), none for 0 or another type");
}

// A level-30 Necromancer's Clay Golem (at 20): a zombie whose melee blow landed
// on it is slowed by its item_slow, a champion by 50 at most; a MonStats2 row
// without isAtt (hydra1) takes none; a re-hit keeps the velocity cut.
int golem_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Potter", false, true, rng);
    character.name = "Potter";
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
    const int clay_id = data.skills.by_name.at("Clay Golem");
    const auto& clay_row = *data.skills.get(clay_id);
    world->fight.skill_level = [&](int skill) { return skill == clay_id ? 20 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    world->fight.summon_one(clay_row, data.monsters.row(clay_row.summon), 20, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    const auto& pet = actor->pets.back();
    const int slow = d2d::rules::calc_dm(clay_row.par[2], clay_row.par[3], 20);   // aurastat item_slow, dm34
    assert(pet.slow == slow && slow > 50 && slow < 90);
    auto& monsters = world->fight.monsters;
    const auto add = [&](const char* row, int id) -> std::size_t {
        auto& added = monsters.emplace_back();
        added.type = data.monsters.row(row);
        added.id = id;
        added.stats.hit_points = added.hit_points = 500;
        added.unit.x = actor->player.x + 4; added.unit.y = actor->player.y;
        return monsters.size() - 1;
    };
    const auto zombie = add("zombie1", 90000), champion = add("zombie1", 90001), hydra = add("hydra1", 90002);
    monsters[champion].boss = d2d::rules::Boss::champion;
    Foe foe;
    foe.landed_by = { 90000, 90001, 90002 };
    world->fight.clay_slows(pet, foe, 100000);
    const std::uint32_t until = 100000 + std::uint32_t(d2d::rules::kClaySlowFrames) * 40;
    assert(monsters[zombie].slow_speed == slow && monsters[zombie].slow_rate == slow && monsters[zombie].slow_until == until);
    assert(monsters[champion].slow_speed == 50 && monsters[champion].slow_until == until);
    assert(monsters[hydra].slow_until == 0);
    world->fight.monster_states(100040);
    assert(monsters[zombie].speed_pct == -slow && monsters[zombie].anim_pct == -slow && monsters[champion].speed_pct == -50);
    // A re-hit while it's on (FUN_0056e970 0x56ea87): longer, the velocity cut kept, the rates anew.
    put_slow(data, monsters[zombie], 30, 110000);
    assert(monsters[zombie].slow_speed == slow && monsters[zombie].slow_rate == 30 && monsters[zombie].slow_until == 110000 + std::uint32_t(d2d::rules::kClaySlowFrames) * 40);
    // After it, a new one.
    world->fight.monster_states(monsters[zombie].slow_until);
    assert(monsters[zombie].speed_pct == 0 && monsters[zombie].anim_pct == 0);
    put_slow(data, monsters[zombie], 30, monsters[zombie].slow_until);
    assert(monsters[zombie].slow_speed == 30);
    std::printf("OK: a Clay Golem at 20 (item_slow %d) slows a zombie by %d, a champion by 50, a hydra not; a re-hit keeps the velocity cut\n", slow, slow);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--clay-slow") return slow_cases();
    if (argc > 1 && std::string_view(argv[1]) == "--item-slow") return item_slow_cases();
    rule_check();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP (the golem check): no d2data.mpq (D2_MPQ_DIR)"); return 0; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP (the golem check): needs the 1.14d data (D2_PATCH_INSTALLER)"); return 0; }
    return golem_check(*data);
} catch (const std::exception& error) {
    std::printf("test_clay_slow: %s\n", error.what());
    return 1;
}
