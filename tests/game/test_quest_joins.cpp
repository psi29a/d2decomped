// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The quests' joins (FUN_00546270, quests.md "Joins"): the game's first
// player runs every +0xd4 unless made new (`fresh`); a later player (World::
// join) runs only the +0xd8s (join_more). Expected values: tools/emu/
// quest_joins.py (plain: +0xd4, `--d8`: +0xd8).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests_act4.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

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
    const auto make = [&](int cls, const char* name) {
        auto made = new_character(*data, cls, name, false, true, rng);
        Character character;
        character.character_class = cls;
        character.name = name;
        character.header = made.header;
        character.stats = made.stats;
        character.items = made.items;
        character.panel = panel_stats(*data, character.header, character.items, character.stats);
        return character;
    };
    constexpr int kSorceress = 1, kBarbarian = 4;
    {   // a4q3's +0xd4 (FUN_005b64e0): no soulstone, state 1; a character made new runs none.
        auto loaded = make(kSorceress, "Loaded");
        World world(&*data, -1, -1);
        world.enter(loaded);
        assert(world.hellforge.state == 1);
        auto fresh = make(kSorceress, "Fresh");
        fresh.fresh = true;
        world.enter(fresh);
        assert(world.hellforge.state == 0);
    }
    {   // a1q3's +0xd8 (LAB_005919d0): the first carrier joining, the stand used: log 2.
        World world(&*data, -1, -1);
        world.enter(make(kSorceress, "Lead"));
        world.tools.state = 4; world.tools.dropped = true; world.tools.log = 1;
        auto carrier = make(kBarbarian, "Carrier");
        d2d::d2s::Item malus;
        malus.code = "hdm"; malus.identified = true;
        malus.location = d2d::d2s::item_location::kStored; malus.panel = d2d::d2s::item_panel::kInventory;
        malus.column = 0; malus.row = 0;
        carrier.items.push_back(malus);
        world.join(carrier);
        assert(world.tools.carriers == 1 && world.tools.log == 2);
        world.join(make(kSorceress, "Empty"));
        assert(world.tools.carriers == 1);                                        // no malus: no carrier
    }
    {   // a2q6's +0xd8 (LAB_0059d4d0): the joiner's word 10 bit 9 cleared.
        World world(&*data, -1, -1);
        world.enter(make(kSorceress, "Lead"));
        auto joiner = make(kSorceress, "Joiner");
        d2d::rules::quest::set(joiner.header.quests[std::size_t(joiner.header.active_difficulty())], 10, 9);
        world.join(joiner);
        const auto& joined = world.players.back()->character.header;
        assert(!d2d::rules::quest::bit(joined.quests[std::size_t(joined.active_difficulty())], 10, 9));
    }
    {   // a3q1's +0xd8 (LAB_005b7bc0): a tome carried, one more about.
        World world(&*data, -1, -1);
        world.enter(make(kSorceress, "Lead"));
        auto joiner = make(kSorceress, "Joiner");
        d2d::d2s::Item tome;
        tome.code = d2d::rules::LamEsenQuest::kTome; tome.identified = true;
        tome.location = d2d::d2s::item_location::kStored; tome.panel = d2d::d2s::item_panel::kInventory;
        joiner.items.push_back(tome);
        const int before = world.lam_esen.holders;
        world.join(joiner);
        assert(world.lam_esen.holders == before + 1);
    }
    {   // a3q7's +0xd4 (LAB_005bd2c0): a first player who's seen the Dark Wanderer: the game's bit 13, none made.
        World world(&*data, -1, -1);
        auto seen = make(kSorceress, "Seen");
        d2d::rules::quest::set(seen.header.quests[std::size_t(seen.header.active_difficulty())], 0x20, 0);
        world.enter(seen);
        assert(world.fight.spawning.wanderer_seen && d2d::rules::quest::bit(world.game_bits, 0x20, 13));
        const auto* jungle = data->level(76);                                     // the Spider Forest: object 368's InitFn 43
        assert(jungle);
        const auto marker = std::ranges::find_if(jungle->units, [](const auto& unit) { return unit.type == d2d::rules::unit_type::kObject && unit.id == 368; });
        const int wanderer = int(data->mon_bin[0x170]);
        const auto wanderers = [&] { return std::ranges::count(world.fight.spawning.levels[jungle].spawns, wanderer, &d2d::rules::Spawn::type); };
        const bool has_marker = marker != jungle->units.end();
        if (has_marker) populate_level(*data, world.fight.spawning, *jungle);
        assert(wanderers() == 0);                                                 // seen: none
        world.enter(make(kSorceress, "Unseen"));
        assert(!world.fight.spawning.wanderer_seen && !d2d::rules::quest::bit(world.game_bits, 0x20, 13));
        if (has_marker) populate_level(*data, world.fight.spawning, *jungle);
        assert(!has_marker || wanderers() == 1);                                  // unseen: him
        if (!has_marker) std::printf("SKIP: no object 368 in the Spider Forest this seed\n");
    }
    std::printf("OK: quest joins\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
