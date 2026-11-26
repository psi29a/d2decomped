// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's unit event 3 under its states (FUN_005a6920, rules::regen_event):
// tools/emu/pet_poison_town.py's frozen answers from game.exe (the life group
// FUN_0063a750 taking the own stat 0x4a off, preventheal 0x34, and the room
// test FUN_00620bb0 / FUN_0061ab00 sparing a unit under a point). Then, with
// the MPQs (D2_MPQ_DIR), a poisoned skeleton under a point of life: in the
// Rogue Encampment its poison takes nothing, in the Blood Moor it dies
// (Fight::pets_turn -> pet_regen). Without the MPQs only that part SKIPs:
// the game.exe cases have run, so the test passes (tests/README.md).

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
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
#include <iterator>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

struct Case { int life, max_life, total, base, group, prevent, spared, frames, life_after, filed, dead; };
constexpr Case kCases[] = {
#include "pet_poison_town_cases.inc"
};

// The event each frame while filed and the unit alive, as the oracle's rig runs it.
int game_cases() {
    int bad = 0;
    for (const auto& one : kCases) {
        int life = one.life;
        bool filed = true, dead = false;
        for (int frame = 0; frame < one.frames && filed && !dead; ++frame) {
            const auto step = d2d::rules::regen_event(life, one.max_life, one.total, one.base, one.group != 0, one.prevent != 0, one.spared != 0);
            dead = step.life == 0 && step.life != life;
            life = step.life;
            filed = step.filed;
        }
        if (life != one.life_after || int(filed) != one.filed || int(dead) != one.dead) {
            if (++bad <= 5)
                std::printf("DIFF life %d total %d base %d group %d prevent %d spared %d frames %d: game %d %d %d, d2d %d %d %d\n", one.life, one.total,
                            one.base, one.group, one.prevent, one.spared, one.frames, one.life_after, one.filed, one.dead, life, int(filed), int(dead));
        }
    }
    return bad;
}

// A raised skeleton poisoned at half a point of life on `level_id` for four frames: its life after.
int poisoned_skeleton(const GameData& data, int level_id) {
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(data, d2d::d2s::kNecromancer, "Bones", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Bones";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    World world(&data, -1, -1);
    world.enter(character);
    Level open = *data.level(level_id);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    const auto& raise = *data.skills.get(data.skills.by_name.at("Raise Skeleton"));
    world.fight.skill_level = [&](int id) { return id == raise.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    world.fight.summon_one(raise, int(data.monsters.by_id.at(raise.summon)), 1, world.fight.calc_env(), actor.player.x + 1, actor.player.y, now, -1);
    assert(actor.pets.size() == 1);
    auto& skeleton = actor.pets.front().monster;
    skeleton.set_life_fine(128);
    Foe poisoner{};
    poisoner.poison = 40; poisoner.poison_ticks = 25;
    world.fight.pet_poisoned(actor.pets.front(), poisoner, now);
    now += 40;
    for (int frame = 0; frame < 4 && !actor.pets.empty(); ++frame, now += 40) world.tick({}, now, now - 40);
    return actor.pets.empty() ? 0 : actor.pets.front().monster.life_fine();
}

}  // namespace

int main() try {
    const int bad = game_cases();
    std::printf("%s: %zu FUN_005a6920 cases, %d differ\n", bad ? "FAIL" : "OK", std::size(kCases), bad);
    if (bad) return 1;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: the skeleton cases need d2data.mpq (D2_MPQ_DIR)"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: the skeleton cases need the 1.14d data (D2_PATCH_INSTALLER)"); return 0; }
    const int in_town = poisoned_skeleton(*data, d2d::rules::level_ids::kRogueEncampment);
    const int outside = poisoned_skeleton(*data, d2d::rules::level_ids::kBloodMoor);
    std::printf("poisoned at 128/256: %d in town, %d in the Blood Moor\n", in_town, outside);
    assert(in_town == 128 && outside == 0);
    std::puts("OK: under a point, a pet's poison takes nothing in town (FUN_0061ab00) and kills it outside");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_poison_town: %s\n", error.what());
    return 1;
}
