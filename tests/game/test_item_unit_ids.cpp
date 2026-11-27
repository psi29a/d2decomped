// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Item unit ids through the real World: one counter per game for items
// carried or lying (game +0xa0, FUN_00552ee0 from FUN_00555230), starting
// at 1 in each new game (FUN_00530930 zeroes +0x90 .. +0xa4); a pick-up
// (FUN_00563560) and a drop (FUN_00563c00) move the same unit, so the item
// keeps its id. Needs the data; SKIPs without it.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

// Every item unit id in the game, carried or lying: none twice.
std::vector<int> all_ids(const World& world) {
    std::vector<int> ids;
    for (const auto& item : world.actor->character.items) ids.push_back(item.id);
    if (world.actor->held) ids.push_back(world.actor->held->id);
    for (const auto& lying : world.loot.ground) ids.push_back(lying.id);
    return ids;
}

bool unique(const std::vector<int>& ids) { return std::set<int>(ids.begin(), ids.end()).size() == ids.size(); }

bool carries(const World& world, int id) {
    return std::ranges::any_of(world.actor->character.items, [&](const d2d::d2s::Item& item) { return item.id == id; });
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Numbered", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Numbered";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    const int carried = int(character.items.size());
    assert(carried > 1);

    World world(&*data, -1, -1);
    world.enter(character);
    auto* actor = world.actor;
    // The save's items are the game's first: 1 .. N.
    std::vector<int> ids = all_ids(world);
    std::ranges::sort(ids);
    for (int index = 0; index < carried; ++index) assert(ids[std::size_t(index)] == index + 1);
    assert(world.loot.next_id == carried + 1);

    // What drops next counts on from them: one counter, gold too.
    std::uint32_t now = 1000 * kTickMs;
    assert(world.loot.put({ .code = "hp1" }, actor->player.x, actor->player.y, 1, world.fight.spawning.game, now));
    const int potion = world.loot.ground.back().id;
    assert(potion == carried + 1 && world.loot.ground.back().item.id == potion);
    assert(world.loot.put({ .code = "gld", .gold = 5 }, actor->player.x + 3, actor->player.y, 1, world.fight.spawning.game, now));
    assert(world.loot.ground.back().id == carried + 2);
    assert(unique(all_ids(world)));

    // Picked up, the potion keeps its id; no new one is taken.
    world.apply(cmd::Pickup{ potion }, now);
    for (int tick = 0; tick < 50 && world.loot.index_of(potion) >= 0; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    }
    assert(world.loot.index_of(potion) < 0 && carries(world, potion));
    assert(world.loot.next_id == carried + 3);

    // Dropped, a carried item keeps its id on the ground.
    const auto stored = std::ranges::find_if(actor->character.items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kStored; });
    assert(stored != actor->character.items.end());
    const int dropped = stored->id;
    world.apply(cmd::ToCursor{ dropped }, now);
    assert(actor->held && actor->held->id == dropped);
    world.apply(cmd::Drop{ dropped }, now);
    world.tick({}, now + kTickMs, now);
    now += kTickMs;
    assert(!carries(world, dropped) && world.loot.index_of(dropped) >= 0);
    assert(world.loot.ground[std::size_t(world.loot.index_of(dropped))].item.id == dropped);
    assert(world.loot.next_id == carried + 3);
    assert(unique(all_ids(world)));

    // A new game numbers from 1 again, whatever ids the character came with.
    Character again = std::move(character);
    for (std::size_t index = 0; index < again.items.size(); ++index) again.items[index].id = 500 + int(index);
    world.enter(again);
    ids = all_ids(world);
    std::ranges::sort(ids);
    for (int index = 0; index < carried; ++index) assert(ids[std::size_t(index)] == index + 1);
    assert(world.loot.next_id == carried + 1);

    std::printf("ok: one item counter per game, ids kept through a pick-up and a drop\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_item_unit_ids: %s\n", error.what());
    return 1;
}
