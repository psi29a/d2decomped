// SPDX-License-Identifier: GPL-3.0-or-later
// Store rules over hand-made tables: prices, buy/sell gold, placement, stock.
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

using namespace d2d::rules;
using d2d::d2s::Item;
using d2d::d2s::kGold;
using d2d::d2s::kGoldBank;
using d2d::d2s::kLevel;

static Item item(const char* code, int quality = 2) {
    Item item;
    item.code = code;
    item.quality = quality;
    return item;
}

int main() {
    Tables tables;
    tables.item_info["hax"] = { .width = 2, .height = 3, .kind = 2 };   // weapon: store tab 1
    tables.item_info["cap"] = { .width = 2, .height = 2, .kind = 1 };   // armour: tab 0
    tables.item_info["wall"] = { .width = 10, .height = 10, .kind = 2 };
    tables.item_info["inv"] = { .width = 10, .height = 4 };             // fills the whole inventory
    tables.item_base["hax"] = { .cost = 1000 };
    tables.item_base["big"] = { .cost = 100000 };
    tables.item_base["gem"] = { .cost = 200 };
    tables.item_base["key"] = { .cost = 10, .stackable = true };
    tables.prefix_cost = { { 0, 0 }, { 1024, 100 } };           // row 0 = none
    NpcPrice charsi;
    charsi.sell = 256;
    charsi.max_buy = { 5000, 10000, 20000 };
    charsi.qflag[0] = 1; charsi.qbuy[0] = 512; charsi.qsell[0] = 1024;
    tables.npc_prices["Charsi"] = charsi;
    const d2d::d2s::Header header;

    // Prices.
    assert(item_price(tables, item("hax"), "Charsi", false, header) == 1000);
    assert(item_price(tables, item("hax", 1), "Charsi", false, header) == 500);        // low quality: half off
    Item magic = item("hax", 4); magic.prefix = 1;
    assert(item_price(tables, magic, "Charsi", false, header) == 2100);                // + 1024/1024 x base + 100
    assert(item_price(tables, item("hax"), "Charsi", true, header) == 250);            // sell mult 256/1024
    Item eth = item("hax"); eth.ethereal = true;
    assert(item_price(tables, eth, "Charsi", true, header) == 62);                     // ethereal sells for a quarter
    assert(item_price(tables, item("big"), "Charsi", true, header) == 5000);           // capped at max_buy (normal)
    Item socketed = item("hax"); socketed.socketed_items = { item("gem") };
    assert(item_price(tables, socketed, "Charsi", false, header) == 1100);             // + half each socketed item
    Item three = item("hax"); three.quantity = 3;
    assert(item_price(tables, three, "Charsi", false, header) == 3000);                // non-stackable x quantity
    Item keys = item("key"); keys.quantity = 3;
    assert(item_price(tables, keys, "Charsi", false, header) == 10);                   // stackables priced once
    assert(item_price(tables, item("nothing"), "Charsi", false, header) == 1);         // never below 1
    assert(item_price(tables, item("hax"), "Nobody", true, header) == 1000);           // no npc.txt row: unscaled
    d2d::d2s::Header quest = header;
    quest.quests[0][2] = 1;                                                  // quest 1, bit 0 (normal)
    assert(item_price(tables, item("hax"), "Charsi", false, quest) == 500);       // qbuy 512/1024

    // Placement: column-first, weapons spill from tab 1 to 2, armour doesn't.
    Store store;
    assert(store_place(tables, store, 1, item("hax")) && store_place(tables, store, 1, item("hax")));
    assert(store.tabs[1][0].column == 0 && store.tabs[1][0].row == 0);
    assert(store.tabs[1][1].column == 0 && store.tabs[1][1].row == 3);
    Store full;
    full.tabs[1] = { item("wall") };
    full.tabs[0] = { item("wall") };
    assert(store_place(tables, full, 1, item("hax")) && full.tabs[2].size() == 1);
    assert(!store_place(tables, full, 0, item("cap")));
    assert(store_tab_for(tables, "cap") == 0 && store_tab_for(tables, "hax") == 1 && store_tab_for(tables, "gem") == 3);

    // Stock: Charsi (hcIdx 0x9a) is vendor 2; perm items stay after a buy.
    assert(vendor_index(0x9a) == 2 && vendor_index(1) == -1);
    tables.vendor_items[2] = { { .code = "hax", .min = 2, .max = 2 }, { .code = "key", .perm = true } };
    Rng rng{ 1 };
    Store shop = open_store(tables, 0x9a, "Charsi", rng);
    assert(shop.vendor == 2 && shop.npc_id == "Charsi");
    assert(shop.tabs[1].size() == 2 && shop.tabs[3].size() == 1 && shop.tab == 1);
    assert(open_store(tables, 1, "Nobody", rng).vendor == -1);

    // Buying: carried gold first, then the stash.
    std::vector<Item> inv;
    d2d::d2s::Stats stats;
    stats.values[kGold] = 300; stats.values[kGoldBank] = 1000; stats.values[kLevel] = 1;
    assert(store_buy(tables, shop, 0, inv, stats));
    assert(stats.get(kGold) == 0 && stats.get(kGoldBank) == 300);
    assert(inv.size() == 1 && inv[0].panel == 1 && shop.tabs[1].size() == 1);
    assert(!store_buy(tables, shop, 0, inv, stats));                              // can't afford: nothing changes
    assert(stats.get(kGoldBank) == 300 && inv.size() == 1);
    shop.tab = 3;
    assert(store_buy(tables, shop, 0, inv, stats) && shop.tabs[3].size() == 1);   // perm: still stocked
    std::vector<Item> packed = { item("inv") };
    packed[0].panel = 1;
    shop.tab = 1;
    stats.values[kGoldBank] = 100000;
    assert(!store_buy(tables, shop, 0, packed, stats) && stats.get(kGoldBank) == 100000);   // no room

    // Selling: gold up, capped at clvl x 10000; the item joins the stock.
    stats.values[kGold] = 9900;
    store_sell(tables, shop, 0, inv, stats);
    assert(stats.get(kGold) == 10000);
    assert(inv.size() == 1 && shop.tabs[1].size() == 2);

    // The item cursor.
    Tables cursor_tables;
    cursor_tables.types["weap"] = {};
    cursor_tables.types["mele"] = { .equiv = { "weap", "" } };
    cursor_tables.types["swor"] = { .equiv = { "mele", "" }, .body = { 4, 5 } };
    cursor_tables.types["axe"]  = { .equiv = { "mele", "" }, .body = { 4, 5 } };
    cursor_tables.types["shld"] = {};
    cursor_tables.types["shie"] = { .equiv = { "shld", "" }, .body = { 4, 5 } };
    cursor_tables.types["misl"] = {};
    cursor_tables.types["bowq"] = { .equiv = { "misl", "" }, .body = { 4, 5 } };
    cursor_tables.types["bow"]  = { .equiv = { "weap", "" }, .body = { 4, 5 } };
    cursor_tables.types["clas"] = {};
    cursor_tables.types["amaz"] = { .equiv = { "clas", "" }, .cls = "ama" };
    cursor_tables.types["abow"] = { .equiv = { "bow", "amaz" }, .body = { 4, 5 } };
    cursor_tables.types["helm"] = { .body = { 1, 1 } };
    cursor_tables.types["poti"] = { .beltable = true };
    cursor_tables.types["hpot"] = { .equiv = { "poti", "" } };
    cursor_tables.item_info["ssd"] = { .width = 1, .height = 3, .type = "swor", .kind = 2, .req_str = 25 };
    cursor_tables.item_info["2hs"] = { .width = 2, .height = 4, .type = "swor", .kind = 2, .two_handed = true, .one_or_two = true };
    cursor_tables.item_info["2ax"] = { .width = 2, .height = 3, .type = "axe", .kind = 2, .two_handed = true };
    cursor_tables.item_info["buc"] = { .width = 2, .height = 2, .type = "shie", .kind = 1 };
    cursor_tables.item_info["aqv"] = { .width = 1, .height = 3, .type = "bowq" };
    cursor_tables.item_info["sbw"] = { .width = 2, .height = 3, .type = "bow", .kind = 2, .two_handed = true };
    cursor_tables.item_info["am1"] = { .width = 2, .height = 4, .type = "abow", .kind = 2, .two_handed = true };
    cursor_tables.item_info["cap"] = { .width = 2, .height = 2, .type = "helm", .kind = 1, .req_lvl = 5 };
    cursor_tables.item_info["hp1"] = { .type = "hpot" };
    cursor_tables.item_info["box"] = { .width = 2, .height = 2 };
    const Wearer sorc{ .cls = d2d::d2s::kSorceress, .str = 30, .dex = 30, .lvl = 10 };
    const Wearer barb{ .cls = d2d::d2s::kBarbarian, .str = 30, .dex = 30, .lvl = 10 };
    auto item_at = [](const std::vector<Item>& items, int loc, int slot) -> const Item* {
        for (const auto& x : items) if (x.location == loc && x.slot == slot) return &x;
        return nullptr;
    };
    auto stored = [](const char* code, int panel, int col, int row) {
        Item stored_item = item(code); stored_item.location = 0; stored_item.panel = panel; stored_item.column = col; stored_item.row = row; return stored_item;
    };

    // Grid: place, swap one, refuse two, stay in bounds, no cube in the cube.
    std::vector<Item> items = { stored("buc", 1, 0, 0), stored("buc", 1, 2, 0) };
    std::optional<Item> held;
    assert(pick_up(items, held, 0) && held->code == "buc" && items.size() == 1);
    assert(!pick_up(items, held, 0));                                            // one at a time
    assert(put_in_grid(cursor_tables, items, held, 1, 10, 4, 4, 2) && !held && items.back().column == 4 && items.back().row == 2);
    held = item("2hs");
    assert(!put_in_grid(cursor_tables, items, held, 1, 10, 4, 3, 0));                       // would cover two items
    assert(!put_in_grid(cursor_tables, items, held, 1, 10, 4, 9, 0));                       // off the right edge
    assert(put_in_grid(cursor_tables, items, held, 1, 10, 4, 4, 0) && held && held->code == "buc");   // swaps with (4,2)
    held = item("box");
    assert(!put_in_grid(cursor_tables, items, held, 4, 3, 4, 0, 0) && put_in_grid(cursor_tables, items, held, 5, 6, 8, 0, 0));

    // Equipping: slot, class and requirement checks, swaps.
    std::vector<Item> empty;
    held = item("cap");
    assert(!equip(cursor_tables, empty, held, 3, sorc));                                     // not a torso item
    assert(!equip(cursor_tables, empty, held, 1, Wearer{ .cls = d2d::d2s::kSorceress, .lvl = 4 }));             // level 5 needed
    assert(equip(cursor_tables, empty, held, 1, sorc) && !held && item_at(empty, 1, 1));
    held = item("ssd");
    assert(!equip(cursor_tables, empty, held, 4, Wearer{ .cls = d2d::d2s::kSorceress, .str = 10 }));            // 25 strength needed
    assert(equip(cursor_tables, empty, held, 4, sorc));
    held = item("buc");
    assert(equip(cursor_tables, empty, held, 5, sorc) && !held);                             // sword + shield
    held = item("am1");
    assert(!equip(cursor_tables, empty, held, 4, sorc));                                     // Amazon-only bow
    held = item("2ax");
    assert(!equip(cursor_tables, empty, held, 4, sorc));                                     // both hands full: two would come off
    std::erase_if(empty, [](const Item& x) { return x.code == "buc"; });
    assert(equip(cursor_tables, empty, held, 4, sorc) && held && held->code == "ssd");       // two-hander swaps the sword out
    held = item("buc");
    assert(equip(cursor_tables, empty, held, 5, sorc) && held && held->code == "2ax");       // a shield displaces the two-hander
    held.reset();
    std::vector<Item> dual;
    held = item("ssd");
    assert(equip(cursor_tables, dual, held, 4, sorc));
    held = item("ssd");
    assert(equip(cursor_tables, dual, held, 5, sorc) && held && item_at(dual, 1, 5) && !item_at(dual, 1, 4));   // no dual wield: swaps
    held = item("ssd");
    assert(equip(cursor_tables, dual, held, 4, barb) && !held && item_at(dual, 1, 4) && item_at(dual, 1, 5));    // a Barbarian dual-wields
    std::vector<Item> barb2h;
    held = item("2hs");
    assert(equip(cursor_tables, barb2h, held, 4, barb));
    held = item("buc");
    assert(equip(cursor_tables, barb2h, held, 5, barb) && !held);                        // 1or2handed: one hand for him
    std::vector<Item> archer;
    held = item("sbw");
    assert(equip(cursor_tables, archer, held, 4, sorc));
    held = item("aqv");
    assert(equip(cursor_tables, archer, held, 5, sorc) && !held);                        // a quiver goes with a bow

    // Belt: beltables only, swap in place.
    std::vector<Item> belt;
    held = item("cap");
    assert(!put_in_belt(cursor_tables, belt, held, 0, 4));
    held = item("hp1");
    assert(put_in_belt(cursor_tables, belt, held, 2, 4) && !held && belt[0].location == 2 && belt[0].column == 2);
    held = item("hp1");
    assert(put_in_belt(cursor_tables, belt, held, 2, 4) && held && belt.size() == 1);
    assert(!put_in_belt(cursor_tables, belt, held, 4, 4));                               // no such box

    // Stat points: vitality adds life and stamina, energy mana (quarters,
    // 8.8 fixed), never more than there are points.
    d2d::d2s::Stats spend_stats;
    spend_stats.values[d2d::d2s::kStatPts] = 5;
    spend_stats.values[d2d::d2s::kLife] = spend_stats.values[d2d::d2s::kMaxLife] = 50 << 8;
    const ClassGains amazon{ .life_per_vit = 12, .stamina_per_vit = 4, .mana_per_energy = 6 };
    assert(spend_stat_points(spend_stats, d2d::d2s::kVit, 2, amazon) == 2);
    assert(spend_stats.get(d2d::d2s::kVit) == 2 && spend_stats.fixed(d2d::d2s::kMaxLife) == 56 && spend_stats.fixed(d2d::d2s::kLife) == 56);
    assert(spend_stats.fixed(d2d::d2s::kMaxStamina) == 2);
    assert(spend_stat_points(spend_stats, d2d::d2s::kEne, 1, amazon) == 1 && spend_stats.get(d2d::d2s::kMaxMana) == 6 * 64);   // 1.5 mana
    assert(spend_stat_points(spend_stats, d2d::d2s::kStr, 10, amazon) == 2 && spend_stats.get(d2d::d2s::kStr) == 2);         // only 2 left
    assert(spend_stat_points(spend_stats, d2d::d2s::kDex, 1, amazon) == 0 && spend_stats.get(d2d::d2s::kStatPts) == 0);
    // Akara's reset undoes it all: the 5 points back, life / stamina /
    // mana to where they were, skills' points back.
    spend_stats.skills[3] = 4; spend_stats.skills[7] = 1;
    respec(spend_stats, { 0, 0, 0, 0 }, amazon);
    assert(spend_stats.get(d2d::d2s::kStatPts) == 5 && spend_stats.get(d2d::d2s::kStr) == 0 && spend_stats.get(d2d::d2s::kVit) == 0);
    assert(spend_stats.fixed(d2d::d2s::kMaxLife) == 50 && spend_stats.fixed(d2d::d2s::kLife) == 50 && spend_stats.get(d2d::d2s::kMaxMana) == 0);
    assert(spend_stats.get(d2d::d2s::kSkillPts) == 5 && spend_stats.skills[3] == 0);

    // Repair: missing / max of the base, rep mult; ethereal and whole items don't.
    Tables tables_copy = tables;
    tables_copy.npc_prices["Charsi"].rep = 512;
    Item worn = item("hax"); worn.max_durability = 40; worn.durability = 10;
    assert(repair_cost(tables_copy, worn, "Charsi", header) == 1000 * 30 / 40 / 2);
    Item whole = worn; whole.durability = 40;
    Item ghost = worn; ghost.ethereal = true;
    assert(repair_cost(tables_copy, whole, "Charsi", header) == 0 && repair_cost(tables_copy, ghost, "Charsi", header) == 0);
    Item tough = worn; tough.props = { { .stat = 73, .value = 10 } };        // +10 max: 50
    assert(max_durability(tough) == 50 && repair_cost(tables_copy, tough, "Charsi", header) == 1000 * 40 / 50 / 2);
    Item forever = worn; forever.props = { { .stat = 152, .value = 1 } };
    assert(repair_cost(tables_copy, forever, "Charsi", header) == 0);
    Store smith; smith.npc_id = "Charsi";
    d2d::d2s::Stats wallet; wallet.values[kGold] = 100; wallet.values[kGoldBank] = 300;
    assert(store_repair(tables_copy, smith, worn, wallet) && worn.durability == 40 && wallet.get(kGold) == 0 && wallet.get(kGoldBank) == 25);
    worn.durability = 0;
    assert(!store_repair(tables_copy, smith, worn, wallet) && worn.durability == 0);   // 500 > 25: can't pay

    // Healing raises life/mana to max, never lowers gear-boosted values.
    d2d::d2s::Stats hurt;
    hurt.values[d2d::d2s::kLife] = 10 << 8; hurt.values[d2d::d2s::kMaxLife] = 50 << 8;
    hurt.values[d2d::d2s::kMana] = 90 << 8; hurt.values[d2d::d2s::kMaxMana] = 60 << 8;
    heal(hurt);
    assert(hurt.fixed(d2d::d2s::kLife) == 50 && hurt.fixed(d2d::d2s::kMana) == 90);
    assert(is_healer(148) && !is_healer(154));                             // Akara yes, Charsi no

    // Skills: character level, prerequisites, max level, points.
    Tables skill_tables;
    skill_tables.class_skills[0] = { { .name = "a", .req_level = 1 }, { .name = "b", .req_level = 6, .req = { 0, -1, -1 } },
                          { .name = "c", .req_level = 1, .max_level = 1 } };
    std::array<std::uint8_t, 30> levels{};
    d2d::d2s::Stats skill_stats;
    skill_stats.values[d2d::d2s::kLevel] = 6;
    skill_stats.values[d2d::d2s::kSkillPts] = 3;
    assert(!can_learn(skill_tables, 0, 1, levels, 6));                                     // needs skill 0 first
    assert(learn_skill(skill_tables, 0, 0, levels, skill_stats) && levels[0] == 1 && skill_stats.get(d2d::d2s::kSkillPts) == 2);
    assert(!can_learn(skill_tables, 0, 1, levels, 5) && can_learn(skill_tables, 0, 1, levels, 6));       // level 6 needed
    assert(learn_skill(skill_tables, 0, 2, levels, skill_stats) && !learn_skill(skill_tables, 0, 2, levels, skill_stats));  // max level 1
    assert(learn_skill(skill_tables, 0, 1, levels, skill_stats) && skill_stats.get(d2d::d2s::kSkillPts) == 0);
    assert(!learn_skill(skill_tables, 0, 0, levels, skill_stats));                                  // out of points
    assert(!can_learn(skill_tables, 7, 0, levels, 99) && !can_learn(skill_tables, 0, 29, levels, 99));   // no such class / skill

    // Item generation.
    Tables gamble_tables = cursor_tables;                                                          // the cursor tables' types
    gamble_tables.item_base["cap"] = { .minac = 3, .maxac = 5, .cost = 100, .level = 1, .durability = 12, .gamble_cost = 3016,
                           .normcode = "cap", .ubercode = "xap", .ultracode = "uap" };
    gamble_tables.item_base["xap"] = { .cost = 500, .level = 22 };
    gamble_tables.item_base["uap"] = { .cost = 900, .level = 52 };
    gamble_tables.item_base["rin"] = { .gamble_cost = 50000 };
    gamble_tables.properties["str"] = { { .func = 1, .stat = 0 } };
    gamble_tables.properties["res-all"] = { { .func = 1, .stat = 39 }, { .func = 3, .stat = 43 } };
    gamble_tables.properties["skilltab"] = { { .func = 10, .stat = 188 } };
    gamble_tables.properties["hit-skill"] = { { .func = 11, .stat = 198 } };
    gamble_tables.properties["dmg%"] = { { .func = 7 } };
    gamble_tables.skill_id["Frost Nova"] = 44;
    gamble_tables.prefixes = { {}, { .name = "Hard", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .rare = true,
                         .itypes = { "helm" }, .mods = { { "str", "", 5, 5 } } },
                   { .name = "Hard2", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .rare = true,
                     .itypes = { "helm" }, .mods = { { "str", "", 7, 7 } } },
                   { .name = "Late", .level = 50, .group = 2, .frequency = 1, .spawnable = true, .itypes = { "helm" } } };
    gamble_tables.suffixes = { {}, { .name = "of Res", .level = 1, .group = 3, .frequency = 1, .spawnable = true, .rare = true,
                         .itypes = { "helm" }, .mods = { { "res-all", "", 10, 10 } } } };
    gamble_tables.uniques = { { .code = "cap", .level = 1, .mods = { { "skilltab", "4", 2, 2 }, { "hit-skill", "Frost Nova", 10, 3 } } } };
    gamble_tables.rare_prefixes = 10; gamble_tables.rare_suffixes = 10;
    Rng roll{ 7 };

    assert(affix_level(10, 1) == 10 && affix_level(99, 60) == 99 && affix_level(80, 60) == 61);
    // Groups: after one group-1 prefix no other group-1 one fits; level 50 is too high.
    assert(pick_affix(gamble_tables, gamble_tables.prefixes, "helm", 10, false, { 1 }, roll) == 0);
    for (int i = 0; i < 20; ++i) {
        const auto magic_cap = generate_item(gamble_tables, "cap", 10, 4, roll);
        assert(magic_cap.quality == 4 && magic_cap.identified && magic_cap.defense >= 3 && magic_cap.defense <= 5 && magic_cap.durability == 12);
        assert((magic_cap.prefix == 0 || magic_cap.prefix == 1 || magic_cap.prefix == 2) && (magic_cap.suffix == 0 || magic_cap.suffix == 1));
        assert(magic_cap.prefix || magic_cap.suffix);
    }
    const auto rare = generate_item(gamble_tables, "cap", 10, 6, roll);
    int pre = 0, suf = 0;
    for (int j = 0; j < 6; ++j) (j % 2 ? suf : pre) += rare.affixes[std::size_t(j)] != 0;
    assert(rare.quality == 6 && pre == 1 && suf == 1 && rare.rare1 >= 156 && rare.rare2 >= 1);   // one per group
    const auto uni = generate_item(gamble_tables, "cap", 10, 7, roll);
    assert(uni.quality == 7 && uni.unique_id == 0 && uni.props.size() == 2);
    assert(uni.props[0].stat == 188 && uni.props[0].param == (1 << 3 | 1) && uni.props[0].value == 2);   // sorc tab 1
    assert(uni.props[1].stat == 198 && uni.props[1].param == (3 | 44 << 6) && uni.props[1].value == 10);
    assert(generate_item(gamble_tables, "cap", 10, 5, roll).quality == 6);              // no set cap: rare instead
    const auto rare_cap = generate_item(gamble_tables, "cap", 10, 6, roll);
    for (const auto& prop : rare_cap.props) if (prop.stat == 43) assert(prop.value == 10); // res-all: func 3 repeats the value
    {   // apply_mod: min == max draws nothing; charges (FUN_0065f6a0) at a level off ilvl and reqlevel
        gamble_tables.properties["charged"] = { { .func = 19, .stat = 204 } };
        gamble_tables.skill_levels.assign(45, { 1, 20 });
        gamble_tables.skill_levels[44] = { 6, 20 };
        std::vector<d2d::d2s::ItemProp> got;
        Rng still{ 9 };
        apply_mod(gamble_tables, { "str", "", 5, 5 }, got, still);
        assert(got.size() == 1 && got[0].value == 5 && still.low == 9 && still.high == 666);
        got.clear();
        apply_mod(gamble_tables, { "charged", "Frost Nova", -20, 0 }, got, still);   // no item: ilvl 1, level 1, full
        assert(got.size() == 1 && got[0].param == (44 << 6 | 1) && got[0].value == 22 + 22 * 256 && still.low == 9);
        got.clear();
        d2d::d2s::Item wand;
        wand.ilvl = 30;
        const ModItem on{ &wand, &gamble_tables.item_base["cap"], 1, false, false };
        apply_mod(gamble_tables, { "charged", "Frost Nova", -20, 0 }, got, still, &on);   // level (30 - 6) / 4 + 1 = 7
        const int charges = 20 + 20 * 7 / 8;
        assert(got.size() == 1 && got[0].param == (44 << 6 | 7) && got[0].value >> 8 == charges);
        assert((got[0].value & 0xff) > charges / 8 && (got[0].value & 0xff) <= charges);
    }

    // Gambling: rings cost their gamble cost; upgrade odds grow with level.
    assert(gamble_price(gamble_tables, "rin", 50) == 50000);
    assert(gamble_upgrade(gamble_tables, "cap", 10) == std::pair(0, 0));
    assert(gamble_upgrade(gamble_tables, "cap", 30) == std::pair(401, 0));             // (30-22)*100/2+1
    assert(gamble_price(gamble_tables, "cap", 1) == ((0 - 0 + 5) * 250 / 3 + 100) * (11 / 3 + 20) / 15);
    for (int i = 0; i < 50; ++i) {
        const auto gambled = gamble_item(gamble_tables, "cap", 30, 0, roll);
        assert(gambled.quality >= 4 && gambled.quality <= 7 && (gambled.code == "cap" || gambled.code == "xap"));
    }

    gamble_tables.gamble = { "cap", "rin" };
    gamble_tables.item_base["rin"].level = 1;
    gamble_tables.item_info["rin"] = { .width = 1, .height = 1, .type = "ring" };
    Store gam = open_gamble(gamble_tables, "Gheed", 10);
    assert(gam.gamble && gam.tabs[0].size() == 1 && gam.tabs[3].size() == 1);
    std::vector<Item> bag;
    d2d::d2s::Stats purse;
    purse.values[kLevel] = 10; purse.values[kGold] = 100; purse.values[kGoldBank] = 100000;
    gam.tab = 3;
    assert(store_gamble(gamble_tables, gam, 0, bag, purse, roll) && bag.size() == 1 && bag[0].code == "rin");
    assert(purse.get(kGold) == 0 && purse.get(kGoldBank) == 100000 - 49900 && gam.tabs[3].size() == 1);
    assert(store_gamble(gamble_tables, gam, 0, bag, purse, roll) && purse.get(kGoldBank) == 100);   // 50100 pays once more
    assert(!store_gamble(gamble_tables, gam, 0, bag, purse, roll) && bag.size() == 2);             // then it can't
    // Pathing: round a wall; up to it when the goal is walled off.
    auto wall = [](int x, int y) { return x == 5 && y >= -10 && y <= 10; };   // a wall at x = 5
    const auto round = find_path(0, 0, 10, 0, wall);
    assert(!round.empty() && round.back() == std::pair(10, 0));
    for (const auto& [x, y] : round) assert(!wall(x, y));
    assert(round.size() > 10);                                             // had to detour
    auto boxed = [](int x, int y) { return std::abs(x - 20) <= 2 && std::abs(y) <= 2 && !(x == 20 && y == 0); };
    const auto closest = find_path(0, 0, 20, 0, boxed);                      // (20,0) is sealed in
    assert(!closest.empty() && closest.back() == std::pair(17, 0));
    assert(find_path(3, 3, 3, 3, wall).empty());

    // Mercenaries: the hire list's offers and hiring.
    Tables merc_tables;
    merc_tables.hirelings = { { .version = 100, .id = 0, .act = 1, .difficulty = 1, .level = 3, .gold = 100, .exp_per_level = 100,
                      .hit_points = 45, .hp_per_level = 9, .def = 15, .def_per_level = 8, .str = 35, .str_per_level = 10,
                      .dex = 45, .dex_per_level = 16, .dmg_min = 1, .dmg_max = 3, .dmg_per_level = 5, .names = 41 },
                    { .version = 100, .id = 0, .act = 1, .difficulty = 1, .level = 36, .gold = 100 },   // other Level: skipped
                    { .version = 0, .id = 0, .act = 1, .difficulty = 1, .level = 3, .gold = 7 } };     // classic row
    for (int i = 0; i < 20; ++i) {
        const auto offer = merc_offer(merc_tables, true, 0, 0, 20, roll);
        assert(offer && offer->id == 0 && offer->level >= 15 && offer->level <= 19 && offer->name >= 0 && offer->name < 41);
        const int level_delta = offer->level - 3;
        assert(offer->life == 45 + 9 * level_delta && offer->cost == 100 * (level_delta * 15 + 100) / 100 && offer->def == 15 + 8 * level_delta);
        assert(offer->str == 35 + (10 * level_delta >> 3) && offer->exp == std::uint32_t((offer->level + 1) * 100 * offer->level * offer->level));
    }
    assert(merc_offer(merc_tables, true, 0, 0, 1, roll)->level == 2);                  // never below 2
    assert(!merc_offer(merc_tables, true, 1, 0, 20, roll));                           // no act 2 rows
    d2d::d2s::Header merc_header;
    d2d::d2s::Stats wallet2;
    wallet2.values[kGold] = 50; wallet2.values[kGoldBank] = 100;
    const MercOffer offer{ .id = 7, .level = 20, .cost = 120, .exp = 999, .seed = 5, .name = 3 };
    assert(hire(offer, merc_header, wallet2) && merc_header.merc_type == 7 && merc_header.merc_seed == 5 && merc_header.merc_name == 3 && merc_header.merc_exp == 999);
    assert(wallet2.get(kGold) == 0 && wallet2.get(kGoldBank) == 30 && !hire(offer, merc_header, wallet2));

    // Identify: carried, worn and belt items; not the stash.
    std::vector<Item> unid = { stored("cap", 1, 0, 0), stored("cap", 5, 0, 0), item("cap") };
    unid[2].location = 1;
    assert(unidentified(unid) == 2 && identify_all(unid) == 2 && unidentified(unid) == 0 && !unid[1].identified);

    // D2's seed: low * 0x6AC690C5 + high, high starting at 666.
    Rng rng2{ 1 };
    assert(rng2.next() == 0x6AC6935Fu && rng2.high == 0);
    Rng rng_a{ 5 }, rng_b{ 5 };
    assert(rng_a(8) == int(rng_b.next() & 7));                                    // power of two: mask
    Rng rng_c{ 5 }, rng_d{ 5 };
    assert(rng_c(10) == int(rng_d.next() % 10) && Rng{}(0) == 0);                 // else modulo; < 1 -> 0

    // Belt potions: the bottom one goes, the column drops a row.
    Tables potion_tables;
    potion_tables.potions["hp1"] = { .life = 30, .ticks = 192 };
    auto in_belt = [](const char* code, int box) { d2d::d2s::Item belt_item; belt_item.code = code; belt_item.location = 2; belt_item.column = box; return belt_item; };
    std::vector<d2d::d2s::Item> belt_items{ in_belt("hp1", 1), in_belt("hp1", 5), in_belt("isc", 2) };
    assert(drink_belt(potion_tables, belt_items, 1) == "hp1" && belt_items.size() == 2 && belt_items[0].column == 1);
    assert(drink_belt(potion_tables, belt_items, 2).empty() && drink_belt(potion_tables, belt_items, 0).empty() && belt_items.size() == 2);   // a scroll, nothing

    // A potion's amount: calc << 8, the class's bonus, no roll at 0 vitality.
    Rng potion_rng{ 9 };
    assert(potion_amount(30, 0, true, 0, potion_rng) == 11520 && potion_amount(30, 4, true, 0, potion_rng) == 15360);   // Amazon x1.5, Barbarian x2
    assert(potion_amount(30, 1, true, 0, potion_rng) == 7680 && potion_amount(20, 1, false, 0, potion_rng) == 10240);   // Sorceress: life x1, mana x2
    assert(potion_amount(20, 4, false, 0, potion_rng) == 5120 && potion_amount(20, 6, false, 0, potion_rng) == 7680);
    Rng roll_a{ 9 }, roll_b{ 9 };
    const int half = roll_b(50) >> 1;
    assert(potion_amount(30, 1, true, 50, roll_a) == (roll_b(100) < half ? 15360 : 7680));                        // rand(vit) / 2 > rand(100): doubled
    // The state over what's left: hp1 alone 7680 / 192 a frame; another
    // with 96 frames left: (40 x 96 + 7680) / (96 + 192).
    assert(potion_rate(0, 0, 7680, 192) == 40 && potion_rate(40, 96, 7680, 192) == 40 && potion_rate(40, 96, 15360, 160) == 75);

    // Picking up: hp potions to their column, then a free one (autobelt);
    // scrolls to their tome, else the inventory; keys onto their stack.
    Tables pick_tables;
    pick_tables.types["hpot"].beltable = pick_tables.types["mpot"].beltable = pick_tables.types["scro"].beltable = true;
    pick_tables.types["key"].autostack = true;
    for (const char* code : { "hp1", "hp2", "mp1" }) { pick_tables.item_base[code].autobelt = true; pick_tables.item_info[code].type = code[0] == 'h' ? "hpot" : "mpot"; }
    pick_tables.item_info["isc"].type = "scro"; pick_tables.item_info["ibk"].type = "book"; pick_tables.item_base["ibk"].max_stack = 20;
    pick_tables.item_info["key"].type = "key"; pick_tables.item_base["key"].stackable = true; pick_tables.item_base["key"].max_stack = 12;
    auto ground_item = [](const char* code, int quantity) { d2d::d2s::Item picked; picked.code = code; picked.quantity = quantity; picked.location = 3; return picked; };
    std::vector<d2d::d2s::Item> carried{ in_belt("hp1", 2), in_belt("mp1", 0) };
    auto picked = ground_item("hp2", -1);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 8) == Pickup::kGone && carried.back().location == 2 && carried.back().column == 6);   // hp1's column, a row up
    picked = ground_item("hp2", -1);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 4) == Pickup::kGone && carried.back().column == 1);   // one row: hp's full, the first free column
    picked = ground_item("isc", -1);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 4) == Pickup::kGone && carried.back().location == 0 && carried.back().panel == 1);   // no tome: not the belt
    carried.push_back(stored("ibk", 1, 2, 0)); carried.back().quantity = 19;
    picked = ground_item("isc", -1);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 4) == Pickup::kGone && carried.back().quantity == 20);
    carried.back().quantity = 15;
    picked = ground_item("ibk", 9);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 4) == Pickup::kStays && carried.back().quantity == 20 && picked.quantity == 4);   // the rest stays on the ground
    carried.push_back(stored("key", 1, 4, 0)); carried.back().quantity = 10;
    picked = ground_item("key", 5);
    assert(pick_up(pick_tables, carried, picked, 10, 4, 4) == Pickup::kGone && carried[carried.size() - 2].quantity == 12 && carried.back().quantity == 3);
    picked = ground_item("key", 10);
    assert(pick_up(pick_tables, carried, picked, 1, 1, 4) == Pickup::kNoRoom && carried.back().quantity == 12 && picked.quantity == 1);   // the stack fills, the rest has no room

    std::puts("test_rules: ok");
}
