// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon's own life regeneration, as tools/emu/pet_regen.py runs game.exe's
// FUN_00573cb0 (stat 0x4a) and FUN_005a6920 (its unit event 3): `--pet-regen`
// reads its cases (`roll damage_regen life max_life frames`) and prints each
// with the regeneration d2d gives the pet, its life after the frames and
// whether the event is still filed (rules::monster_regen, rules::regen_frame).
// Without arguments: the rule on typed-in numbers (no game data, so CI runs
// it), then a raised skeleton hurt in a game with the MPQs (D2_MPQ_DIR): it
// regains its stat 0x4a a frame from the frame after (Fight::pet_hurt,
// pets_turn).

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int regen_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int roll = 0, damage_regen = 0, life = 0, max_life = 0, frames = 0;
        if (!(fields >> roll >> damage_regen >> life >> max_life >> frames)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        // The event filed by the hurt, a frame of it while it stays filed.
        const int regen = d2d::rules::monster_regen(roll, damage_regen);
        bool filed = true;
        for (int frame = 0; frame < frames && filed && regen != 0; ++frame) {
            const auto step = d2d::rules::regen_frame(life, max_life, regen);
            life = step.life;
            filed = step.filed;
        }
        std::printf("%s | %d %d %d\n", line.c_str(), regen, life, int(filed));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--pet-regen") return regen_cases();
    // A skeleton's 21 life at DamageRegen 4: 5/256 a frame; past 0x7fffffff / 4 the life shifts first; none at 0.
    assert(d2d::rules::monster_regen(21, 4) == 5 && d2d::rules::monster_regen(100, 3) == 18 && d2d::rules::monster_regen(15, 0) == 0);
    assert(d2d::rules::monster_regen(0x7fffff, 0xfff) == (0x7fffff00 >> 12) * 0xfff && d2d::rules::monster_regen(9, 0x1000) == 0);
    // Up to the max life and no further; the event ends only once a frame passes it.
    assert(d2d::rules::regen_frame(100, 200, 5).life == 105 && d2d::rules::regen_frame(100, 200, 5).filed);
    assert(d2d::rules::regen_frame(200, 200, 5).life == 200 && !d2d::rules::regen_frame(200, 200, 5).filed);
    assert(d2d::rules::regen_frame(195, 200, 5).life == 200 && d2d::rules::regen_frame(195, 200, 5).filed);
    std::puts("OK: stat 0x4a from the roll and DamageRegen, a frame of it up to the max life (FUN_00573cb0, FUN_005a6920)");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Bones", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Bones";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.stats.values[d2d::d2s::kLevel] = 24;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    const auto& raise = *data->skills.get(data->skills.by_name.at("Raise Skeleton"));
    world.fight.skill_level = [&](int id) { return id == raise.id ? 8 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    world.fight.summon_one(raise, int(data->monsters.by_id.at(raise.summon)), 8, world.fight.calc_env(), actor.player.x + 1, actor.player.y, now, -1);
    assert(actor.pets.size() == 1 && actor.pets.front().monster.stats.regen == 5);   // 21 life at DamageRegen 4
    auto& pet = actor.pets.front();
    const int full = pet.monster.max_life_fine();
    world.fight.pet_hurt(pet, 10, now);
    const int hurt = pet.monster.life_fine();
    assert(hurt == full - 10 * 256);
    now += 40;
    for (int frame = 1; frame <= 4; ++frame, now += 40) {
        world.tick({}, now, now - 40);
        assert(actor.pets.size() == 1 && pet.monster.life_fine() == hurt + 5 * frame);
    }
    std::puts("OK: a hurt skeleton regains 5/256 of a point a frame from the next frame (Fight::pet_hurt, pets_turn)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_regen: %s\n", error.what());
    return 1;
}
