// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A set item's hover text (FUN_0048d1d0, drawn bottom-up): under its own
// lines a blank, while it's worn the set's bonuses the owner has (gold,
// FUN_004e6680) and a blank, the set's name (gold) and its members from the
// last SetItems row to the first, green when the owner has an identified
// piece of that row worn or in the inventory, cube or stash (FUN_00486770),
// else red. Civerb's Vestments: Ward (row 0, lrg), Icon (1, amu), Cudgel
// (2, gsc); two pieces worn give its 2a, res-fire 15. Needs the game data.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    namespace d2s = d2d::d2s;
    const auto piece = [](const char* code, int row) {
        d2s::Item item;
        item.code = code; item.quality = 5; item.set_id = row; item.identified = true; item.defense = -1;
        return item;
    };
    auto ward = piece("lrg", 0), icon = piece("amu", 1);
    ward.location = d2s::item_location::kEquipped; ward.slot = d2s::body_location::kLeftArm;
    icon.location = d2s::item_location::kStored; icon.panel = d2s::item_panel::kStash;
    std::vector<d2s::Item> owned{ ward, std::move(icon) };
    const auto lines = item_lines(*data, ward, 30, nullptr, &owned);
    assert(lines.size() >= 5);
    const auto tail = lines.end() - 5;
    assert(tail[0].text.empty());
    assert(tail[1].text == "Civerb's Vestments" && tail[1].rgb == kTxtGold);
    assert(tail[2].text == "Civerb's Cudgel" && tail[2].rgb == kTxtRed);
    assert(tail[3].text == "Civerb's Icon" && tail[3].rgb == kTxtGreen);
    assert(tail[4].text == "Civerb's Ward" && tail[4].rgb == kTxtGreen);
    // The Icon worn too: the set's 2a shows in gold between two blanks.
    owned[1].location = d2s::item_location::kEquipped; owned[1].slot = d2s::body_location::kNeck; owned[1].panel = 0;
    const auto worn = item_lines(*data, owned[0], 30, nullptr, &owned);
    assert(worn.size() >= 7);
    const auto gold = worn.end() - 7;
    assert(gold[0].text.empty() && gold[1].rgb == kTxtGold && !gold[1].text.empty() && gold[2].text.empty());
    assert(gold[3].text == "Civerb's Vestments");
    // Not worn (in the stash), its set's bonuses don't show; no owner, every member is red.
    const auto stashed = item_lines(*data, piece("amu", 1), 30);
    assert(stashed.back().text == "Civerb's Ward" && stashed.back().rgb == kTxtRed);
    std::printf("set tooltip: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_set_tooltip: %s\n", error.what());
    return 1;
}
