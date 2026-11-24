// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A barricade door's death (FUN_0057ccb0, 0x57cdf1: MonStats 0x1b0 / 0x1b1)
// opens the blocker made on its spot (object 571 / 572): FUN_0057cc30 finds
// that object at the dead door's subtile, mode 2 (FUN_00624690), footprint
// out (FUN_00623830). Another monster's death, or a blocker elsewhere, is
// left alone. No game data.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData data;
    data.mon_bin.assign(0x1b2, 0);
    data.mon_bin[0x1b0] = 30;                                   // barricadedoor1's row
    data.mon_bin[0x1b1] = 31;                                   // barricadedoor2's
    Level field;
    field.id = 111;
    field.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    field.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    const auto blocker = [&](int object_id, float x, float y) {
        auto& object = field.npcs.emplace_back();
        object.root = "objects"; object.mode = "NU"; object.object_id = object_id;
        object.x = x; object.y = y; object.size_x = 2; object.size_y = 2;
        object.collision = 0b001;                               // NU blocks, OP / ON don't
        set_footprint(field, object, true);
    };
    blocker(571, 3.1f, 3.1f);                                   // under the door that dies
    blocker(571, 6.1f, 6.1f);                                   // another door's
    blocker(572, 3.1f, 3.1f);                                   // the other kind, same spot
    World world(&data, -1, -1);
    world.level = &field;
    const auto mode = [&](int index) { const auto found = world.doors.find({ &field, index }); return found == world.doors.end() ? 0 : found->second.mode; };

    world.barricade_broken({ .type = 7, .x = 3.1f, .y = 3.1f, .level = 30 }, 1000);   // not a barricade door
    assert(mode(0) == 0 && mode(1) == 0 && mode(2) == 0);
    assert(field.blocked(3.0f, 3.0f, field.npcs[0].stamp));
    world.barricade_broken({ .type = 30, .x = 3.1f, .y = 3.1f, .level = 30 }, 2000);
    assert(mode(0) == 2 && mode(1) == 0 && mode(2) == 0);
    std::puts("OK: a barricade door's death opens its own blocker only");
    world.barricade_broken({ .type = 31, .x = 3.1f, .y = 3.1f, .level = 30 }, 3000);
    assert(mode(2) == 2 && mode(1) == 0);
    assert(!field.blocked(3.0f, 3.0f, field.npcs[0].stamp));
    std::puts("OK: the other kind opens 572, and the arch is free");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_barricade_door: %s\n", error.what());
    return 1;
}
