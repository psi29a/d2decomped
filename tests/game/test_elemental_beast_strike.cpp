// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// ElementalBeast's state-1 melee think (FUN_005f6b70) starts S1 and files
// its type-0 action event for the next frame, ahead of the mode's action frame.
// Made-up rows (test_data.hpp); the delay is captured by tools/emu/elemental_beast_strike.py.

#include <ai.hpp>
#include <ai/units.hpp>
#include <cof_anim.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <test_data.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace d2d::game;

namespace {
constexpr int kStrikeDelayFrames =
#include "elemental_beast_strike_cases.inc"
;
}  // namespace

// Run FUN_005f6b70's port through the real think dispatcher and S1 hit path.
int main() try {
    d2d::test::TestData fixture;
    auto& data = fixture.data;
    auto& type = fixture.monster(0, "firebeast");
    data.mon_bin.resize(0xfb, 1);                                  // class 0xfa is not this synthetic row
    type.ai_name = "ElementalBeast";
    type.hit_class = 1;
    type.killable = true;
    type.melee_rng = 1;
    type.size = 1;

    d2d::rules::Rng spawn{ 7 };
    std::vector<Monster> pack{ make_monster(data, 0, 10.1f, 10.1f, spawn, 0, false) };
    auto& beast = pack[0];
    beast.ai_state = 1;
    beast.stats.hit_points = beast.hit_points = 100;
    beast.stats.no_hit_roll = true;                                // timing test: make the synthetic blow land
    beast.stats.s1_min = beast.stats.s1_max = 1;
    beast.stats.to_hit_s1 = 100000;

    std::string timing_key = beast.npc.root + "/" + beast.npc.code + "/S1/" + std::string(cof_weapon_class(beast.npc, "S1"));
    for (const auto& component : beast.npc.comp) timing_key += "/" + component;
    data.npc_timings[timing_key] = GameData::AnimTiming{ .speed = 256, .frames = 10, .action = 5 };

    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    arena.walk.assign(80 * 80, 0);
    std::array<Foe, 1> foes{};
    foes[0].x = 10.3f;
    foes[0].y = 10.1f;
    foes[0].alive = true;
    const Crowd crowd;
    std::vector<Missile> missiles;
    d2d::rules::Rng rng{ 3 };
    constexpr std::uint32_t kStart = 40'000;
    beast.thought_ms = kStart - 40;
    beast.next_act = kStart;

    monster_update(data, arena, beast, foes, rng, kStart, 0.04f, crowd, missiles, pack);
    assert(beast.mode == "S1" && beast.ai_state == 2);
    assert(beast.scheduled_action_ms == kStart + std::uint32_t(kStrikeDelayFrames) * 40);
    assert(foes[0].hits == 0);
    assert(beast.scheduled_anim_position == 0u && beast.scheduled_anim_ms == kStart);  // S1's pose starts now

    const auto strike_at = *beast.scheduled_action_ms;
    monster_update(data, arena, beast, foes, rng, strike_at, 0.04f, crowd, missiles, pack);
    assert(beast.struck && foes[0].hits == 1);
    assert(strike_at < kStart + data.npc_timing(beast.npc, "S1").action_ms());
    std::puts("OK: ElementalBeast's S1 hits on its explicit next-frame event");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_elemental_beast_strike: %s\n", error.what());
    return 1;
}
