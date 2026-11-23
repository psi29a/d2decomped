// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Harrogath's main gate and the pen door (OperateFn 61, FUN_00582080, #689):
// 500 ms after its last change, a shut gate (mode 0) opens (1, its footprint
// out: FUN_00623830), then its event 1 (FUN_00581490, FrameCnt1 + 1 frames)
// takes it to 2; an open one shuts (0, its footprint back: FUN_00620a70).
// No game data.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <object_ids.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    using d2d::rules::operate_fn::kGate;
    assert(World::is_door(kGate) && World::operable(kGate));
    GameData data;
    Level town;
    town.id = 109;
    town.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    town.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    town.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    auto& gate = town.npcs.emplace_back();
    gate.root = "objects"; gate.mode = "NU"; gate.operate_fn = kGate;
    gate.x = 4; gate.y = 4; gate.size_x = 3; gate.size_y = 3;
    gate.collision = 0b001;                                     // shut blocks; OP and ON don't
    gate.frames[1] = 1; gate.op_frames = 12; gate.mode2 = true;
    World world(&data, -1, -1);
    world.level = &town;
    world.actor->player.x = 4; world.actor->player.y = 5;
    set_footprint(town, gate, true);
    const auto blocked = [&] { return town.blocked(4.1f, 4.1f, gate.stamp); };
    assert(blocked());
    world.operate(0, 1000);
    assert(world.doors.at({ &town, 0 }).mode == 1 && !blocked());
    const std::uint32_t on_ms = 1000 + std::uint32_t(object_op_ticks(gate)) * kTickMs;
    assert(object_op_ticks(gate) == 13);
    world.slime_doors_on(on_ms - 1);
    assert(world.doors.at({ &town, 0 }).mode == 1);
    world.slime_doors_on(on_ms);
    assert(world.doors.at({ &town, 0 }).mode == 2);
    world.operate(0, on_ms + 100);                              // under 500 ms since its last change
    assert(world.doors.at({ &town, 0 }).mode == 2);
    world.operate(0, on_ms + 500);
    assert(world.doors.at({ &town, 0 }).mode == 0 && blocked());
    std::puts("OK: the gate opens, goes ON by its event 1, and shuts again 500 ms on");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_town_gate: %s\n", error.what());
    return 1;
}
