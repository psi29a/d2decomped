// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The ancients' gear (FUN_005b1c50, the table at 0x6e2c90): four magic items
// each, the ubercode in Nightmare and the ultracode in Hell; Korlic's ring has
// neither, so past Normal he wears three. A woken ancient (World::seal_boss)
// wears them: each item two game seed steps (FUN_00559ce0). Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
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
    if (!data || !data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    using Worn = std::vector<std::pair<std::string, int>>;
    assert(d2d::rules::ancient_gear(data->rules, 0x21c, 0) == (Worn{ { "bsd", 4 }, { "tow", 5 }, { "fld", 3 }, { "hbt", 9 } }));
    assert(d2d::rules::ancient_gear(data->rules, 0x21d, 1) == (Worn{ { "9ta", 4 }, { "9ta", 5 }, { "xhg", 10 }, { "xhb", 9 } }));
    assert(d2d::rules::ancient_gear(data->rules, 0x21e, 0) == (Worn{ { "vou", 4 }, { "rin", 6 }, { "fld", 3 }, { "crn", 1 } }));
    assert(d2d::rules::ancient_gear(data->rules, 0x21e, 2) == (Worn{ { "7vo", 4 }, { "uld", 3 }, { "urn", 1 } }));
    assert(d2d::rules::ancient_gear(data->rules, 0x21b, 0).empty());
    std::printf("OK: the ancients' gear by difficulty, no ring past Normal\n");

    // Talic woken (his superunique row, no minions): one game seed step for his unit seed as he's made
    // (FUN_00555230 -> FUN_00650e40), then eight for his four items.
    const auto talic = std::ranges::find(data->superuniques, 0x21c, &GameData::SuperUnique::type);
    const auto* arreat = data->level(d2d::rules::level_ids::kArreatSummit);
    if (talic == data->superuniques.end() || !arreat) { std::printf("SKIP: no Talic or Arreat Summit\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 4, "Rite", false, true, rng);
    Character character;
    character.character_class = 4;
    character.name = "Rite";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);
    auto expected = world.fight.spawning.game;
    for (int step = 0; step < 9; ++step) expected.next();
    world.seal_boss(*arreat, int(talic - data->superuniques.begin()), arreat->ds1.width() * 5 / 2, arreat->ds1.height() * 5 / 2);
    assert(world.fight.spawning.game.low == expected.low && world.fight.spawning.game.high == expected.high);
    std::printf("OK: Talic's unit seed and four items take nine game seed steps\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ancient_gear: %s\n", error.what());
    return 1;
}
