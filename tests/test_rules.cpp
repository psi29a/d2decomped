// Store rules over hand-made tables: prices, buy/sell gold, placement, stock.
#include <rules.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;
using d2d::d2s::Item;
using d2d::d2s::kGold;
using d2d::d2s::kGoldBank;
using d2d::d2s::kLevel;

static Item item(const char* code, int quality = 2) {
    Item it;
    it.code = code;
    it.quality = quality;
    return it;
}

int main() {
    Tables t;
    t.item_info["hax"] = { .w = 2, .h = 3, .kind = 2 };   // weapon: store tab 1
    t.item_info["cap"] = { .w = 2, .h = 2, .kind = 1 };   // armour: tab 0
    t.item_info["wall"] = { .w = 10, .h = 10, .kind = 2 };
    t.item_info["inv"] = { .w = 10, .h = 4 };             // fills the whole inventory
    t.item_base["hax"] = { .cost = 1000 };
    t.item_base["big"] = { .cost = 100000 };
    t.item_base["gem"] = { .cost = 200 };
    t.item_base["key"] = { .cost = 10, .stackable = true };
    t.prefix_cost = { { 0, 0 }, { 1024, 100 } };           // row 0 = none
    NpcPrice charsi;
    charsi.sell = 256;
    charsi.max_buy = { 5000, 10000, 20000 };
    charsi.qflag[0] = 1; charsi.qbuy[0] = 512; charsi.qsell[0] = 1024;
    t.npc_prices["Charsi"] = charsi;
    const d2d::d2s::Header h;

    // Prices.
    assert(item_price(t, item("hax"), "Charsi", false, h) == 1000);
    assert(item_price(t, item("hax", 1), "Charsi", false, h) == 500);        // low quality: half off
    Item magic = item("hax", 4); magic.prefix = 1;
    assert(item_price(t, magic, "Charsi", false, h) == 2100);                // + 1024/1024 x base + 100
    assert(item_price(t, item("hax"), "Charsi", true, h) == 250);            // sell mult 256/1024
    Item eth = item("hax"); eth.ethereal = true;
    assert(item_price(t, eth, "Charsi", true, h) == 62);                     // ethereal sells for a quarter
    assert(item_price(t, item("big"), "Charsi", true, h) == 5000);           // capped at max_buy (normal)
    Item socketed = item("hax"); socketed.socketed_items = { item("gem") };
    assert(item_price(t, socketed, "Charsi", false, h) == 1100);             // + half each socketed item
    Item three = item("hax"); three.quantity = 3;
    assert(item_price(t, three, "Charsi", false, h) == 3000);                // non-stackable x quantity
    Item keys = item("key"); keys.quantity = 3;
    assert(item_price(t, keys, "Charsi", false, h) == 10);                   // stackables priced once
    assert(item_price(t, item("nothing"), "Charsi", false, h) == 1);         // never below 1
    assert(item_price(t, item("hax"), "Nobody", true, h) == 1000);           // no npc.txt row: unscaled
    d2d::d2s::Header quest = h;
    quest.quests[0][2] = 1;                                                  // quest 1, bit 0 (normal)
    assert(item_price(t, item("hax"), "Charsi", false, quest) == 500);       // qbuy 512/1024

    // Placement: column-first, weapons spill from tab 1 to 2, armour doesn't.
    Store st;
    assert(store_place(t, st, 1, item("hax")) && store_place(t, st, 1, item("hax")));
    assert(st.tabs[1][0].column == 0 && st.tabs[1][0].row == 0);
    assert(st.tabs[1][1].column == 0 && st.tabs[1][1].row == 3);
    Store full;
    full.tabs[1] = { item("wall") };
    full.tabs[0] = { item("wall") };
    assert(store_place(t, full, 1, item("hax")) && full.tabs[2].size() == 1);
    assert(!store_place(t, full, 0, item("cap")));
    assert(store_tab_for(t, "cap") == 0 && store_tab_for(t, "hax") == 1 && store_tab_for(t, "gem") == 3);

    // Stock: Charsi (hcIdx 0x9a) is vendor 2; perm items stay after a buy.
    assert(vendor_index(0x9a) == 2 && vendor_index(1) == -1);
    t.vendor_items[2] = { { .code = "hax", .min = 2, .max = 2 }, { .code = "key", .perm = true } };
    std::uint32_t rng = 1;
    Store shop = open_store(t, 0x9a, "Charsi", rng);
    assert(shop.vendor == 2 && shop.npc_id == "Charsi");
    assert(shop.tabs[1].size() == 2 && shop.tabs[3].size() == 1 && shop.tab == 1);
    assert(open_store(t, 1, "Nobody", rng).vendor == -1);

    // Buying: carried gold first, then the stash.
    std::vector<Item> inv;
    d2d::d2s::Stats stats;
    stats.v[kGold] = 300; stats.v[kGoldBank] = 1000; stats.v[kLevel] = 1;
    assert(store_buy(t, shop, 0, inv, stats));
    assert(stats.get(kGold) == 0 && stats.get(kGoldBank) == 300);
    assert(inv.size() == 1 && inv[0].panel == 1 && shop.tabs[1].size() == 1);
    assert(!store_buy(t, shop, 0, inv, stats));                              // can't afford: nothing changes
    assert(stats.get(kGoldBank) == 300 && inv.size() == 1);
    shop.tab = 3;
    assert(store_buy(t, shop, 0, inv, stats) && shop.tabs[3].size() == 1);   // perm: still stocked
    std::vector<Item> packed = { item("inv") };
    packed[0].panel = 1;
    shop.tab = 1;
    stats.v[kGoldBank] = 100000;
    assert(!store_buy(t, shop, 0, packed, stats) && stats.get(kGoldBank) == 100000);   // no room

    // Selling: gold up, capped at clvl x 10000; the item joins the stock.
    stats.v[kGold] = 9900;
    store_sell(t, shop, 0, inv, stats);
    assert(stats.get(kGold) == 10000);
    assert(inv.size() == 1 && shop.tabs[1].size() == 2);

    std::puts("test_rules: ok");
}
