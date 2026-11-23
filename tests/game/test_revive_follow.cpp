// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A revived monster's first AI step (FUN_005b0ff0 with its owner, set by
// FUN_005c55c0): 8 cells off the Necromancer, a monster beside it, it walks
// back to a spot 0x13 about him, not at the monster; 2 cells off, the step
// doesn't act and it takes the monster, by its own search (mode 5: an unaware
// one 6 cells off too); a subtile off him, it backs off. Needs the
// game data; SKIPs without it.

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    auto loaded = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!loaded || !loaded->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const auto& data = *loaded;
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Gravedigger", false, true, rng);
    character.name = "Gravedigger";
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
    const int revive_id = data.skills.by_name.at("Revive");
    const auto& revive_row = *data.skills.get(revive_id);
    world->fight.skill_level = [&](int skill) { return skill == revive_id ? 1 : 0; };
    world->fight.skill_base = world->fight.skill_level;
    auto* actor = world->actor;
    const int zombie = data.monsters.row("zombie1");
    const auto revived_at = [&](float apart, float foe_off = 0.4f, bool foe_aware = true) -> Fight::Pet& {
        actor->pets.clear();
        world->fight.monsters.clear();
        auto& corpse = world->fight.monsters.emplace_back();
        corpse.type = zombie;
        world->fight.summon_one(revive_row, zombie, 1, world->fight.calc_env(), actor->player.x + apart, actor->player.y, 100000, 0);
        auto& foe = world->fight.monsters.emplace_back();      // a live zombie beside the revived one
        foe.type = zombie; foe.id = 777; foe.stats.hit_points = foe.hit_points = 50; foe.mode = "NU"; foe.aware = foe_aware;
        auto& pet = actor->pets.back();
        foe.unit.x = pet.monster.unit.x + foe_off; foe.unit.y = pet.monster.unit.y;
        return pet;
    };
    const Crowd crowd;
    // 8 cells (40 subtiles) off: over 0x14, it walks back to a spot 0x13 about its owner.
    auto& distant = revived_at(8.f);
    world->fight.pets_turn(200000, 0.04f, crowd);
    assert(distant.homing && distant.target < 0 && distant.monster.unit.walking);
    assert(std::abs(distant.monster.unit.goal_x * 5 - actor->player.x * 5) <= 0x13 + 1 && std::abs(distant.monster.unit.goal_y * 5 - actor->player.y * 5) <= 0x13 + 1);
    // 2 cells (10 subtiles) off: the step doesn't act, it takes the monster beside it.
    auto& close_by = revived_at(2.f);
    world->fight.pets_turn(200000, 0.04f, crowd);
    assert(!close_by.homing && close_by.target == 1);
    // Its search is its own (alignment 2, FUN_005dd0b0 mode 5): an unaware zombie 6 cells (30 subtiles) off,
    // within 0x23, is taken.
    auto& searching = revived_at(2.f, 6.f, false);
    world->fight.pets_turn(200000, 0.04f, crowd);
    assert(searching.monster.align == 2 && searching.target == 1);
    // A subtile east of its owner: within 1, it backs off 0x13 east (FUN_005defe0: away on each axis, none on y).
    auto& touching = revived_at(0.2f);
    const auto sub = [](float cells) { return int(std::floor(cells * 5)); };
    const int start_x = sub(touching.monster.unit.x), start_y = sub(touching.monster.unit.y);
    assert(start_x == sub(actor->player.x) + 1 && start_y == sub(actor->player.y));
    world->fight.pets_turn(200000, 0.04f, crowd);
    assert(touching.homing && sub(touching.monster.unit.goal_x) == start_x + 0x13 && sub(touching.monster.unit.goal_y) == start_y);
    std::puts("OK: a revived monster 8 cells off its owner walks back; 2 off it fights, its search mode 5; 1 subtile off it backs off 0x13");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_revive_follow: %s\n", error.what());
    return 1;
}
