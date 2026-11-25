// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Baal Taunt's two controls (hit 58 FUN_005acdf0, hit 59 FUN_005acf20) lay their sub missiles only
// when FUN_00552fd0 finds the missile's owner; one with none (owner -1) lays nothing at its end.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missile_flight.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

// Runs one update frame at the missile's end and returns how many missiles there are after it.
std::size_t ends_with(const Level& level, Missile control) {
    std::array<Foe, 1> foes{};
    foes[0].x = foes[0].y = 30.f;
    d2d::rules::Rng rng(7);
    std::vector<Missile> missiles{ std::move(control) };
    missiles_update(level, missiles, foes, rng, 400, [](float, float) { return std::vector<std::array<int, 4>>{}; },
                    [](Missile&) { return false; }, [](int) { return true; }, [](Missile&) {});
    return missiles.size();
}

}  // namespace

// Runs the hit-spawn cases: lightning and poison ends, each with an owner (FUN_00552fd0).
int main() try {
    Level open;
    open.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    open.walk.assign(40 * 40, 0);
    GameData::MissileInfo bolt;
    bolt.range = 10;
    bolt.vel = 10;
    bolt.param1 = bolt.param2 = 1;
    for (const int func : { 58, 59 }) {
        GameData::MissileInfo control;
        control.hit_func = func;
        control.hit_par1 = 2;
        control.hit_sub_info = &bolt;
        Missile end;
        end.info = &control;
        end.x = 4.1f; end.y = 4.3f;
        end.born = 0; end.dies = 400;
        assert(end.owner == -1);
        assert(ends_with(open, end) == 0);          // no owner: it expires and lays nothing
        end.owner = 7;
        assert(ends_with(open, end) >= 1);          // an owner: the sub missile(s)
    }
    std::puts("OK: hit 58 and 59 lay their sub missiles only with an owner (FUN_00552fd0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_hit_owner: %s\n", error.what());
    return 1;
}
