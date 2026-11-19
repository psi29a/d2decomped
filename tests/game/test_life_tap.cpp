// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Life Tap's heal, as tools/emu/life_tap.py runs game.exe's FUN_005c61e0:
// `--life-tap` reads its cases (`striker life max damage pct`, 256ths) and
// prints each with `| life`: rules::life_tap_life on the striker's life.
// Without arguments: life_tap_life on a few typed-in numbers, then, with the
// game data, a Necromancer's skeleton striking a Life-Tapped zombie through
// the real Fight::land: the skeleton heals.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
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
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int life_tap_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int striker = 0, damage = 0, pct = 0;
        std::int64_t life = 0, max_life = 0;
        if (!(fields >> striker >> life >> max_life >> damage >> pct)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        std::printf("%s | %lld\n", line.c_str(), static_cast<long long>(d2d::rules::life_tap_life(life, max_life, damage, pct)));
    }
    return 0;
}

int heal_checks() {
    using d2d::rules::life_tap_life;
    assert(life_tap_life(100 * 256, 400 * 256, 50 * 256, 10) == 105 * 256);
    assert(life_tap_life(399 * 256, 400 * 256, 50 * 256, 10) == 400 * 256);   // held at the max
    // The hit's 256ths count (FUN_00483360 on +8): 255 at 15 % heals 38, not the whole points' 0.
    assert(life_tap_life(100 * 256, 400 * 256, 255, 15) == 100 * 256 + 38);
    assert(life_tap_life(100 * 256, 400 * 256, 1000 * 256, -20) == 1);       // a negative % takes it to 1 at least
    assert(life_tap_life(0, 400 * 256, 1000 * 256, 10) == 0);                // nothing on a striker at 0
    std::puts("OK: Life Tap's heal");
    return 0;
}

// A level-30 Necromancer's skeleton at 5 points strikes a zombie under his
// Life Tap at 1: it heals calc1 % of the blow's physical (the hit's +8;
// FUN_005c61e0 takes any striker, 0x5c620a), as the player would; a fire-only
// blow heals nothing (its damage is at +0x4c, not +8).
int pet_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Tapper", false, true, rng);
    character.name = "Tapper";
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
    const int raise = data.skills.by_name.at("Raise Skeleton"), life_tap = data.skills.by_name.at("Life Tap");
    world->fight.skill_level = [&](int skill) { return skill == raise || skill == life_tap ? 1 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    const auto& raise_row = *data.skills.get(raise);
    const int pet_id = world->fight.summon_one(raise_row, data.monsters.row(raise_row.summon), 1, world->fight.calc_env(), actor->player.x + 3, actor->player.y, 100000);
    auto& pet = actor->pets.back().monster;
    assert(pet.id == pet_id && pet.max_life_fine() > 5 * 256);
    pet.set_life_fine(5 * 256);
    auto& zombie = world->fight.monsters.emplace_back();
    zombie.type = data.monsters.row("zombie1");
    zombie.id = 90000;
    zombie.stats.hit_points = zombie.hit_points = 500;
    zombie.unit.x = actor->player.x + 4; zombie.unit.y = actor->player.y;
    zombie.curse = { life_tap, 1, 1'000'000'000u, false };
    const int pct = d2d::rules::eval_calc(data.skills, data.skills.get(life_tap)->calc[0], world->fight.calc_env(), life_tap, 1);
    d2d::rules::Blow fire{ .hit = true };
    fire.damage = 100;                                                   // all fire: phys_fine 0
    (void)world->fight.land(world->fight.monsters.size() - 1, fire, false, 100000, pet_id);
    assert(pet.life_fine() == 5 * 256);
    d2d::rules::Blow blow{ .hit = true };
    blow.damage = 30;                                                    // 10 physical, 20 fire
    blow.phys_fine = 10 * 256;
    (void)world->fight.land(world->fight.monsters.size() - 1, blow, false, 100000, pet_id);
    const auto expect = d2d::rules::life_tap_life(5 * 256, pet.max_life_fine(), 10 * 256, pct);
    assert(pct > 0 && pet.life_fine() == expect && expect > 5 * 256 && expect < pet.max_life_fine());
    std::printf("OK: a skeleton striking a Life-Tapped zombie heals to %d/256 (%d %%)\n", pet.life_fine(), pct);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--life-tap") return life_tap_cases();
    if (heal_checks() != 0) return 1;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("no game data: the striker check skipped"); return 0; }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("no 1.14d data: the striker check skipped"); return 0; }
    return pet_check(*data);
} catch (const std::exception& error) {
    std::printf("test_life_tap: %s\n", error.what());
    return 1;
}
