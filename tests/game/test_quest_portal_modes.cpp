// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The quest portals objects.txt has unselectable in mode 0 (#684, #690):
// Tyrael's portal to Harrogath (566, InitFn 78, FUN_005b5840: a4q2 +0x40),
// Baal's portals (563 / 569, InitFn 75, FUN_0058e670: a5q6 +0x94) and the
// last portal (565, InitFn 77, FUN_0058e710: a5q6 +0x9c) start in mode 1,
// the game's first of each, then 2 (FUN_0058e500 sets both a5q6 fields to 1).
// Act 5's quest objects take a click (World::quest_object). No game data.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <object_ids.hpp>
#include <quests_act5.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    using namespace d2d::rules::operate_fn;
    for (const int operate_fn : { kAncientsAltar, kAncientsDoor, kFrozenAnya, kBaalsPortal, kHomePortal }) assert(World::quest_object(operate_fn));
    GameData data;
    Level throne;
    throne.id = d2d::rules::EveQuest::kThrone;
    throne.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    throne.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    for (const int object_id : { d2d::rules::object_ids::kLastPortal, d2d::rules::object_ids::kLastPortal, d2d::rules::EveQuest::kChamberPortal,
                                 d2d::rules::EveQuest::kThronePortal, d2d::rules::EveQuest::kLastPortal, d2d::rules::EveQuest::kLastPortal }) {
        auto& object = throne.npcs.emplace_back();
        object.root = "objects"; object.mode = "NU"; object.object_id = object_id;
    }
    World world(&data, -1, -1);
    world.level = &throne;
    world.init_new_objects();
    const auto mode = [&](int index) { return world.doors.at({ &throne, index }).mode; };
    assert(mode(0) == 1 && mode(1) == 2);                       // Tyrael's: the game's first opens, a later one's open
    assert(mode(2) == 1 && mode(3) == 2);                       // Baal's portals share a5q6 +0x94
    assert(mode(4) == 1 && mode(5) == 2);                       // the last portal, +0x9c
    std::puts("OK: the quest portals start in mode 1, the game's first, then 2; Act 5's quest objects take a click");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_portal_modes: %s\n", error.what());
    return 1;
}
