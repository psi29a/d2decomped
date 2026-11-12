// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The true tomb's way down to Duriel is object 100 (SJ), not a town portal:
// FUN_0059d870 makes it with FUN_00555230(2, 100, ..) and sets mode 1, its
// 16-frame opening (0x59d986), or mode 2 when the staff went in in an
// earlier game (data +0xc, 0x59d976).

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    // objects.txt row 100: SJ, OP 16 frames at FrameDelta 200 with Sync; the
    // client's OP counter reaches frame 15 on its 20th tick (FUN_00624390).
    const auto& look = data->duriel_portal;
    assert(look.code == "SJ" && look.object_id == 100);
    assert(look.frames[1] == 16 && look.frame_delta[1] == 200 && look.sync);
    assert(object_op_ticks(look) == 20);

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
    assert(world.tombs.true_tomb > 0);
    // Into the true tomb by the orifice (objects.txt 152), so its room comes up.
    const Level* tomb = data->level(world.tombs.true_tomb);
    assert(tomb);
    const auto orifice = std::ranges::find_if(tomb->units, [](const auto& unit) { return unit.type == d2d::rules::unit_type::kObject && unit.id == 152; });
    assert(orifice != tomb->units.end());
    world.arrive(tomb, (float(orifice->x) + 0.5f) / 5, (float(orifice->y) + 0.5f) / 5, "a test");
    constexpr std::uint32_t kNow = 100000;
    world.tombs.later = false;
    world.durielportal_open(kNow);
    assert(world.portal[6].level && world.portal[6].level->id == world.tombs.true_tomb);
    assert(world.portal[6].born == kNow);                       // mode 1: it opens from now
    world.portal[6] = {};
    world.tombs.later = true;
    world.durielportal_open(kNow);
    assert(world.portal[6].born == kNow - 20 * kTickMs);         // mode 2: open already
    std::printf("OK: the way down is object 100, opening in this game, open from an earlier one\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
