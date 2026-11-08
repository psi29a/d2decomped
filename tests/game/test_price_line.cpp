// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A store's price on an item's hover (add_price_line): FUN_0048dd90 adds
// FUN_004b2ad0's "Cost: " / "Sell value: " after the name (FUN_0048cea0 at
// 0x48ec27), the belt's hover after its lines (FUN_00497a40 at 0x497bf1).
// The string is drawn bottom-up, so the price shows on top, and it has no
// colour code of its own, so it keeps the colour the top line ends in.

#include <item_text.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using d2d::game::add_price_line;
using d2d::game::TextLine;

int main() try {
    // A magic item: the price goes over the blue name, in blue.
    std::vector<TextLine> magic{ { "Jagged Short Sword", d2d::game::kTxtBlue }, { "One-Hand Damage: 2 to 7", d2d::game::kTxtWhite } };
    add_price_line(magic, "Sell value: 35");
    assert(magic.size() == 3 && magic[0].text == "Sell value: 35" && magic[0].rgb == d2d::game::kTxtBlue);
    assert(magic[1].text == "Jagged Short Sword" && magic[2].rgb == d2d::game::kTxtWhite);

    // A unique: gold. A plain item: white.
    std::vector<TextLine> unique{ { "The Gnasher", d2d::game::kTxtGold }, { "Hand Axe", d2d::game::kTxtGold } };
    add_price_line(unique, "Cost: 2400");
    assert(unique[0].rgb == d2d::game::kTxtGold && unique[1].text == "The Gnasher");
    std::vector<TextLine> plain{ { "Minor Healing Potion", d2d::game::kTxtWhite } };
    add_price_line(plain, "Sell value: 1");
    assert(plain[0].rgb == d2d::game::kTxtWhite);

    // A top line that changes colour part-way: the colour it ends in.
    std::vector<TextLine> split{ { "Defense: 12", d2d::game::kTxtWhite, 9, d2d::game::kTxtBlue } };
    add_price_line(split, "Sell value: 3");
    assert(split[0].rgb == d2d::game::kTxtBlue);

    // Nothing above it: white.
    std::vector<TextLine> none;
    add_price_line(none, "Cost: 5");
    assert(none.size() == 1 && none[0].rgb == d2d::game::kTxtWhite);
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
