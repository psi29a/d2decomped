// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The client's skill searches (FUN_00467880: SearchEnemyNear, SearchEnemyXY,
// SearchOpenXY) run only while the hand's skill can go now (FUN_004d9fc0 ->
// FUN_00647960 status 0). The World leaves each player's left / right status
// in the View (World::hand_statuses, View::hand_status), which the client's
// use_hand reads. Synthetic rows: no game data.
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.skill(0, "Attack");
    fixture.skill(8, "Inner Sight").aura = true;
    Level field;
    field.ds1 = d2d::ds1::Map(10, 10, 1, 1);
    field.walk.assign(std::size_t(50 * 50), 0);
    World world(&fixture.data, -1, -1);
    world.level = &field;
    world.fight.skill_level = [](int) { return 1; };
    auto& lead = *world.players.front();
    lead.character.header.left_skill = 0;
    lead.character.header.right_skill = 8;
    world.hand_statuses(1000);
    assert(lead.hand_status[0] == 0);                                // Attack goes: the searches run
    assert(lead.hand_status[1] == 6);                                // an aura: they don't
    assert(world.view().hand_status == lead.hand_status);
    lead.character.header.right_skill = 300;                         // no row
    world.hand_statuses(1040);
    assert(world.view().hand_status[1] == 3);
    std::puts("OK: the hands' skill status reaches the client's searches (FUN_004d9fc0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hand_status: %s\n", error.what());
    return 1;
}
