// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A missile the player's side makes takes its unit seed (+0x20) off the game
// seed as it's made (Fight::launch), not at the frame's first missile tick:
// two made one after the other take two steps in that order. No game data.

#include <ai.hpp>
#include <cues.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    game_data.skills.rows.resize(2);
    game_data.skills.rows[1].id = 1;
    auto& row = game_data.missiles["bolt"];
    row.range = 10;
    const Level* level = nullptr;
    d2d::rules::Rng rng{ 1 };
    Cues cues;
    Loot loot{ &game_data, level, rng, cues };
    Fight fight{ &game_data, level, rng, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    fight.spawning.game = d2d::rules::Rng{ 0x1234 };
    auto expect = fight.spawning.game;
    const auto first_seed = expect.next(), second_seed = expect.next();
    const auto& first = fight.launch(row, game_data.skills.rows[1], 1, 0, 0, 1.f, 0.f, row.range, 7000);
    assert(first.seed && first.seed->low == d2d::rules::Rng{ first_seed }.low);
    const auto& second = fight.launch(row, game_data.skills.rows[1], 1, 0, 0, 1.f, 0.f, row.range, 7000);
    assert(second.seed && second.seed->low == d2d::rules::Rng{ second_seed }.low);
    assert(fight.spawning.game.low == expect.low && fight.spawning.game.high == expect.high);
    std::puts("OK: a launched missile is seeded off the game seed as it's made");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_launch_seed: %s\n", error.what());
    return 1;
}
