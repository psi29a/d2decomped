// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Guardian's (a3q6) FUN_00544300 sites move its log (+0xb) and send it (flag 1, FUN_005bc210): Ormus's
// talk's close log 2 (LAB_005bc370, armed when it opens; 0x5bc39e), into the Durance 1 log 3 (0x5bc4e2),
// into its level 3 log 4 (0x5bc53b); out of the docks log 2 (flag 0, 0x5bc450) and the report's 0xd (flag 0,
// 0x5bc6b1) send nothing. The marks (LAB_005bc020) set bits 2 .. 9. No game data.
#include <gamedata.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>
#include <variant>

using namespace d2d::game;
using Guardian = d2d::rules::GuardianQuest;
namespace level_ids = d2d::rules::level_ids;

namespace {

constexpr int kDocks = 75, kBazaar = 80, kTemple = 98;

Level& add_level(GameData& data, int id) {
    auto& made = *(data.levels[id] = std::make_unique<Level>());
    made.id = id;
    return made;
}

// Into `into` from `from`, as a level change runs the hooks.
void go(World& world, GameData& data, int from, int into) {
    world.level = data.levels[into].get();
    world.act3_enter(*data.levels[from]);
}

// Whether the Guardian's news was raised since the last look, taken as World::act3_update does.
bool took(World& world) { return std::exchange(world.guardian.news, false); }

}  // namespace

// Checks the Guardian's log, sends and marks at each of its sites.
int main() try {
    GameData data;
    for (const int id : { kDocks, kBazaar, kTemple, level_ids::kDuranceOfHateLevel1, level_ids::kDuranceOfHateLevel3 }) add_level(data, id);
    const auto has = [](World& world, int bit) { return d2d::rules::quest::bit(world.quests(), Guardian::kQuest, bit); };
    World world(&data, -1, -1);
    go(world, data, kBazaar, kTemple);                          // a level past 97: state 1, the hook armed
    assert(world.guardian.state == 1 && !took(world));
    world.level = data.levels[kDocks].get();
    world.act3_said(d2d::rules::monster_ids::kOrmus, Guardian::kTells);
    assert(world.guardian.state == 3 && world.guardian.log == 0);
    world.act3_talk_closed(d2d::rules::monster_ids::kOrmus);
    assert(world.guardian.log == 2 && took(world));
    world.act3_talk_closed(d2d::rules::monster_ids::kOrmus);   // once
    assert(!took(world));
    go(world, data, kDocks, kBazaar);                          // out of the docks: log 2 kept (flag 0), state 5, bit 7
    assert(world.guardian.state == 5 && world.guardian.log == 2 && !took(world) && has(world, 7));
    go(world, data, kTemple, level_ids::kDuranceOfHateLevel1);
    assert(world.guardian.log == 3 && took(world) && has(world, 8));
    go(world, data, level_ids::kDuranceOfHateLevel1, level_ids::kDuranceOfHateLevel1);   // log 3 already
    assert(!took(world));
    go(world, data, level_ids::kDuranceOfHateLevel1, level_ids::kDuranceOfHateLevel3);
    assert(world.guardian.log == 4 && took(world) && has(world, 9));
    go(world, data, level_ids::kDuranceOfHateLevel1, level_ids::kDuranceOfHateLevel3);   // log 4 already
    assert(!took(world));
    // The news reaches the player through World::act3_update.
    world.guardian.news = true;
    world.actor->events.clear();
    world.act3_update(0);
    assert(std::ranges::any_of(world.actor->events, [](const auto& event) {
        const auto* news = std::get_if<ev::QuestNews>(&event);
        return news && news->quest == Guardian::kQuest;
    }));
    {   // Told at state 2 (Lam Esen's game bit 13), out of the docks: bit 4 (state 4, log 2).
        World early(&data, -1, -1);
        d2d::rules::quest::set(early.game_bits, d2d::rules::LamEsenQuest::kQuest, 13);
        early.guardian.state = 2;
        go(early, data, kDocks, kBazaar);
        assert(early.guardian.state == 4 && early.guardian.log == 2 && has(early, 4));
    }
    {   // The report with bit 13: log 0xd, no send.
        d2d::rules::QuestBits bits{};
        d2d::rules::quest::set(bits, Guardian::kQuest, 0xb);
        d2d::rules::quest::set(bits, Guardian::kQuest, 13);
        Guardian guardian;
        guardian.said(bits, d2d::rules::monster_ids::kOrmus, Guardian::kReportFirst + 1, false);
        assert(guardian.state == 7 && guardian.log == 0xd && !guardian.news);
    }
    std::puts("OK: the Guardian's log moves and is sent at its FUN_00544300 sites");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_guardian_log: %s\n", error.what());
    return 1;
}
