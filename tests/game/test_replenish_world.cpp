// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A replenishing item in the world (stat 0xfd, timer 3: FUN_00562d30),
// with the game's MPQs (D2_MPQ_DIR, as test_game; it skips without):
// - a character's items are queued on entering (FUN_00558cb0 ->
//   FUN_00558580), 2500 / rate + 1 frames to the first point, 125 after;
// - a shot's ammunition queues it (FUN_0056c310 -> FUN_00558580);
// - a magic stack thrown out breaks and stays (FUN_00580030), and its first
//   one back mends it (FUN_00562c40 -> FUN_0055f900);
// - an item on the floor keeps its timer.
// The rules themselves are checked against game.exe in tests/test_replenish.cpp.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

namespace filesystem = std::filesystem;
using namespace d2d::game;
using d2d::d2s::Item;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const filesystem::path data_dir = mpq_dir ? filesystem::path(mpq_dir) : filesystem::path(home ? home : "") / "Workspace/private/diablo2";
    if (!filesystem::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? filesystem::path(patch) : filesystem::path{}, 0x1234);
    assert(data && data->town.id == 1);

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Thrower", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Thrower";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.stats.values[d2d::d2s::kDex] = 100;            // throwing knives ask 21
    character.stats.values[d2d::d2s::kLevel] = 50;
    std::erase_if(character.items, [](const Item& item) {
        return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
    Item knives;
    knives.code = "tkf";
    knives.location = d2d::d2s::item_location::kEquipped;
    knives.slot = d2d::d2s::body_location::kRightArm;
    knives.identified = true;
    knives.quality = 4;
    const int most = d2d::rules::stack_most(data->rules, knives);
    assert(most > 10);
    knives.quantity = most - 5;
    knives.durability = knives.max_durability = 6;
    knives.props = { { .stat = d2d::rules::kReplenishQuantity, .param = 0, .value = 25 } };
    character.items.push_back(knives);
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    auto& items = world.actor->character.items;
    const auto held = [&]() -> Item& { return *std::ranges::find(items, "tkf", &Item::code); };
    const auto index = [&] { return std::size_t(&held() - items.data()); };
    std::uint32_t now_ms = 1000 * kTickMs;
    // The frames World::tick takes until `done`.
    const auto frames_until = [&](const auto& done) {
        int frames = 0;
        for (; !done() && frames < 1000; ++frames) { world.tick({}, now_ms + kTickMs, now_ms); now_ms += kTickMs; }
        return frames;
    };

    assert(held().wielded && d2d::rules::item_usable(held()));
    assert(held().replenish_in == 2500 / 25 + 1);                       // queued on entering
    world.tick({}, now_ms + kTickMs, now_ms); now_ms += kTickMs;        // the day clock's first tick may step no frame
    world.tick({}, now_ms + kTickMs, now_ms); now_ms += kTickMs;
    const int left = held().replenish_in;
    assert(left == 100 || left == 99);
    assert(frames_until([&] { return held().quantity > most - 5; }) == left);
    assert(held().quantity == most - 4 && held().replenish_in == 125);
    std::printf("OK: the first one back %d frames after entering, the next in 125\n", 2500 / 25 + 1);

    // Full: the timer runs once more and stops; a shot queues it again.
    held().quantity = most;
    (void)frames_until([&] { return held().replenish_in == 0; });
    assert(held().quantity == most);
    world.fight.spend_ammo(int(index()));
    assert(held().quantity == most - 1 && held().replenish_in == 2500 / 25 + 1);
    std::printf("OK: a shot queues the timer\n");

    // The last one thrown: a magic stack breaks and stays; the first one back mends it and it is wielded again.
    held().quantity = 1; held().replenish_in = 0;
    world.fight.spend_ammo(int(index()));
    assert(held().quantity == 0 && d2d::rules::broken(held()) && !held().wielded && held().replenish_in == 2500 / 25 + 1);
    assert(world.fight.ammo_index() < 0);
    assert(frames_until([&] { return held().quantity > 0; }) == 2500 / 25 + 1);
    assert(held().quantity == 1 && !d2d::rules::broken(held()) && held().wielded && held().durability == d2d::rules::max_durability(held()));
    assert(world.fight.ammo_index() == int(index()));
    std::printf("OK: a broken stack's first one back mends it\n");

    // On the floor the timer goes on, without the mend.
    Item dropped = held();
    items.erase(items.begin() + std::ptrdiff_t(index()));
    dropped.quantity = 0; dropped.flags |= 0x100u; dropped.replenish_in = 3;
    world.loot.place(dropped, world.actor->player.x, world.actor->player.y, now_ms);
    assert(world.loot.ground.size() == 1);
    assert(frames_until([&] { return world.loot.ground[0].item.quantity > 0; }) == 3);
    assert(d2d::rules::broken(world.loot.ground[0].item) && world.loot.ground[0].item.replenish_in == 125);
    std::printf("OK: an item on the floor keeps its timer\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_replenish_world: %s\n", error.what());
    return 1;
}
