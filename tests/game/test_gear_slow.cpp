// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Gear's item_slow (stat 150; its ItemStatCost events domeleedamage /
// domissiledamage, func 19, FUN_005c0310): a landed hit from the player's or
// the merc's fighter carries the gear's value (Blow::slow) and Fight::land
// puts state 0x18 on the target through rules::item_slow (the rule
// tools/emu/item_slow.py checks): 50 at most on a champion, 90 on another.
// Without the game data: player_blow carries the value on a hit only.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

void blow_check() {
    d2d::rules::Fighter fighter = d2d::rules::simple_fighter(2, 4, 100000, 0);
    fighter.item_slow = 30;
    d2d::rules::Target target;
    target.hit_points = target.max_hp = 100;
    d2d::rules::Rng rng{ 9 };
    const auto blow = d2d::rules::player_blow(fighter, target, 30, rng, {});
    assert(blow.hit && blow.slow == 30);
    d2d::rules::Swing smite;
    smite.smite = true;
    fighter.smite_lo = fighter.smite_hi = 3;
    const auto smitten = d2d::rules::player_blow(fighter, target, 30, rng, smite);
    assert(smitten.slow == 30);   // a Smite is a melee record (event 5) too
    std::puts("OK: a landed blow carries its fighter's item_slow");
}

int land_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 0, "Slower", false, true, rng);
    character.name = "Slower";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto* actor = world->actor;
    auto& monsters = world->fight.monsters;
    const auto add = [&](int id, d2d::rules::Boss boss) -> std::size_t {
        auto& added = monsters.emplace_back();
        added.type = data.monsters.row("zombie1");
        added.id = id;
        added.boss = boss;
        added.stats.hit_points = added.hit_points = 5000;
        added.unit.x = actor->player.x + 2; added.unit.y = actor->player.y;
        return monsters.size() - 1;
    };
    const auto plain = add(90000, d2d::rules::Boss::none), champion = add(90001, d2d::rules::Boss::champion), missed = add(90002, d2d::rules::Boss::none);
    d2d::rules::Blow blow{ .hit = true };
    blow.damage = 1;
    blow.slow = 80;
    world->fight.land(plain, blow, true, 100000, world->fight.player_foe_id());
    world->fight.land(champion, blow, true, 100000, world->fight.player_foe_id());
    d2d::rules::Blow miss;
    miss.slow = 80;
    world->fight.land(missed, miss, true, 100000, world->fight.player_foe_id());
    const std::uint32_t until = 100000 + std::uint32_t(d2d::rules::kClaySlowFrames) * 40;
    assert(monsters[plain].slow_speed == 80 && monsters[plain].slow_until == until);
    assert(monsters[champion].slow_speed == 50 && monsters[champion].slow_until == until);
    assert(monsters[missed].slow_until == 0);
    std::puts("OK: a landed blow with item_slow 80 slows a zombie by 80 and a champion by 50; a miss slows nothing");
    return 0;
}

}  // namespace

int main() try {
    blow_check();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP (the land check): no d2data.mpq (D2_MPQ_DIR)"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP (the land check): needs the 1.14d data (D2_PATCH_INSTALLER)"); return 0; }
    return land_check(*data);
} catch (const std::exception& error) {
    std::printf("test_gear_slow: %s\n", error.what());
    return 1;
}
