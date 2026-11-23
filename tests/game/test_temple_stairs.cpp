// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The stairs whose warp has an empty LvlWarp click box (#687): a Kurast
// temple's door (OperateFn 47, FUN_00581f60) shut opens (mode 1), its event 1
// (FUN_00581490) at FrameCnt1 + 1 frames takes it to 2, and then a click
// sends the player through the warp tile of its room (FUN_005550b0). The
// sewers' stairs (44, FUN_005b84e0) do the same only open (mode 2), and
// only through a warp in their own room. No game data.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <object_ids.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>

using namespace d2d::game;

namespace {

Level& open_level(GameData& data, int id) {
    auto& made = *(data.levels[id] = std::make_unique<Level>());
    made.id = id;
    made.ds1 = d2d::ds1::Map(16, 8, 1, 1);
    made.walk.assign(std::size_t(16 * 5) * std::size_t(8 * 5), 0);
    made.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    made.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    return made;
}

}  // namespace

int main() try {
    using namespace d2d::rules::operate_fn;
    assert(World::operable(kStairs) && World::operable(kDuranceStairs) && World::operable(kSewerStairs));
    GameData data;
    auto& bazaar = open_level(data, 80);
    auto& temple = open_level(data, 94);
    bazaar.warps.push_back({ .x = 4, .y = 4, .destination = 94, .exit_x = 0, .exit_y = 0, .unit_x = 4.5f, .unit_y = 4.5f });
    temple.warps.push_back({ .x = 4, .y = 4, .destination = 80, .exit_x = 0, .exit_y = 0, .unit_x = 4.5f, .unit_y = 4.5f });
    bazaar.npcs.resize(2);
    auto& door = bazaar.npcs[0];
    door.root = "objects"; door.mode = "NU"; door.operate_fn = kStairs;
    door.x = 4; door.y = 4; door.frames[1] = 1; door.op_frames = 9; door.mode2 = true;
    auto& stairs = bazaar.npcs[1];                              // the sewers' way, shut, its warp a room away
    stairs.root = "objects"; stairs.mode = "NU"; stairs.operate_fn = kSewerStairs;
    stairs.x = 12; stairs.y = 4;
    World world(&data, -1, -1);
    world.level = &bazaar;
    world.actor->player.x = 4; world.actor->player.y = 5;
    world.operate(0, 1000);
    assert(world.doors.at({ &bazaar, 0 }).mode == 1 && world.level == &bazaar);
    const std::uint32_t open_ms = 1000 + std::uint32_t(object_op_ticks(door)) * kTickMs;
    assert(object_op_ticks(door) == 10);
    world.slime_doors_on(open_ms);
    assert(world.doors.at({ &bazaar, 0 }).mode == 2);
    world.operate(1, open_ms);                                  // shut: nothing
    assert(world.level == &bazaar);
    world.set_door_mode(1, 2, open_ms);
    world.operate(1, open_ms);                                  // open, but no warp in its own room
    assert(world.level == &bazaar);
    world.operate(0, open_ms);                                  // the door open: through its room's warp
    assert(world.level == &temple);
    std::puts("OK: a temple door opens, goes ON, and leads through its room's warp; the sewers' stairs only through their own room's");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_temple_stairs: %s\n", error.what());
    return 1;
}
