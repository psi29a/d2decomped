// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Frozen Anya clicked (OperateFn 67, FUN_0058abc0): without Malah's potion
// ("ice ", FUN_00558110 at 0x58abec) she speaks her 20131 to the clicker
// (0x58abfb: FUN_005456a0, S->C 0x27, the speech box, as the Moldy Tome's);
// with it she thaws and says nothing. No game data.
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <variant>

using namespace d2d::game;

int main() try {
    using d2d::rules::PrisonQuest;
    GameData data;
    Level river;
    river.id = 114;                                             // the Frozen River
    river.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    river.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    auto& fana = river.npcs.emplace_back();
    fana.root = "objects"; fana.mode = "NU"; fana.object_id = PrisonQuest::kFana;
    fana.x = 4; fana.y = 4;
    World world(&data, -1, -1);
    world.level = &river;
    world.prison.state = 3;
    const auto spoke = [&] { return std::ranges::any_of(world.actor->events, [](const auto& event) { return std::holds_alternative<ev::Speech>(event); }); };

    assert(world.act5_operate(0, 1000));
    assert(world.actor->events.size() == 1);
    const auto* told = std::get_if<ev::Speech>(&world.actor->events[0]);
    assert(told && told->npc == 0 && told->string == PrisonQuest::kNoPotion);
    std::puts("OK: frozen Anya without the potion speaks her 20131 to the clicker");

    world.actor->events.clear();
    d2d::d2s::Item potion;
    potion.code = PrisonQuest::kPotionCode;
    potion.id = 7;
    world.actor->character.items.push_back(potion);
    world.prison.potions = 1;
    assert(world.act5_operate(0, 2000));
    assert(!spoke());
    assert(world.prison.anya == 1 && d2d::rules::quest::bit(world.quests(), PrisonQuest::kQuest, 1));
    assert(world.actor->character.items.empty());              // the potion poured
    std::puts("OK: with the potion she thaws and says nothing");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_frozen_anya_speech: %s\n", error.what());
    return 1;
}
