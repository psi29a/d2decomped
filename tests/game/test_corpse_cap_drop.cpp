// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A death with 16 corpses already makes none (FUN_0057f700 -> FUN_0057f540):
// the item in hand and every worn item go onto the ground, and the old
// corpses stay.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
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
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

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
    const auto worn = [&] {
        return std::ranges::count(world.actor->character.items, d2d::d2s::item_location::kEquipped, &d2d::d2s::Item::location);
    };
    const auto starting_worn = worn();
    assert(starting_worn > 0);                                                 // a new Sorceress wears her staff

    // 15 corpses: the death still makes one, with her gear on it.
    world.actor->corpses.resize(15);
    world.make_corpse(0);
    assert(world.actor->corpses.size() == 16 && world.actor->corpses.back().items.size() == std::size_t(starting_worn));
    assert(worn() == 0);

    // 16: none. The held item and the worn ones land on the ground instead.
    world.actor->corpses.back().items.clear();
    world.actor->character.items.push_back({ .code = "cap", .location = d2d::d2s::item_location::kEquipped, .slot = 1 });
    world.actor->held = d2d::d2s::Item{ .code = "hp1", .location = d2d::d2s::item_location::kCursor };
    const auto on_ground = world.loot.ground.size();
    world.make_corpse(0);
    assert(world.actor->corpses.size() == 16);
    assert(!world.actor->held && worn() == 0);
    assert(world.loot.ground.size() == on_ground + 2);
    std::printf("OK: 16 corpses: no new one, the gear on the ground\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
