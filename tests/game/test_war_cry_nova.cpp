// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A war cry from a monster (do 68, FUN_005d83e0: Shout): on its action
// frame the nova of its srvmissilea (FUN_0056d400: 64 on rules::kNovaRing,
// drawn, hitting nothing) and its aurastate on itself and an ally in reach.
// Made-up rows (test_data.hpp): the rule, not the game's numbers.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& data = fixture.data;
    auto& overseer = fixture.monster(0, "overseer1");
    overseer.velocity = 4;
    overseer.walk_base = 256;   // its S1 plays at its walk rate (a moving mode under class 0x19a: FUN_006214a0)
    fixture.mon_level(1, 100, 10, 10, 10);
    auto& cry = fixture.skill(1, "Shout");
    cry.srvdofunc = d2d::rules::ServerDoFunction::kWarCry;
    cry.srvmissilea = "shout";
    cry.aurastate = "shout";
    fixture.state("shout");
    auto& shout = data.missiles["shout"];
    shout.name = "shout"; shout.vel = 10; shout.range = 10;
    Level here;
    here.id = d2d::rules::level_ids::kBloodyFoothills;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    here.walk.assign(80 * 80, 0);
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> player{};
    player[0].x = 10.1f; player[0].y = 8.1f;
    d2d::rules::Rng rng(7);
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(data, 0, 8.1f, 8.1f, spawn, 0), make_monster(data, 0, 8.5f, 8.1f, spawn, 0) };
    auto& caller = pack[0];
    // Its S1: 10 frames, the action at 5 (the COF's timing, set as the cache would hold it).
    std::string key = caller.npc.root + "/" + caller.npc.code + "/S1/" + std::string(cof_weapon_class(caller.npc, "S1"));
    for (const auto& component : caller.npc.comp) key += "/" + component;
    data.npc_timings[key] = GameData::AnimTiming{ .speed = 256, .frames = 10, .action = 5 };
    caller.aware = true;
    caller.chase = 0;
    caller.skill = 1;
    constexpr std::uint32_t kStart = 40000;
    set_mode(data, caller, "S1", kStart);
    caller.next_act = caller.mode_until + 40;                    // no think till the cast's done
    for (std::uint32_t now = kStart; now < caller.mode_until && shots.empty(); now += 40)
        monster_update(data, here, caller, player, rng, now, 0.04f, crowd, shots, pack);
    std::printf("Shout: %zu missiles\n", shots.size());
    assert(shots.size() == 64);
    for (const auto& shot : shots) assert(shot.visual_only && shot.info == &data.missiles.at("shout"));
    assert(std::ranges::any_of(pack[1].states, [&](const Monster::SkillState& held) { return held.skill == 1; }));
    std::puts("OK: a monster's Shout sends its nova (64, drawn) and puts its state on an ally");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_war_cry_nova: %s\n", error.what());
    return 1;
}
