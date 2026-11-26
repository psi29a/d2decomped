// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Blackened Temple's and the Guardian's told and later states go by Lam Esen's game bit 13 (word 0x11),
// not their own: Ormus's 594 / 628 (FUN_00544760(game, 0x11, 0xd) at 0x5bb26f, 0x5bc603) and leaving the
// docks (0x5bb1ce, 0x5bc45e) through World::act3_said / act3_enter. No game data.
#include <gamedata.hpp>
#include <level.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>

using namespace d2d::game;
using d2d::rules::BlackenedTempleQuest;
using d2d::rules::GuardianQuest;
using d2d::rules::LamEsenQuest;

namespace {

constexpr int kDocks = 75, kBazaar = 80;

// A level with an id and nothing else: the act 3 hooks read only its id.
Level& add_level(GameData& data, int id) {
    auto& made = *(data.levels[id] = std::make_unique<Level>());
    made.id = id;
    return made;
}

// Ormus tells both quests with one game bit set (a word's bit 13): the states each takes.
std::pair<int, int> told_with(GameData& data, int word) {
    World world(&data, -1, -1);
    world.level = data.levels[kDocks].get();
    d2d::rules::quest::set(world.game_bits, word, 13);
    world.act3_said(d2d::rules::monster_ids::kOrmus, BlackenedTempleQuest::kTells);
    world.act3_said(d2d::rules::monster_ids::kOrmus, GuardianQuest::kTells);
    return { world.temple.state, world.guardian.state };
}

// Both told (state 3, 2), then out of the docks with one game bit set: the states each takes.
std::pair<int, int> left_with(GameData& data, int word) {
    World world(&data, -1, -1);
    d2d::rules::quest::set(world.game_bits, word, 13);
    world.temple.state = world.guardian.state = 3;
    world.level = data.levels[kBazaar].get();
    world.act3_enter(*data.levels[kDocks]);
    return { world.temple.state, world.guardian.state };
}

}  // namespace

// Checks a3q5 / a3q6 read word 0x11's game bit 13 where game.exe does.
int main() try {
    GameData data;
    add_level(data, kDocks);
    add_level(data, kBazaar);
    assert(told_with(data, LamEsenQuest::kQuest) == std::pair(2, 2));
    assert(told_with(data, BlackenedTempleQuest::kQuest) == std::pair(3, 3));   // their own bits aren't read
    assert(told_with(data, GuardianQuest::kQuest) == std::pair(3, 3));
    assert(left_with(data, LamEsenQuest::kQuest) == std::pair(4, 4));
    assert(left_with(data, BlackenedTempleQuest::kQuest) == std::pair(5, 5));
    assert(left_with(data, GuardianQuest::kQuest) == std::pair(5, 5));
    std::puts("OK: the Temple's and the Guardian's states go by Lam Esen's game bit 13");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_act3_tome_done: %s\n", error.what());
    return 1;
}
