// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Event 4 (0x543d80, an item into a player's inventory) reaches the records
// an item is bound to only from its callers: the ground pickup, the corpse
// taken, the death and a save's corpse (World::item_event). A quest's gift
// (FUN_005466b0, placed by FUN_00560200) raises none.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

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
    namespace quest = d2d::rules::quest;
    using d2d::rules::CainQuest;
    const auto scroll = [] { return d2d::d2s::Item{ .code = "bks", .location = d2d::d2s::item_location::kCursor }; };

    // a1q4 out of the camp (state 3, log 1): a Bark Scroll's event 4 sets bit 3.
    world.cain.active = true; world.cain.state = 3; world.cain.log = 1;
    world.give_item("bks", 0);                                                    // Akara's kind of gift: no event
    assert(world.carries("bks") && !quest::bit(world.quests(), CainQuest::kQuest, 3));
    world.item_event("bks", 0);                                                   // picked up
    assert(quest::bit(world.quests(), CainQuest::kQuest, 3));
    quest::set(world.quests(), CainQuest::kQuest, 3, false);
    world.actor->held = scroll();                                                 // held as the death plays out:
    world.make_corpse(0);                                                         // the corpse's event marks the players
    assert(quest::bit(world.quests(), CainQuest::kQuest, 3));
    std::printf("OK: a1q4's Bark Scroll event comes from a pickup or the corpse, not a gift\n");

    // a3q4 (FUN_005ba6a0): the bird state 3, the figurine state 1 and the
    // event's player's bit 6; on a corpse those are its own new records.
    using d2d::rules::GoldenBirdQuest;
    world.golden_bird.open = true; world.golden_bird.state = 0;
    world.give_item(GoldenBirdQuest::kBird, 0);                                   // Meshif's gift: no event
    assert(world.golden_bird.state == 0);
    world.item_event(GoldenBirdQuest::kBird, 0);
    assert(world.golden_bird.state == 3);
    world.item_event(GoldenBirdQuest::kFigurine, 0);
    assert(world.golden_bird.state == 1 && quest::bit(world.quests(), GoldenBirdQuest::kQuest, 6));
    quest::set(world.quests(), GoldenBirdQuest::kQuest, 6, false);
    world.golden_bird.state = 0;
    world.actor->held = d2d::d2s::Item{ .code = GoldenBirdQuest::kFigurine, .location = d2d::d2s::item_location::kCursor };
    world.make_corpse(0);
    assert(world.golden_bird.state == 1 && !quest::bit(world.quests(), GoldenBirdQuest::kQuest, 6));
    std::printf("OK: a3q4's items on event 4; the corpse's bit 6 is its own\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
