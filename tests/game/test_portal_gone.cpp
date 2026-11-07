// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A town portal pair closes (FUN_00535430, a new read's old pair): both ends
// are freed (FUN_00555600), so a walk queued to either loses its target
// (LAB_00481697); a walk to another pair's portal is kept. And which
// object each of the game's own portals is.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

// The game's permanent pairs are object 60 (PP, red): the Cairn Stones'
// (FUN_005a9930), the Sanctuary's (FUN_0056d130), Anya's (FUN_0058af90) and
// the Cow Portal (FUN_00594140); [6] is object 100; the rest town portals (59).
static_assert(World::portal_object(2) == 60 && World::portal_object(5) == 60 && World::portal_object(8) == 60 && World::portal_object(11) == 60);
static_assert(World::portal_object(6) == 100 && World::portal_object(0) == 59 && World::portal_object(World::kGibbetPortal) == 59 && World::portal_object(14) == 59);

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    assert(data->town_portal.code == "TP" && data->red_portal.code == "PP");

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
    auto& walker = *world.actor;
    const std::size_t pair = walker.portal_pair;
    for (const std::size_t end : { pair, pair + 1 }) {
        walker.take_portal = int(end);
        world.close_portal_pair(pair);
        assert(walker.take_portal == -1);
    }
    walker.take_portal = int(pair + 2);
    world.close_portal_pair(pair);
    assert(walker.take_portal == int(pair + 2));
    std::printf("OK: a closed pair drops a walk to either end, keeps another pair's\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
