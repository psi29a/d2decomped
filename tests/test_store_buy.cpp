// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A bought item (FUN_00577830) goes to the belt first, as a pickup does
// (FUN_0055e9b0 -> FUN_0063c790: rules::belt_box), then, given a wearer, to
// a body slot (FUN_00562e00), and only then to the inventory's first free
// spot (FUN_00560200); nowhere for it, nothing is paid. No game data: a
// potion, an axe and a cap typed in.

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <utility>
#include <vector>

using namespace d2d::rules;
using d2d::d2s::Item;
namespace location = d2d::d2s::item_location;

int main() {
    Tables tables;
    tables.npc_prices["Akara"].max_buy = { 100000, 100000, 100000 };
    tables.types["hpot"].beltable = true;
    tables.types["axe"] = {};
    tables.item_info["hp1"] = { .type = "hpot" };
    tables.item_info["hax"] = { .width = 2, .height = 3, .type = "axe", .kind = 2 };
    tables.item_base["hp1"] = { .cost = 30, .autobelt = true };
    tables.item_base["hax"] = { .cost = 100 };

    Store shop;
    shop.npc_id = "Akara";
    shop.perm = { "hp1" };
    shop.tab = 3;
    Item potion;
    potion.code = "hp1";
    potion.identified = true;
    shop.tabs[3] = { potion };
    Item axe;
    axe.code = "hax";
    axe.identified = true;
    shop.tabs[1] = { axe, axe };
    const int potion_price = item_price(tables, potion, shop.npc_id, false, shop.header);

    std::vector<Item> items;
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kGold] = 100000;
    // An empty belt: autobelt, the first free bottom box (FUN_0063c600).
    assert(store_buy(tables, shop, 0, items, stats, 8) == Trade::done);
    assert(items.size() == 1 && items[0].location == location::kBelt && items[0].column == 0);
    assert(stats.get(d2d::d2s::kGold) == 100000 - potion_price && shop.tabs[3].size() == 1);
    // The next one stacks up its column, below the belt's 8 boxes.
    assert(store_buy(tables, shop, 0, items, stats, 8) == Trade::done && items[1].location == location::kBelt && items[1].column == 4);
    // Beltless (4 boxes): the column is full, so the next free bottom box.
    assert(store_buy(tables, shop, 0, items, stats) == Trade::done && items[2].location == location::kBelt && items[2].column == 1);
    assert(store_buy(tables, shop, 0, items, stats) == Trade::done && items[3].column == 2);
    assert(store_buy(tables, shop, 0, items, stats) == Trade::done && items[4].column == 3);
    // The belt full: the inventory.
    assert(store_buy(tables, shop, 0, items, stats) == Trade::done);
    assert(items[5].location == location::kStored && items[5].panel == d2d::d2s::item_panel::kInventory);

    // An axe isn't Beltable: the inventory, and it leaves the stock.
    shop.tab = 1;
    std::vector<Item> carried;
    assert(store_buy(tables, shop, 0, carried, stats) == Trade::done && carried[0].location == location::kStored && shop.tabs[1].size() == 1);
    // Nowhere for it: no_room, nothing paid and still stocked.
    Item big;
    big.code = "big";
    big.location = location::kStored;
    big.panel = d2d::d2s::item_panel::kInventory;
    tables.item_info["big"] = { .width = 10, .height = 4 };
    std::vector<Item> packed = { std::move(big) };
    const auto gold = stats.get(d2d::d2s::kGold);
    assert(store_buy(tables, shop, 0, packed, stats) == Trade::no_room && stats.get(d2d::d2s::kGold) == gold && shop.tabs[1].size() == 1);

    // Given a wearer, a body slot first (FUN_00562e00 -> FUN_0055d710 mode
    // 0): a cap to the empty head, an axe to the empty hand. With an axe in
    // hand a second goes to the inventory, though a Barbarian's pickup would
    // wield it (0x577d10: a weapon in hand bars a bought weapon).
    namespace body = d2d::d2s::body_location;
    tables.types["weap"] = {};
    tables.types["axe"].equiv = { "weap", "" };
    tables.types["axe"].body = { body::kRightArm, body::kLeftArm };
    tables.types["helm"].body = { body::kHead, body::kHead };
    tables.item_info["cap"] = { .width = 2, .height = 2, .type = "helm", .kind = 1 };
    tables.item_base["cap"] = { .cost = 50 };
    Item cap;
    cap.code = "cap";
    cap.identified = true;
    shop.tabs[0] = { cap };
    shop.tabs[1] = { axe, axe, axe };
    const Wearer barbarian{ .cls = d2d::d2s::kBarbarian, .str = 100, .dex = 100, .lvl = 30 };
    std::vector<Item> worn;
    shop.tab = 0;
    assert(store_buy(tables, shop, 0, worn, stats, 4, false, &barbarian) == Trade::done);
    assert(worn[0].location == location::kEquipped && worn[0].slot == body::kHead);
    shop.tab = 1;
    assert(store_buy(tables, shop, 0, worn, stats, 4, false, &barbarian) == Trade::done);
    assert(worn[1].location == location::kEquipped && worn[1].slot == body::kRightArm);
    assert(store_buy(tables, shop, 0, worn, stats, 4, false, &barbarian) == Trade::done);
    assert(worn[2].location == location::kStored && worn[2].panel == d2d::d2s::item_panel::kInventory);
    auto ground_axe = axe;
    assert(pick_up(tables, worn, ground_axe, 10, 4, 4, &barbarian) == Pickup::kGone && worn[3].slot == body::kLeftArm);   // the pickup does
    return 0;
}
