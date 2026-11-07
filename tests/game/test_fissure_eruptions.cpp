// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The fissures (object 399, InitFn 58 FUN_0054fdb0): their first eruption
// (event 8) the init's object-seed step % 250 + 25 frames on; each eruption
// (FUN_00581250) in NU / ON steps the seed for the next one, goes OP and ON
// FrameCnt1 + 1 frames later (event 1, FUN_00581490); one that comes while
// still OP queues nothing more (bugs.md #52). Needs the data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto* river = data->level(d2d::rules::level_ids::kRiverOfFlame);
    if (!river) { std::printf("SKIP: no River of Flame\n"); return D2D_TEST_SKIPPED; }
    if (!data->builder) { std::printf("SKIP: no level builder\n"); return D2D_TEST_SKIPPED; }
    Level one = *river;                                  // a fissure alone (fh, objects.txt 399)
    one.npcs.clear();
    auto object_seed = d2d::rules::Rng(99);
    auto roll = object_seed;
    add_object(*data, data->builder->objects, data->builder->obj_row, one, 399, 10, 10, object_seed);
    assert(one.npcs.size() == 1 && one.npcs[0].erupts == int(roll.next() % 250) + 25);   // InitFn 58's step

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);

    one.npcs[0].erupts = 30;                             // its first eruption 30 frames on
    world.level = &one;
    world.npc_states = npc_start(one);
    const int on_frames = one.npcs[0].op_frames + 1;
    assert(on_frames == 50);                             // fh: FrameCnt1 49
    const std::uint32_t start = 1000 * kTickMs;
    auto seed = world.fight.spawning.objects;
    std::uint32_t now = start;
    for (; now < start + 30 * kTickMs; now += kTickMs) {
        world.fissure_frame(now);
        assert(world.npc_states[0].mode != "OP");
    }
    world.fissure_frame(now);                            // frame 30: event 8
    assert(world.npc_states[0].mode == "OP" && world.npc_states[0].mode_ms == now);
    const auto& erupting = world.fissures.at({ &one, 0 });
    assert(erupting.next_ms == now + std::uint32_t(seed.next() % 250 + 25) * kTickMs);
    assert(erupting.on_ms == now + std::uint32_t(on_frames) * kTickMs);
    const bool falls_in_op = erupting.next_ms < erupting.on_ms;   // a roll under 25 (bugs.md #52)
    world.fissure_frame(erupting.on_ms - kTickMs);
    assert(world.npc_states[0].mode == "OP");
    world.fissure_frame(erupting.on_ms);                 // event 1: ON
    assert(world.npc_states[0].mode == "ON" && erupting.stopped == falls_in_op);
    std::printf("OK: a fissure erupts at its init roll, OP then ON %d frames on\n", on_frames);

    // An eruption due while still OP: no more, ever.
    world.fissures.clear();
    world.npc_states = npc_start(one);
    world.fissure_frame(start);
    auto& stuck = world.fissures.at({ &one, 0 });
    stuck.next_ms = start;
    world.fissure_frame(start);                          // erupts: OP
    assert(world.npc_states[0].mode == "OP");
    stuck.next_ms = start + 10 * kTickMs;                // the next roll under 50 - 25
    world.fissure_frame(start + 10 * kTickMs);
    assert(stuck.stopped && world.npc_states[0].mode == "OP");
    world.fissure_frame(stuck.on_ms);
    assert(world.npc_states[0].mode == "ON");
    world.fissure_frame(stuck.on_ms + 1000 * kTickMs);
    assert(world.npc_states[0].mode == "ON" && stuck.stopped);
    std::printf("OK: one that falls in OP stops for good (bugs.md #52)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_fissure_eruptions: %s\n", error.what());
    return 1;
}
