// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_004cadb0 reads the unit's room (FUN_004cace0), then each room of its
// near list (FUN_00619790). A neighbour level's tiles are shifted onto the
// unit's cells. A flagless first tile keeps the level default.
#include <sound_ids.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

namespace snd = d2d::rules::sound_ids;

struct Tile { int x, y; unsigned material; };
struct Step {
    const std::vector<Tile>* tiles = nullptr;
    int shift_x = 0;
    int shift_y = 0;
};

int main() try {
    const std::vector<Tile> sand{ { 1, 2, 0x40 } };
    const std::vector<Tile> wood{ { 1, 2, 0x80 } };
    const std::vector<Tile> blank{ { 1, 2, 0 } };
    const std::vector<Tile> other{ { 9, 9, 0x80 } };
    const std::vector<Tile> shifted{ { 5, 6, 0x40 } };
    // List order would take the sand room. The unit's room is asked first.
    const std::vector<std::vector<Tile>> listed{ sand, wood };
    assert(snd::first_floor_material(listed, 1, 2, 3) == 4);
    const Step unit_then_neighbour[] = { { &wood, 0, 0 }, { &sand, 0, 0 } };
    assert(snd::floor_material_near(unit_then_neighbour, 1, 2, 3) == 6);
    // No tile in the unit's room: the near room's tile.
    const Step miss_then_sand[] = { { &other, 0, 0 }, { &sand, 0, 0 } };
    assert(snd::floor_material_near(miss_then_sand, 1, 2, 3) == 4);
    // A flagless tile in the unit's room stops the walk at the level default.
    const Step blank_then_sand[] = { { &blank, 0, 0 }, { &sand, 0, 0 } };
    assert(snd::floor_material_near(blank_then_sand, 1, 2, 3) == 3);
    // A neighbour level's tile (5, 6) is the unit's cell (1, 2) after the shift.
    const Step neighbour[] = { { &other, 0, 0 }, { &shifted, 4, 4 } };
    assert(snd::floor_material_near(neighbour, 1, 2, 3) == 4);
    const Step skipped[] = { { nullptr, 0, 0 }, { &sand, 0, 0 } };
    assert(snd::floor_material_near(skipped, 1, 2, 3) == 4);
    const Step none[] = { { &other, 0, 0 } };
    assert(snd::floor_material_near(none, 1, 2, 0) == 1 && snd::floor_material_near(none, 1, 2, 7) == 1);
    std::printf("OK: a footstep reads the unit's room, then the near list\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_floor_material: %s\n", error.what());
    return 1;
}
