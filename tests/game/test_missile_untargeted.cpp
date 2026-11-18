// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A missile with no target point is aimed with FUN_0064fc60 (missile_aim)
// at the start plus (1, 1) (FUN_0059fa30). The room follows (path flag 1,
// FUN_006492f0) only when that point lies outside the first room. A re-aim in
// flight aims from the fine position at the new point's centre.
#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missiles.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

constexpr std::uint32_t centre_of(int subtile) { return (std::uint32_t(subtile) << 16) + 0x8000; }

Missile shot_at(float x, float y) {
    Missile missile;
    missile.x = x;
    missile.y = y;
    missile.velocity_x = 5.f;   // due east: the float ratio is not this aim
    missile.velocity_y = 0.f;
    return missile;
}

}  // namespace

int main() try {
    Level level;
    level.ds1 = d2d::ds1::Map(8, 8, 1, 1);   // 40 x 40 subtiles, nothing blocked
    level.walk.assign(40 * 40, 0);
    const auto whole = [](float, float) { return std::vector<std::array<int, 4>>{ { 0, 0, 40, 40 } }; };
    const auto start_only = [](float, float) { return std::vector<std::array<int, 4>>{ { 20, 20, 1, 1 } }; };
    const auto [aim_x, aim_y] = d2d::rules::missile_aim(centre_of(20), centre_of(20), centre_of(21), centre_of(21));

    Missile untargeted = shot_at(4.1f, 4.1f);   // subtile (20, 20); no target point
    assert(missile_fly(level, untargeted, whole));
    assert(untargeted.flight.aim_x == aim_x && untargeted.flight.aim_y == aim_y);
    assert(!untargeted.follow);                 // (21, 21) is inside the 40x40 room

    Missile on_edge = shot_at(4.1f, 4.1f);
    static_cast<void>(missile_fly(level, on_edge, start_only));   // only the launch's aim and follow flag are checked
    assert(on_edge.flight.aim_x == aim_x && on_edge.flight.aim_y == aim_y);
    assert(on_edge.follow);                     // (21, 21) lies outside the start subtile

    Missile inside = shot_at(4.1f, 4.1f);
    inside.to = { 4.5f, 4.5f };                 // subtile (22, 22), inside the room
    assert(missile_fly(level, inside, whole));
    const auto [inside_x, inside_y] = d2d::rules::missile_aim(centre_of(20), centre_of(20), centre_of(22), centre_of(22));
    assert(inside.flight.aim_x == inside_x && inside.flight.aim_y == inside_y);
    assert(!inside.follow);

    Missile outside = shot_at(4.1f, 4.1f);
    outside.to = { 10.f, 4.1f };                // subtile (50, 20), past the room
    static_cast<void>(missile_fly(level, outside, whole));   // only the launch's follow flag is checked
    assert(outside.follow);

    std::puts("OK: no target point aims with FUN_0064fc60, and the room follows only outside");

    // A re-aim in flight (FUN_00649970 -> FUN_006492f0): from the fine position kept, at the point's centre.
    Missile guided = shot_at(4.1f, 4.1f);
    guided.to = { 6.1f, 4.1f };
    assert(missile_fly(level, guided, whole));
    const auto flown_x = guided.flight.x, flown_y = guided.flight.y;
    guided.retarget = std::pair{ 4.1f, 6.1f };   // subtile (20, 30)
    assert(missile_fly(level, guided, whole));
    const auto [turned_x, turned_y] = d2d::rules::missile_aim(flown_x, flown_y, centre_of(20), centre_of(30));
    assert(guided.flight.aim_x == turned_x && guided.flight.aim_y == turned_y && !guided.retarget);
    std::puts("OK: a re-aim turns at the point from where it flies");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_untargeted: %s\n", error.what());
    return 1;
}
