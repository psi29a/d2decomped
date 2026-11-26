// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Act 4's records send their log (S->C 0x5d as ev::QuestNews, World::act4_update) at each FUN_00544300
// flag-1 site to every player FUN_005b3940, FUN_005b4e40 or FUN_005b59c0 passes. No game data.
#include <gamedata.hpp>
#include <quests.hpp>
#include <quests_act4.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <variant>

using namespace d2d::game;
using namespace d2d::rules;

namespace {

// Whether `player` received `quest` from the last World::act4_update.
bool sent(const World::Player& player, int quest) {
    return std::ranges::any_of(player.events, [quest](const Event& event) {
        const auto* news = std::get_if<ev::QuestNews>(&event);
        return news && news->quest == quest;
    });
}

}  // namespace

// Checks the flag-1 fanout and a representative flag-0 site from the Act 4 FUN_00544300 call-site table.
int main() {
    GameData data;
    World world(&data, -1, -1);
    auto& second = world.add_player();
    auto& first = *world.actor;

    // Tyrael's close (0x5b41fc) is flag 1. FUN_005b3940 skips the done player but sends to the clean one.
    quest::set(world.quests(), IzualQuest::kQuest, 0);
    world.izual.heard = true;
    world.act4_talk_closed(IzualQuest::kTyrael);
    world.act4_update(0);
    assert(!sent(first, IzualQuest::kQuest) && sent(second, IzualQuest::kQuest));

    first.events.clear(); second.events.clear();
    // Bit 13 reopens the callback gate despite bit 0; bit 15 alone shuts the other player out.
    quest::set(world.quests(), IzualQuest::kQuest, 13);
    auto& second_bits = second.character.header.quests[0];
    quest::set(second_bits, HellforgeQuest::kQuest, 15);
    world.hellforge.forge_seen();                              // 0x5b5a6f, flag 1
    world.act4_update(0);
    assert(sent(first, HellforgeQuest::kQuest) && !sent(second, HellforgeQuest::kQuest));

    first.events.clear(); second.events.clear();
    quest::set(second_bits, TerrorQuest::kQuest, 14);
    quest::set(second_bits, TerrorQuest::kQuest, 15);
    world.terror.heard = true;
    world.act4_talk_closed(TerrorQuest::kTyrael);              // 0x5b4fc0, flag 1
    world.act4_update(0);
    assert(sent(first, TerrorQuest::kQuest) && sent(second, TerrorQuest::kQuest));

    first.events.clear(); second.events.clear();
    quest::set(world.quests(), IzualQuest::kQuest, 0, false);
    quest::set(world.quests(), IzualQuest::kQuest, 13, false);
    world.izual.state = 2; world.izual.log = 0; world.izual.news = false;
    world.izual.enter(world.quests(), act4_detail::kFortress); // 0x5b41bf, flag 0
    world.act4_update(0);
    assert(world.izual.log == 1 && !sent(first, IzualQuest::kQuest) && !sent(second, IzualQuest::kQuest));

    std::puts("OK: act 4 quest-log sends fan out through their stock callbacks");
    return 0;
}
