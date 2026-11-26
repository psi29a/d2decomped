// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Blackened Temple's record log (+0xb, read by quest_log) moves at its FUN_00544300 sites: Ormus's
// talk's close log 2 (LAB_005bb0c0, armed entering the Causeway at state 0; 0x5bb0ee), each Council member
// made (FUN_005bb550: log 1 / 3 at 0x5bb5ec, state 4 / 5), out of the docks (0x5bb1c0), the last death
// (0x5bbdb3) and Cain's (0x5bb35c); the marks (LAB_005badc0) set bits 2 and 3. No game data.
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>

using namespace d2d::game;
using Temple = d2d::rules::BlackenedTempleQuest;
namespace level_ids = d2d::rules::level_ids;

namespace {

constexpr int kDocks = 75, kBazaar = 80;

Level& add_level(GameData& data, int id) {
    auto& made = *(data.levels[id] = std::make_unique<Level>());
    made.id = id;
    return made;
}

// A Council superunique (SuperUniques row `super`) made in Travincal, as Fight::add_monster lists it.
void make_council(World& world, int unit_id, int super) {
    Monster member;
    member.id = unit_id;
    member.type = 1;
    member.super = super;
    world.fight.monsters.push_back(member);
    world.fight.made_bosses.push_back({ unit_id, member.type, level_ids::kTravincal });
}

}  // namespace

// Checks the Temple's log, state and marks at each of its sites.
int main() try {
    GameData data;
    add_level(data, kDocks);
    add_level(data, kBazaar);
    add_level(data, level_ids::kKurastCauseway);
    World world(&data, -1, -1);
    world.golden_bird.armed = false;                            // no Golden Bird carrier to bind
    world.level = data.levels[level_ids::kKurastCauseway].get();
    world.act3_enter(*data.levels[kBazaar]);                    // into the Causeway: state 1, the hook armed
    assert(world.temple.state == 1 && world.temple.log == 0);
    world.act3_said(d2d::rules::monster_ids::kOrmus, Temple::kTells);
    assert(world.temple.state == 3 && world.temple.log == 0);
    world.act3_talk_closed(d2d::rules::monster_ids::kCainAct3);  // not his
    assert(world.temple.log == 0);
    world.act3_talk_closed(d2d::rules::monster_ids::kOrmus);
    assert(world.temple.log == 2 && d2d::rules::quest::bit(world.quests(), Temple::kQuest, 2));
    world.temple.log = 1;
    world.act3_talk_closed(d2d::rules::monster_ids::kOrmus);     // once
    assert(world.temple.log == 1);
    world.temple.log = 2;
    make_council(world, 40, 26);                                  // Ismail: log 3 (told), state 5, everyone's bit 3
    make_council(world, 41, 5);                                   // not the Council
    world.act3_bind_new_bosses();
    assert(world.temple.log == 3 && world.temple.state == 5 && d2d::rules::quest::bit(world.quests(), Temple::kQuest, 3));
    {   // Out of the docks, told, log under 3: log 2 (flag 0), state 5.
        World docks(&data, -1, -1);
        docks.temple.state = 3;
        docks.level = data.levels[kBazaar].get();
        docks.act3_enter(*data.levels[kDocks]);
        assert(docks.temple.log == 2 && docks.temple.state == 5);
    }
    {   // A Council member made before Ormus told it: log 1.
        World early(&data, -1, -1);
        early.golden_bird.armed = false;
        make_council(early, 7, 29);
        early.act3_bind_new_bosses();
        assert(early.temple.log == 1 && early.temple.state == 5);
    }
    {   // The last death: log 4, 0xd with the orb broken; Cain's 626 with bit 13: 0xd.
        d2d::rules::QuestBits bits{}, game_bits{};
        Temple temple;
        temple.council = 1;
        assert(temple.council_died(game_bits).last && temple.state == 6 && temple.log == 4);
        Temple broken;
        broken.council = 1;
        broken.orb_broken = true;
        assert(broken.council_died(game_bits).last && broken.state == 7 && broken.log == 0xd);
        Temple cain;
        cain.state = 6;
        d2d::rules::quest::set(bits, Temple::kQuest, 4);
        d2d::rules::quest::set(bits, Temple::kQuest, 13);
        cain.said(bits, game_bits, d2d::rules::monster_ids::kCainAct3, Temple::kDone, false, 1);
        assert(cain.state == 7 && cain.log == 0xd);
    }
    std::puts("OK: the Blackened Temple's log moves at its FUN_00544300 sites");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_temple_log: %s\n", error.what());
    return 1;
}
