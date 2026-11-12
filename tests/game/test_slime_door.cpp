// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Maggot Lair's slime doors (objects 229 / 230, OperateFn 29,
// FUN_005821a0): a click operates one; shut (mode 0) it goes to OP
// (FUN_00624690(unit, 1) at 0x5821b8) and its footprint lifts
// (FUN_00623830, 0x5821c1); in any other mode it does nothing, so it never
// shuts again. With Mode2 its event 1 (0x58223c, FUN_00581490) takes it ON
// (object_op_ticks), with ON's footprint. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

// Does any cell of the door's footprint carry its stamp's bits (set_footprint's rect)?
bool stamped(const Level& level, const Npc& door) {
    const int width = level.ds1.width() * 5, height = level.ds1.height() * 5;
    const int center_x = int(door.x * 5), center_y = int(door.y * 5);
    for (int row = center_y - door.size_y / 2; row < center_y - door.size_y / 2 + door.size_y; ++row)
        for (int column = center_x - door.size_x / 2; column < center_x - door.size_x / 2 + door.size_x; ++column)
            if (column >= 0 && row >= 0 && column < width && row < height && (level.walk[std::size_t(row) * std::size_t(width) + std::size_t(column)] & door.stamp)) return true;
    return false;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    using d2d::rules::operate_fn::kSlimeDoor;
    assert(World::is_door(kSlimeDoor) && World::operable(kSlimeDoor));

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);

    int opened = 0;
    for (int level_id = d2d::rules::level_ids::kMaggotLairLevel3 - 2; level_id <= d2d::rules::level_ids::kMaggotLairLevel3; ++level_id) {
        const auto* lair = data->level(level_id);
        if (!lair) continue;
        populate_level(*data, world.fight.spawning, *lair);
        world.level = lair;
        world.npc_states = npc_start(*lair);
        for (std::size_t index = 0; index < lair->npcs.size(); ++index) {
            const auto& door = lair->npcs[index];
            if (door.operate_fn != kSlimeDoor) continue;
            // Shut, its footprint stamped (HasCollision0); in OP, lifted (HasCollision1).
            set_footprint(*lair, door, (door.collision & 1) != 0);
            assert(stamped(*lair, door));
            world.operate(int(index), 1000);
            const auto state = world.doors.find({ lair, int(index) });
            assert(state != world.doors.end() && state->second.mode == 1);
            assert(!stamped(*lair, door));
            world.operate(int(index), 5000);                 // past the 500 a plain door waits: still OP, never shut
            assert(world.doors.at({ lair, int(index) }).mode == 1 && !stamped(*lair, door));
            // Event 1 (0x58223c, FUN_00581490): with Mode2, ON at object_op_ticks after the click
            // (FrameCnt1 + 1, or the client's OP counter's end if sooner).
            assert(door.mode2 && object_op_ticks(door) > 0);
            const std::uint32_t on_ms = 1000 + std::uint32_t(object_op_ticks(door)) * kTickMs;
            world.slime_doors_on(on_ms - 1);
            assert(world.doors.at({ lair, int(index) }).mode == 1);
            world.slime_doors_on(on_ms);
            const auto& turned = world.doors.at({ lair, int(index) });
            assert(turned.mode == 2 && turned.when == on_ms);
            assert(stamped(*lair, door) == ((door.collision >> 2 & 1) != 0));   // HasCollision2's footprint
            world.operate(int(index), on_ms + 5000);         // ON: a click does nothing
            assert(world.doors.at({ lair, int(index) }).mode == 2);
            std::printf("slime door %zu (object %d): FrameCnt1 %d, ON after %d ticks, HasCollision2 %d\n", index, door.object_id, door.op_frames,
                        object_op_ticks(door), door.collision >> 2 & 1);
            ++opened;
        }
    }
    if (!opened) { std::printf("SKIP: no slime door in the Maggot Lair\n"); return D2D_TEST_SKIPPED; }
    std::printf("OK: %d slime doors open once, go ON and stay open\n", opened);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_slime_door: %s\n", error.what());
    return 1;
}
