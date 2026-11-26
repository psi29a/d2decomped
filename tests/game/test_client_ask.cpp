// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A joined game's ask before a skill goes to the host (FUN_004610c0 -> FUN_004d9fc0): skilldelay is the host's
// state 121 on our player, the client's own guard (min(delay, 12) frames from its do, FUN_004c6680 at 0x4c68b8)
// turns any status 0 into 8, and AttackNoMana asks Attack in its place. Synthetic rows: no game data.
#include <character.hpp>
#include <client_ask.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <skills.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.skill(0, "Attack");
    auto& bolt = fixture.skill(36, "Fire Bolt");
    bolt.mana = 2;
    auto& meteor = fixture.skill(56, "Meteor");
    meteor.mana = 2;
    meteor.delay = d2d::rules::compile_calc("30", fixture.data.skills.names);
    auto& bash = fixture.skill(126, "Bash");
    bash.attack_no_mana = true;
    bash.mana = 50;
    Level field;
    field.ds1 = d2d::ds1::Map(10, 10, 1, 1);
    field.walk.assign(std::size_t(50 * 50), 0);
    World world(&fixture.data, -1, -1);
    world.level = &field;
    world.fight.skill_level = [](int) { return 1; };
    auto& lead = *world.players.front();
    world.fight.act_as(lead);
    lead.character.stats.values[d2d::d2s::kMana] = 10 << 8;

    const ChargedSkill none;
    auto ask = client_ask(world.fight, 56, none, false, 0, 1000);
    assert(ask.skill == 56 && ask.status == 0);
    ask = client_ask(world.fight, 56, none, true, 0, 1000);           // the host's skilldelay: a skill with a delay waits
    assert(ask.status == 8);
    assert(client_ask(world.fight, 36, none, true, 0, 1000).status == 0);   // one without goes
    lead.skill_delay = 5000;                                         // the World's own stands aside
    assert(client_ask(world.fight, 56, none, false, 0, 1000).status == 0 && lead.skill_delay == 5000);
    // The client's guard: any 0 an 8, Fire Bolt's too, till it runs out.
    assert(client_ask(world.fight, 36, none, false, 1200, 1000).status == 8);
    assert(client_ask(world.fight, 36, none, false, 1200, 1200).status == 0);
    // Bash with too little mana: Attack in its place; under the guard Attack's 0 is an 8 too.
    ask = client_ask(world.fight, 126, none, false, 0, 1000);
    assert(ask.skill == 0 && ask.status == 0);
    assert(client_ask(world.fight, 126, none, false, 1200, 1000).status == 8);
    // Meteor's 30 frames: the guard is 12 of them; a 5-frame delay its own 5.
    assert(client_skill_guard(1000 + 30 * 40, 1000) == 1000 + 12 * 40);
    assert(client_skill_guard(1000 + 5 * 40, 1000) == 1000 + 5 * 40);
    assert(client_skill_guard(900, 1000) == 0);
    std::puts("OK: the client asks with the host's skilldelay, its own 12-frame guard and AttackNoMana (FUN_004610c0, FUN_004c6680)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
