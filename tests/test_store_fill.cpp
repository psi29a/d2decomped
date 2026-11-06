// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Shift + right click on a multibuy item (FUN_004b2650 sets C->S 0x32's
// 0x80000000) fills, for a perm item only (FUN_00577830, FUN_00576ed0): a
// scroll fills its tome, keys (AutoStack) top up a carried stack or come
// as one stack of what the gold buys, and potions are bought while the
// belt takes them, then one for the inventory. No game data: a scroll,
// a tome, keys and a potion typed in.

#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

using namespace d2d::rules;
using d2d::d2s::Item;
namespace location = d2d::d2s::item_location;

namespace {

Item stock(const char* code) {
    Item item;
    item.code = code;
    item.identified = true;
    item.quality = 2;
    return item;
}
Item carried(const char* code, int quantity) {
    Item item = stock(code);
    item.location = location::kStored;
    item.panel = d2d::d2s::item_panel::kInventory;
    item.quantity = quantity;
    return item;
}

}  // namespace

int main() {
    Tables tables;
    tables.npc_prices["Akara"].max_buy = { 100000, 100000, 100000 };
    tables.types["scro"] = {};
    tables.types["book"] = {};
    tables.types["key"].autostack = true;
    tables.types["hpot"].beltable = true;
    tables.item_info["tsc"] = { .type = "scro" };
    tables.item_info["tbk"] = { .type = "book" };
    tables.item_info["key"] = { .type = "key" };
    tables.item_info["hp1"] = { .type = "hpot" };
    tables.item_base["tsc"] = { .cost = 100, .multibuy = true };
    tables.item_base["tbk"] = { .cost = 150, .max_stack = 20 };
    tables.item_base["key"] = { .cost = 10, .stackable = true, .max_stack = 12, .multibuy = true };
    tables.item_base["hp1"] = { .cost = 30, .autobelt = true, .multibuy = true };

    Store shop;
    shop.npc_id = "Akara";
    shop.perm = { "tsc", "key", "hp1" };
    shop.tab = 3;
    auto keys = stock("key");
    keys.quantity = 12;
    shop.tabs[3] = { stock("tsc"), keys, stock("hp1") };
    const auto price = [&](const Item& item) { return item_price(tables, item, shop.npc_id, false, shop.header); };
    const int scroll = price(stock("tsc")), potion = price(stock("hp1"));
    auto one_key = stock("key");
    one_key.quantity = 1;
    const int key = price(one_key);
    assert(key == 10 && price(keys) == 120);

    d2d::d2s::Stats stats;
    const auto gold = [&] { return stats.get(d2d::d2s::kGold); };

    // A scroll fills its tome, as far as the gold goes (local_14 / local_8).
    std::vector<Item> items = { carried("tbk", 5) };
    stats.values[d2d::d2s::kGold] = 3 * scroll + 1;
    assert(store_buy(tables, shop, 0, items, stats, 4, true) == Trade::done && items[0].quantity == 8 && gold() == 1);
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 0, items, stats, 4, true) == Trade::done && items[0].quantity == 20 && gold() == 100000 - 12 * scroll);
    items[0].quantity = 5;
    assert(store_buy(tables, shop, 0, items, stats) == Trade::done && items[0].quantity == 6);   // no shift: one
    shop.perm = { "key", "hp1" };                                                                  // not perm: no fill
    assert(store_buy(tables, shop, 0, items, stats, 4, true) == Trade::done && items[0].quantity == 7);
    shop.perm = { "tsc", "key", "hp1" };

    // Keys top up the carried stack with room, one key's price each (FUN_00577700).
    items = { carried("key", 7) };
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 1, items, stats, 4, true) == Trade::done && items.size() == 1 && items[0].quantity == 12);
    assert(gold() == 100000 - 5 * key);
    items[0].quantity = 3;
    stats.values[d2d::d2s::kGold] = 12 * key + 5;                                                  // enough for the stocked 12, and no more
    assert(store_buy(tables, shop, 1, items, stats, 4, true) == Trade::done && items[0].quantity == 12 && gold() == 3 * key + 5);
    // None carried: one stack of what the gold buys, at most 12.
    items.clear();
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 1, items, stats, 4, true) == Trade::done && items.size() == 1 && items[0].quantity == 12);
    assert(items[0].location == location::kStored && gold() == 100000 - 12 * key);
    // No shift: the stocked stack, as it is, beside the carried one.
    items = { carried("key", 7) };
    assert(store_buy(tables, shop, 1, items, stats) == Trade::done && items.size() == 2 && items[0].quantity == 7 && items[1].quantity == 12);

    // Potions: bought while the belt takes them (8 boxes), then one for the inventory.
    items.clear();
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 2, items, stats, 8, true) == Trade::done && items.size() == 9);
    for (std::size_t i = 0; i < 8; ++i) assert(items[i].location == location::kBelt);
    assert(items[8].location == location::kStored && gold() == 100000 - 9 * potion);
    // The gold runs out first: those bought stay, and no_gold (0x8d4).
    items.clear();
    stats.values[d2d::d2s::kGold] = 3 * potion;
    assert(store_buy(tables, shop, 2, items, stats, 8, true) == Trade::no_gold && items.size() == 3 && gold() == 0);
    // In an expansion game a super potion is perm (FUN_00576ed0) though not
    // in PermStoreItems: it fills, and stays stocked (FUN_00576f50).
    tables.item_info["hp5"] = { .type = "hpot" };
    tables.item_base["hp5"] = { .cost = 300, .autobelt = true, .multibuy = true };
    shop.tabs[3].push_back(stock("hp5"));
    shop.header.status = 0x20;
    items.clear();
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 3, items, stats, 8, true) == Trade::done && items.size() == 9 && shop.tabs[3].size() == 4);
    shop.header.status = 0;
    assert(store_buy(tables, shop, 3, items, stats, 8, true) == Trade::done && shop.tabs[3].size() == 3);   // classic: one, then gone
    // The belt and the inventory full after one: done, nothing more said.
    tables.item_info["big"] = { .width = 10, .height = 4 };
    tables.item_info["mp1"] = { .type = "hpot" };
    items = { carried("big", -1) };
    for (int box = 0; box < 3; ++box) {
        auto mana = carried("mp1", -1);
        mana.location = location::kBelt;
        mana.column = box;
        items.push_back(std::move(mana));
    }
    stats.values[d2d::d2s::kGold] = 100000;
    assert(store_buy(tables, shop, 2, items, stats, 4, true) == Trade::done && items.size() == 5 && items[4].column == 3 && gold() == 100000 - potion);
    return 0;
}
