// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005e7e20 asks FUN_005ddc30 for a hostile monster and starts A1 through
// FUN_005ddf90 when that target's unit distance is below 25.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <npc.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

// Checks Flavie's FUN_005e7e20 attack gate through the world NPC driver.
int main() try {
    constexpr std::uint32_t kNow = 1000;
    const GameData game_data;
    Level level;
    Npc flavie;
    flavie.town_ai = TownAi::navi;
    flavie.x = flavie.y = 10.f;
    flavie.shape.size = 2;
    level.npcs.push_back(flavie);

    auto states = npc_start(level);
    states[0].filed = true;
    states[0].wait_until = kNow;

    Monster enemy;
    enemy.unit.x = 12.f;
    enemy.unit.y = 10.f;
    enemy.unit.shape.size = 2;
    std::array<Monster, 1> monsters{ enemy };
    npc_patrol(game_data, level, states, { -1, -1, -1 }, kNow, 0.04f, {}, {}, false, monsters);
    assert(states[0].mode == "A1");

    states = npc_start(level);
    states[0].filed = true;
    states[0].wait_until = kNow;
    monsters[0].align = 2;
    npc_patrol(game_data, level, states, { -1, -1, -1 }, kNow, 0.04f, {}, {}, false, monsters);
    assert(states[0].mode.empty());
    assert(states[0].wait_until == kNow + 50 * 40);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_flavie_attack: %s\n", error.what());
    return 1;
}
