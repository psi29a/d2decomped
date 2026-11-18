// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Reanimated Horde rises again (MonUMod 34 ai_after_death, reanimatedhorde1
// BaseId 0x1b4 by FUN_005b21b0): hook 1 (FUN_005a3840) queues event 7 as it
// dies, hook 2 (FUN_005a3910) puts it up in S1 at full life with the AI scratch
// + 1, twice at most. The REAL Fight::boss_events on synthetic game data.

#include <ai.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData data;
    d2d::rules::MonType horde;
    horde.id = "reanimatedhorde1"; horde.base = 0x1b4; horde.level = { 4, 4, 4 }; horde.corpse_sel = true; horde.velocity = 6; horde.killable = true;
    for (auto& per_difficulty : horde.diff) { per_difficulty.aip[0] = 2; per_difficulty.aip[7] = 100; per_difficulty.min_hp = per_difficulty.max_hp = 100; }
    data.monsters.types = { horde };
    data.mon_npc.resize(1);
    data.monsters.lvl.resize(10);
    for (auto& row : data.monsters.lvl) row.hit_points = { 50, 50, 50 };
    World world(&data, -1, -1);
    auto& fight = world.fight;
    Level floor;                                                  // an open 30 x 30 tile map for the spot test
    floor.ds1 = d2d::ds1::Map(30, 30, 1, 1);
    floor.walk.assign(150 * 150, 0);
    fight.mon_level = &floor;
    d2d::rules::Rng rng{ 3 };
    auto make = [&] {
        fight.monsters.clear();
        auto monster = make_monster(data, 0, 20.f, 20.f, rng, 0, true);
        monster.align = 0;
        fight.monsters.push_back(std::move(monster));
        fight.monsters[0].fx_mode = fight.monsters[0].mode;
    };
    auto kill = [&](std::uint32_t now_ms) {
        auto& monster = fight.monsters[0];
        monster.hit_points = 0;
        set_mode(data, monster, "DT", now_ms);
        fight.boss_events({}, now_ms);
    };
    make();
    assert(fight.monsters[0].targetable());                      // alive it is a target: only the rise (S1) takes that away
    fight.monsters[0].next_act = 1500; fight.monsters[0].thought_ms = 1000;   // a think pending at death
    kill(1000);
    assert(fight.monsters[0].rise_at == 1000 + 21 * 40);          // aip8 100 %: event 7 at 1 + aip1 x 10 frames
    set_mode(data, fight.monsters[0], "DD", 1500);
    fight.boss_events({}, 1000 + 20 * 40);
    assert(!fight.monsters[0].alive());                           // not yet due
    fight.boss_events({}, 1000 + 21 * 40);
    assert(fight.monsters[0].alive() && fight.monsters[0].mode == "S1" && fight.monsters[0].ai_state == 1);
    assert(fight.monsters[0].hit_points == fight.monsters[0].stats.hit_points && !fight.monsters[0].targetable());
    // Its think is the rise's 0x33 frames on (FUN_005a3910's event 2), not the one pending at death.
    assert(fight.monsters[0].next_act == 1000 + 21 * 40 + 51 * 40 && fight.monsters[0].think_pending());
    // S1 ending on that very tick keeps the think's deadline (mode_ends files none while a rise's think is pending).
    {
        auto& risen = fight.monsters[0];
        risen.mode_until = risen.next_act;
        const auto due = risen.next_act;
        std::vector<Foe> far_foe(1);
        far_foe[0].x = 900.f; far_foe[0].y = 900.f; far_foe[0].alive = true;
        std::vector<Missile> missiles;
        d2d::rules::Rng step_rng{ 5 };
        monster_update(data, floor, risen, far_foe, step_rng, due, 0.04f, Crowd{}, missiles);
        assert(risen.mode == "NU" && risen.next_act == due);
    }
    make();                                                       // a unique, or a good one, never rises
    fight.monsters[0].align = 2;
    kill(1000);
    assert(fight.monsters[0].rise_at == 0);
    make();                                                       // two rises at most: the scratch at 2 stays down
    fight.monsters[0].ai_state = 2;
    kill(1000);
    set_mode(data, fight.monsters[0], "DD", 1500);
    fight.boss_events({}, 1000 + 21 * 40);
    assert(!fight.monsters[0].alive());
    make();                                                       // its spot taken (a player on it): event 7 again, aip1 x 10 + 1 frames on
    kill(1000);
    set_mode(data, fight.monsters[0], "DD", 1500);
    Foe player;
    player.x = 20.f; player.y = 20.f; player.alive = true;
    fight.boss_events(std::span<Foe>(&player, 1), 1000 + 21 * 40);
    assert(!fight.monsters[0].alive() && fight.monsters[0].rise_at == 1000 + 42 * 40);
    // The plus is the corpse's own subtile and its four neighbours (FUN_0064d870, pattern 1): a monster two
    // subtiles east stands outside it, one on a neighbour blocks, and a retry looks again.
    make();
    kill(1000);
    set_mode(data, fight.monsters[0], "DD", 1500);
    {
        auto bystander = make_monster(data, 0, 20.4f, 20.f, rng, 0, true);
        bystander.align = 0;
        fight.monsters.push_back(std::move(bystander));
        fight.monsters[1].fx_mode = fight.monsters[1].mode;
    }
    fight.boss_events({}, 1000 + 21 * 40);
    assert(fight.monsters[0].mode == "S1");
    make();
    kill(1000);
    set_mode(data, fight.monsters[0], "DD", 1500);
    {
        auto bystander = make_monster(data, 0, 20.2f, 20.f, rng, 0, true);
        bystander.align = 0;
        fight.monsters.push_back(std::move(bystander));
        fight.monsters[1].fx_mode = fight.monsters[1].mode;
    }
    fight.boss_events({}, 1000 + 21 * 40);
    assert(!fight.monsters[0].alive() && fight.monsters[0].rise_at == 1000 + 42 * 40);
    fight.monsters.pop_back();                                   // it walks off: the retry finds the spot free
    fight.boss_events({}, 1000 + 42 * 40);
    assert(fight.monsters[0].mode == "S1");
    // A death between two boss_events passes (a missile's kill, then a portal trip past the death's end) still
    // queues the rise: hook 1 runs as the mode changes, and the corpse in DD is raised when event 7 is delivered.
    make();
    set_mode(data, fight.monsters[0], "DT", 1000);
    assert(fight.monsters[0].rise_at == 1000 + 21 * 40);
    set_mode(data, fight.monsters[0], "DD", 1500);
    fight.boss_events({}, 5000);
    assert(fight.monsters[0].mode == "S1" && fight.monsters[0].ai_state == 1);
    std::puts("test_ai_after_death_rise: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ai_after_death_rise: %s\n", error.what());
    return 1;
}
