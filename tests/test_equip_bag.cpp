// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Bag equip (rules::equip_from_bag). The host command calls this and changes
// nothing when it returns false. A joined game does not send the command.
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

using d2d::d2s::Item;
using d2d::rules::Tables;
using d2d::rules::Wearer;
using d2d::rules::equip_from_bag;

namespace {
Item bag(const std::string& code, int id, int column, int row, bool identified = true) {
    Item item;
    item.code = code;
    item.id = id;
    item.identified = identified;
    item.location = d2d::d2s::item_location::kStored;
    item.panel = d2d::d2s::item_panel::kInventory;
    item.column = column;
    item.row = row;
    return item;
}

int count_id(const std::vector<Item>& items, int id) {
    int count = 0;
    for (const Item& item : items) if (item.id == id) ++count;
    return count;
}
}

int main() try {
    Tables tables;
    tables.item_info["aar"] = { .width = 2, .height = 2, .type = "tors", .req_str = 20 };
    tables.item_info["old"] = { .width = 2, .height = 3, .type = "tors" };
    tables.item_info["cap"] = { .width = 2, .height = 2, .type = "tors" };
    tables.item_info["gem"] = { .width = 1, .height = 1, .type = "gem" };
    tables.item_info["swd"] = { .width = 1, .height = 3, .type = "swor" };
    tables.item_info["rin"] = { .width = 1, .height = 1, .type = "ring" };
    tables.types["tors"].body = { d2d::d2s::body_location::kTorso, 0 };
    tables.types["swor"].body = { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm };
    tables.types["ring"].body = { d2d::d2s::body_location::kRightRing, d2d::d2s::body_location::kLeftRing };
    tables.types["gem"].body = {};
    const Wearer hero{ 0, 40, 40, 20 };
    const Wearer weak{ 0, 10, 40, 20 };

    std::vector<Item> items;
    items.push_back(bag("aar", 1, 0, 0));
    std::optional<Item> hand;
    assert(equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    assert(!hand && items.size() == 1 && items[0].location == d2d::d2s::item_location::kEquipped && items[0].slot == d2d::d2s::body_location::kTorso);
    assert(count_id(items, 1) == 1);

    items[0].location = d2d::d2s::item_location::kStored;
    items[0].panel = d2d::d2s::item_panel::kInventory;
    items[0].column = items[0].row = 0;
    Item worn = bag("cap", 2, 0, 0);
    worn.location = d2d::d2s::item_location::kEquipped;
    worn.slot = d2d::d2s::body_location::kTorso;
    worn.panel = worn.column = worn.row = 0;
    items.push_back(worn);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 10; ++column) {
            if (column < 2 && row < 2) continue;
            items.push_back(bag("gem", 100 + row * 10 + column, column, row));
        }
    const auto before = items;
    assert(equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    assert(count_id(items, 1) == 1 && count_id(items, 2) == 1);
    const Item* now_new = nullptr;
    const Item* now_old = nullptr;
    for (const Item& item : items) {
        if (item.id == 1) now_new = &item;
        if (item.id == 2) now_old = &item;
    }
    assert(now_new && now_new->location == d2d::d2s::item_location::kEquipped && now_new->slot == d2d::d2s::body_location::kTorso);
    assert(now_old && now_old->location == d2d::d2s::item_location::kStored && now_old->panel == d2d::d2s::item_panel::kInventory);

    items = before;
    items[1].code = "old";
    const auto refused = items;
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    assert(items.size() == refused.size());
    for (std::size_t index = 0; index < items.size(); ++index)
        assert(items[index].id == refused[index].id && items[index].location == refused[index].location && items[index].column == refused[index].column);

    hand = bag("gem", 9, 0, 0);
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    assert(hand && hand->id == 9);
    hand.reset();

    items[0].identified = false;
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    items[0].identified = true;
    assert(!equip_from_bag(tables, items, hand, 1, weak, -1, 10, 4));
    items[0].code = "swd";
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    items[0].code = "rin";
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    items[0].code = "aar";
    items[0].panel = d2d::d2s::item_panel::kStash;
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    items[0].panel = d2d::d2s::item_panel::kInventory;
    assert(!equip_from_bag(tables, items, hand, 99, hero, -1, 10, 4));
    tables.types["tors"].cls = "bar";
    assert(!equip_from_bag(tables, items, hand, 1, hero, -1, 10, 4));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_equip_bag: %s\n", error.what());
    return 1;
}
