// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

using namespace d2d::game;

int main() {
    const char* data_dir = std::getenv("D2_MPQ_DIR");
    if (!data_dir) return D2D_TEST_SKIPPED;
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? std::filesystem::path(patch) : std::filesystem::path{}, 3397892852u);
    assert(data);

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 1, "Headless", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);

    const auto* tristram = data->level(d2d::rules::level_ids::kTristram);
    assert(tristram);
    populate_level(*data, world.fight.spawning, *tristram);
    const auto corpse = std::ranges::find(tristram->npcs, d2d::rules::operate_fn::kWirtBody, &Npc::operate_fn);
    assert(corpse != tristram->npcs.end());
    const int index = int(corpse - tristram->npcs.begin());

    const auto count = [&](const char* code) {
        return std::ranges::count_if(world.loot.ground, [code](const auto& item) { return item.item.code == code; });
    };
    world.arrive(tristram, corpse->x + 3, corpse->y, "test");
    world.autoloot_gold = false;

    // Follow the same interaction command and walk-to-object ticks as a click.
    std::uint32_t now = 1000;
    const auto tick = [&] {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    };
    world.apply(cmd::Interact{ index }, now);
    assert(world.actor->interact_npc == index);
    while (world.actor->interact_npc >= 0 && now < 9000) tick();
    assert(count("leg") == 1 && count("gld") == 0);

    for (int frame = 0; frame < 9; ++frame) tick();
    assert(count("gld") == 0);
    tick();
    assert(count("gld") == 1);
    for (int frame = 0; frame < 190; ++frame) tick();
    const auto piles = count("gld");
    assert(piles >= 10 && piles < 20);

    world.apply(cmd::Interact{ index }, now);
    assert(world.actor->interact_npc == -1);
    for (int frame = 0; frame < 10; ++frame) tick();
    assert(count("leg") == 1 && count("gld") == piles);
}
