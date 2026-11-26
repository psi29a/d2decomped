// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Act 5's records send their log (S->C 0x5d as ev::QuestNews, World::act5_update) at each FUN_00544300
// flag-1 site to every player FUN_00586c80 .. FUN_0058d770 passes. No game data.
#include <gamedata.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <variant>

using namespace d2d::game;
using namespace d2d::rules;

namespace {

// Whether `player` received `quest` from the last World::act5_update.
bool sent(const World::Player& player, int quest) {
    return std::ranges::any_of(player.events, [quest](const Event& event) {
        const auto* news = std::get_if<ev::QuestNews>(&event);
        return news && news->quest == quest;
    });
}

}  // namespace

// Checks all six Act 5 callbacks fan out by each player's bits rather than the current actor's.
int main() {
    GameData data;
    World world(&data, -1, -1);
    auto& second = world.add_player();
    auto& first = *world.actor;
    constexpr std::array quests{ SiegeQuest::kQuest, RescueQuest::kQuest, PrisonQuest::kQuest,
                                 BetrayalQuest::kQuest, RiteQuest::kQuest, EveQuest::kQuest };
    for (const int quest : quests) quest::set(world.quests(), quest, 0);

    // These are representative flag-1 sites for the six callbacks: Shenk's first think, a cage InitFn, Fana's
    // InitFn, Nihlathak's timer, Qual-Kehk's talk, and the a5q5 chain.
    world.siege.shenk_seen();                                      // 0x587941
    assert(world.rescue.cage(1, 1) == 0);                          // 0x588707
    world.prison.fana_seen();                                      // 0x58aa3d / 0x58aa9a
    world.betrayal.log_timer();                                    // 0x58b78d
    world.rite.state = 1;
    world.rite.said(world.quests(), RiteQuest::kQualKehk, RiteQuest::kTold, 20, 0); // 0x58c2f2
    world.eve.opened(world.quests(), true);                         // 0x58e3ce
    world.act5_update(0);
    for (const int quest : quests) {
        assert(!sent(first, quest));
        assert(sent(second, quest));
    }

    first.events.clear(); second.events.clear();
    auto& second_bits = second.character.header.quests[0];
    for (const int quest : quests) {
        quest::set(second_bits, quest, 15);
        quest::set(second_bits, quest, 14);
    }
    world.siege.news = world.rescue.news = world.prison.news = world.betrayal.news = world.rite.news = world.eve.news = true;
    world.act5_update(0);
    for (const int quest : quests) {
        assert(!sent(first, quest));
        assert(sent(second, quest));
    }

    std::puts("OK: act 5 quest-log sends fan out through their stock callbacks");
    return 0;
}
