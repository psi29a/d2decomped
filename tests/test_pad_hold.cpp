// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// test-includes: apps/d2d
// Which hold A confirms (apps/d2d/pad_hold.hpp), and that a full belt
// refuses another potion. It does not run Town.
#include "pad_hold.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using d2d::client::PadHoldKind;
using d2d::client::PadHoldQuery;
using d2d::client::pad_drops_from_bag;
using d2d::client::pad_hold_can_equip;
using d2d::client::pad_hold_kind;
using d2d::client::pad_shows_hold_cue;
using d2d::client::potion_code;

int main() try {
    assert(potion_code("hp3") && potion_code("mp1") && potion_code("rvs") && potion_code("rvl"));
    assert(potion_code("vps") && potion_code("yps") && potion_code("wms"));
    assert(!potion_code("key") && !potion_code("isc") && !potion_code("aar"));

    PadHoldQuery query;
    query.hand_empty = true;
    query.bag = true;
    query.potion = true;
    assert(pad_hold_kind(query) == PadHoldKind::Fill);
    query.shop = true;
    query.sellable = true;
    assert(pad_hold_kind(query) == PadHoldKind::Sell);
    query.store_mode = 1;
    assert(pad_hold_kind(query) == PadHoldKind::None);
    query.store_mode = 2;
    assert(pad_hold_kind(query) == PadHoldKind::Sell);
    query.store_mode = 3;
    assert(pad_hold_kind(query) == PadHoldKind::None);
    query.store_mode = 0;
    query.gamble = true;
    assert(pad_hold_kind(query) == PadHoldKind::None);
    query.gamble = false;
    query.identify = true;
    assert(pad_hold_kind(query) == PadHoldKind::None);
    query.identify = false;
    query.shop = false;
    query.potion = false;
    query.equippable = true;
    assert(pad_hold_kind(query) == PadHoldKind::Equip);
    query.hand_empty = false;
    assert(pad_hold_kind(query) == PadHoldKind::None);
    query.hand_empty = true;
    query.equippable = false;
    query.potion = true;
    query.shop = false;
    assert(d2d::client::pad_hold_button(query, true, false) == PadHoldKind::Fill);
    assert(d2d::client::pad_hold_button(query, false, true) == PadHoldKind::FillOne);
    assert(d2d::client::pad_hold_button(query, false, false) == PadHoldKind::None);
    query.potion = false;
    query.equippable = true;
    assert(d2d::client::pad_hold_button(query, false, true) == PadHoldKind::None);

    d2d::rules::Tables tables;
    tables.item_info["hp1"] = { .width = 1, .height = 1, .type = "hpot" };
    tables.types["hpot"].beltable = true;
    tables.item_base["hp1"].autobelt = true;
    std::vector<d2d::d2s::Item> items(5);
    for (int column = 0; column < 4; ++column) {
        items[std::size_t(column)].code = "hp1";
        items[std::size_t(column)].id = column + 1;
        items[std::size_t(column)].location = d2d::d2s::item_location::kBelt;
        items[std::size_t(column)].column = column;
    }
    items[4].code = "hp1";
    items[4].id = 9;
    items[4].identified = true;
    items[4].location = d2d::d2s::item_location::kStored;
    items[4].panel = d2d::d2s::item_panel::kInventory;
    assert(!d2d::rules::auto_belt(tables, items, 9, 4));
    assert(items[4].location == d2d::d2s::item_location::kStored);
    items[0].location = d2d::d2s::item_location::kStored;
    assert(d2d::rules::auto_belt(tables, items, 9, 4));
    assert(items[4].location == d2d::d2s::item_location::kBelt);

    // A joined game does not equip, and does not pick a bag item up and drop it.
    // The hold line is only for the pad.
    assert(pad_hold_can_equip(false, true) && !pad_hold_can_equip(true, true) && !pad_hold_can_equip(false, false));
    assert(pad_drops_from_bag(false) && !pad_drops_from_bag(true));
    assert(pad_shows_hold_cue(true, true, false, false));
    assert(!pad_shows_hold_cue(false, true, true, true));
    assert(!pad_shows_hold_cue(true, false, false, false));
    PadHoldQuery joined;
    joined.hand_empty = true;
    joined.bag = true;
    joined.equippable = pad_hold_can_equip(true, true);
    assert(pad_hold_kind(joined) == PadHoldKind::None);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pad_hold: %s\n", error.what());
    return 1;
}
