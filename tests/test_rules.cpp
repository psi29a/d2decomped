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

    // The item cursor.
    Tables c;
    c.types["weap"] = {};
    c.types["mele"] = { .equiv = { "weap", "" } };
    c.types["swor"] = { .equiv = { "mele", "" }, .body = { 4, 5 } };
    c.types["axe"]  = { .equiv = { "mele", "" }, .body = { 4, 5 } };
    c.types["shld"] = {};
    c.types["shie"] = { .equiv = { "shld", "" }, .body = { 4, 5 } };
    c.types["misl"] = {};
    c.types["bowq"] = { .equiv = { "misl", "" }, .body = { 4, 5 } };
    c.types["bow"]  = { .equiv = { "weap", "" }, .body = { 4, 5 } };
    c.types["clas"] = {};
    c.types["amaz"] = { .equiv = { "clas", "" }, .cls = "ama" };
    c.types["abow"] = { .equiv = { "bow", "amaz" }, .body = { 4, 5 } };
    c.types["helm"] = { .body = { 1, 1 } };
    c.types["poti"] = { .beltable = true };
    c.types["hpot"] = { .equiv = { "poti", "" } };
    c.item_info["ssd"] = { .w = 1, .h = 3, .type = "swor", .kind = 2, .req_str = 25 };
    c.item_info["2hs"] = { .w = 2, .h = 4, .type = "swor", .kind = 2, .two_handed = true, .one_or_two = true };
    c.item_info["2ax"] = { .w = 2, .h = 3, .type = "axe", .kind = 2, .two_handed = true };
    c.item_info["buc"] = { .w = 2, .h = 2, .type = "shie", .kind = 1 };
    c.item_info["aqv"] = { .w = 1, .h = 3, .type = "bowq" };
    c.item_info["sbw"] = { .w = 2, .h = 3, .type = "bow", .kind = 2, .two_handed = true };
    c.item_info["am1"] = { .w = 2, .h = 4, .type = "abow", .kind = 2, .two_handed = true };
    c.item_info["cap"] = { .w = 2, .h = 2, .type = "helm", .kind = 1, .req_lvl = 5 };
    c.item_info["hp1"] = { .type = "hpot" };
    c.item_info["box"] = { .w = 2, .h = 2 };
    const Wearer sorc{ .cls = 1, .str = 30, .dex = 30, .lvl = 10 };
    const Wearer barb{ .cls = 4, .str = 30, .dex = 30, .lvl = 10 };
    auto at = [](const std::vector<Item>& v, int loc, int slot) -> const Item* {
        for (const auto& x : v) if (x.location == loc && x.slot == slot) return &x;
        return nullptr;
    };
    auto stored = [](const char* code, int panel, int col, int row) {
        Item it = item(code); it.location = 0; it.panel = panel; it.column = col; it.row = row; return it;
    };

    // Grid: place, swap one, refuse two, stay in bounds, no cube in the cube.
    std::vector<Item> v = { stored("buc", 1, 0, 0), stored("buc", 1, 2, 0) };
    std::optional<Item> held;
    assert(pick_up(v, held, 0) && held->code == "buc" && v.size() == 1);
    assert(!pick_up(v, held, 0));                                            // one at a time
    assert(put_in_grid(c, v, held, 1, 10, 4, 4, 2) && !held && v.back().column == 4 && v.back().row == 2);
    held = item("2hs");
    assert(!put_in_grid(c, v, held, 1, 10, 4, 3, 0));                       // would cover two items
    assert(!put_in_grid(c, v, held, 1, 10, 4, 9, 0));                       // off the right edge
    assert(put_in_grid(c, v, held, 1, 10, 4, 4, 0) && held && held->code == "buc");   // swaps with (4,2)
    held = item("box");
    assert(!put_in_grid(c, v, held, 4, 3, 4, 0, 0) && put_in_grid(c, v, held, 5, 6, 8, 0, 0));

    // Equipping: slot, class and requirement checks, swaps.
    std::vector<Item> e;
    held = item("cap");
    assert(!equip(c, e, held, 3, sorc));                                     // not a torso item
    assert(!equip(c, e, held, 1, Wearer{ .cls = 1, .lvl = 4 }));             // level 5 needed
    assert(equip(c, e, held, 1, sorc) && !held && at(e, 1, 1));
    held = item("ssd");
    assert(!equip(c, e, held, 4, Wearer{ .cls = 1, .str = 10 }));            // 25 strength needed
    assert(equip(c, e, held, 4, sorc));
    held = item("buc");
    assert(equip(c, e, held, 5, sorc) && !held);                             // sword + shield
    held = item("am1");
    assert(!equip(c, e, held, 4, sorc));                                     // Amazon-only bow
    held = item("2ax");
    assert(!equip(c, e, held, 4, sorc));                                     // both hands full: two would come off
    std::erase_if(e, [](const Item& x) { return x.code == "buc"; });
    assert(equip(c, e, held, 4, sorc) && held && held->code == "ssd");       // two-hander swaps the sword out
    held = item("buc");
    assert(equip(c, e, held, 5, sorc) && held && held->code == "2ax");       // a shield displaces the two-hander
    held.reset();
    std::vector<Item> dual;
    held = item("ssd");
    assert(equip(c, dual, held, 4, sorc));
    held = item("ssd");
    assert(equip(c, dual, held, 5, sorc) && held && at(dual, 1, 5) && !at(dual, 1, 4));   // no dual wield: swaps
    held = item("ssd");
    assert(equip(c, dual, held, 4, barb) && !held && at(dual, 1, 4) && at(dual, 1, 5));    // a Barbarian dual-wields
    std::vector<Item> barb2h;
    held = item("2hs");
    assert(equip(c, barb2h, held, 4, barb));
    held = item("buc");
    assert(equip(c, barb2h, held, 5, barb) && !held);                        // 1or2handed: one hand for him
    std::vector<Item> archer;
    held = item("sbw");
    assert(equip(c, archer, held, 4, sorc));
    held = item("aqv");
    assert(equip(c, archer, held, 5, sorc) && !held);                        // a quiver goes with a bow

    // Belt: beltables only, swap in place.
    std::vector<Item> belt;
    held = item("cap");
    assert(!put_in_belt(c, belt, held, 0, 4));
    held = item("hp1");
    assert(put_in_belt(c, belt, held, 2, 4) && !held && belt[0].location == 2 && belt[0].column == 2);
    held = item("hp1");
    assert(put_in_belt(c, belt, held, 2, 4) && held && belt.size() == 1);
    assert(!put_in_belt(c, belt, held, 4, 4));                               // no such box

    // Stat points: vitality adds life and stamina, energy mana (quarters,
    // 8.8 fixed), never more than there are points.
    d2d::d2s::Stats ps;
    ps.v[d2d::d2s::kStatPts] = 5;
    ps.v[d2d::d2s::kLife] = ps.v[d2d::d2s::kMaxLife] = 50 << 8;
    const ClassGains amazon{ .life_per_vit = 12, .stamina_per_vit = 4, .mana_per_energy = 6 };
    assert(spend_stat_points(ps, d2d::d2s::kVit, 2, amazon) == 2);
    assert(ps.get(d2d::d2s::kVit) == 2 && ps.fixed(d2d::d2s::kMaxLife) == 56 && ps.fixed(d2d::d2s::kLife) == 56);
    assert(ps.fixed(d2d::d2s::kMaxStamina) == 2);
    assert(spend_stat_points(ps, d2d::d2s::kEne, 1, amazon) == 1 && ps.get(d2d::d2s::kMaxMana) == 6 * 64);   // 1.5 mana
    assert(spend_stat_points(ps, d2d::d2s::kStr, 10, amazon) == 2 && ps.get(d2d::d2s::kStr) == 2);         // only 2 left
    assert(spend_stat_points(ps, d2d::d2s::kDex, 1, amazon) == 0 && ps.get(d2d::d2s::kStatPts) == 0);

    // Repair: missing / max of the base, rep mult; ethereal and whole items don't.
    Tables r = t;
    r.npc_prices["Charsi"].rep = 512;
    Item worn = item("hax"); worn.max_durability = 40; worn.durability = 10;
    assert(repair_cost(r, worn, "Charsi", h) == 1000 * 30 / 40 / 2);
    Item whole = worn; whole.durability = 40;
    Item ghost = worn; ghost.ethereal = true;
    assert(repair_cost(r, whole, "Charsi", h) == 0 && repair_cost(r, ghost, "Charsi", h) == 0);
    Item tough = worn; tough.props = { { .stat = 73, .value = 10 } };        // +10 max: 50
    assert(max_durability(tough) == 50 && repair_cost(r, tough, "Charsi", h) == 1000 * 40 / 50 / 2);
    Item forever = worn; forever.props = { { .stat = 152, .value = 1 } };
    assert(repair_cost(r, forever, "Charsi", h) == 0);
    Store smith; smith.npc_id = "Charsi";
    d2d::d2s::Stats wallet; wallet.v[kGold] = 100; wallet.v[kGoldBank] = 300;
    assert(store_repair(r, smith, worn, wallet) && worn.durability == 40 && wallet.get(kGold) == 0 && wallet.get(kGoldBank) == 25);
    worn.durability = 0;
    assert(!store_repair(r, smith, worn, wallet) && worn.durability == 0);   // 500 > 25: can't pay

    // Healing raises life/mana to max, never lowers gear-boosted values.
    d2d::d2s::Stats hurt;
    hurt.v[d2d::d2s::kLife] = 10 << 8; hurt.v[d2d::d2s::kMaxLife] = 50 << 8;
    hurt.v[d2d::d2s::kMana] = 90 << 8; hurt.v[d2d::d2s::kMaxMana] = 60 << 8;
    heal(hurt);
    assert(hurt.fixed(d2d::d2s::kLife) == 50 && hurt.fixed(d2d::d2s::kMana) == 90);
    assert(is_healer(148) && !is_healer(154));                             // Akara yes, Charsi no

    // Skills: character level, prerequisites, max level, points.
    Tables k;
    k.class_skills[0] = { { .name = "a", .req_level = 1 }, { .name = "b", .req_level = 6, .req = { 0, -1, -1 } },
                          { .name = "c", .req_level = 1, .max_level = 1 } };
    std::array<std::uint8_t, 30> lv{};
    d2d::d2s::Stats sk;
    sk.v[d2d::d2s::kLevel] = 6;
    sk.v[d2d::d2s::kSkillPts] = 3;
    assert(!can_learn(k, 0, 1, lv, 6));                                     // needs skill 0 first
    assert(learn_skill(k, 0, 0, lv, sk) && lv[0] == 1 && sk.get(d2d::d2s::kSkillPts) == 2);
    assert(!can_learn(k, 0, 1, lv, 5) && can_learn(k, 0, 1, lv, 6));       // level 6 needed
    assert(learn_skill(k, 0, 2, lv, sk) && !learn_skill(k, 0, 2, lv, sk));  // max level 1
    assert(learn_skill(k, 0, 1, lv, sk) && sk.get(d2d::d2s::kSkillPts) == 0);
    assert(!learn_skill(k, 0, 0, lv, sk));                                  // out of points
    assert(!can_learn(k, 7, 0, lv, 99) && !can_learn(k, 0, 29, lv, 99));   // no such class / skill

    // Item generation.
    Tables g = c;                                                          // the cursor tables' types
    g.item_base["cap"] = { .minac = 3, .maxac = 5, .cost = 100, .level = 1, .durability = 12, .gamble_cost = 3016,
                           .normcode = "cap", .ubercode = "xap", .ultracode = "uap" };
    g.item_base["xap"] = { .cost = 500, .level = 22 };
    g.item_base["uap"] = { .cost = 900, .level = 52 };
    g.item_base["rin"] = { .gamble_cost = 50000 };
    g.properties["str"] = { { .func = 1, .stat = 0 } };
    g.properties["res-all"] = { { .func = 1, .stat = 39 }, { .func = 3, .stat = 43 } };
    g.properties["skilltab"] = { { .func = 10, .stat = 188 } };
    g.properties["hit-skill"] = { { .func = 11, .stat = 198 } };
    g.properties["dmg%"] = { { .func = 7 } };
    g.skill_id["Frost Nova"] = 44;
    g.prefixes = { {}, { .name = "Hard", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .rare = true,
                         .itypes = { "helm" }, .mods = { { "str", "", 5, 5 } } },
                   { .name = "Hard2", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .rare = true,
                     .itypes = { "helm" }, .mods = { { "str", "", 7, 7 } } },
                   { .name = "Late", .level = 50, .group = 2, .frequency = 1, .spawnable = true, .itypes = { "helm" } } };
    g.suffixes = { {}, { .name = "of Res", .level = 1, .group = 3, .frequency = 1, .spawnable = true, .rare = true,
                         .itypes = { "helm" }, .mods = { { "res-all", "", 10, 10 } } } };
    g.uniques = { { .code = "cap", .level = 1, .mods = { { "skilltab", "4", 2, 2 }, { "hit-skill", "Frost Nova", 10, 3 } } } };
    g.rare_prefixes = 10; g.rare_suffixes = 10;
    Rng roll{ 7 };

    assert(affix_level(10, 1) == 10 && affix_level(99, 60) == 99 && affix_level(80, 60) == 61);
    // Groups: after one group-1 prefix no other group-1 one fits; level 50 is too high.
    assert(pick_affix(g, g.prefixes, "helm", 10, false, { 1 }, roll) == 0);
    for (int i = 0; i < 20; ++i) {
        const auto m = generate_item(g, "cap", 10, 4, roll);
        assert(m.quality == 4 && m.identified && m.defense >= 3 && m.defense <= 5 && m.durability == 12);
        assert((m.prefix == 0 || m.prefix == 1 || m.prefix == 2) && (m.suffix == 0 || m.suffix == 1));
        assert(m.prefix || m.suffix);
    }
    const auto rare = generate_item(g, "cap", 10, 6, roll);
    int pre = 0, suf = 0;
    for (int k = 0; k < 6; ++k) (k % 2 ? suf : pre) += rare.affixes[std::size_t(k)] != 0;
    assert(rare.quality == 6 && pre == 1 && suf == 1 && rare.rare1 >= 156 && rare.rare2 >= 1);   // one per group
    const auto uni = generate_item(g, "cap", 10, 7, roll);
    assert(uni.quality == 7 && uni.unique_id == 0 && uni.props.size() == 2);
    assert(uni.props[0].stat == 188 && uni.props[0].param == (1 << 3 | 1) && uni.props[0].value == 2);   // sorc tab 1
    assert(uni.props[1].stat == 198 && uni.props[1].param == (3 | 44 << 6) && uni.props[1].value == 10);
    assert(generate_item(g, "cap", 10, 5, roll).quality == 6);              // no set cap: rare instead
    const auto r2 = generate_item(g, "cap", 10, 6, roll);
    for (const auto& p : r2.props) if (p.stat == 43) assert(p.value == 10); // res-all: func 3 repeats the value

    // Gambling: rings cost their gamble cost; upgrade odds grow with level.
    assert(gamble_price(g, "rin", 50) == 50000);
    assert(gamble_upgrade(g, "cap", 10) == std::pair(0, 0));
    assert(gamble_upgrade(g, "cap", 30) == std::pair(401, 0));             // (30-22)*100/2+1
    assert(gamble_price(g, "cap", 1) == ((0 - 0 + 5) * 250 / 3 + 100) * (11 / 3 + 20) / 15);
    for (int i = 0; i < 50; ++i) {
        const auto gi = gamble_item(g, "cap", 30, 0, roll);
        assert(gi.quality >= 4 && gi.quality <= 7 && (gi.code == "cap" || gi.code == "xap"));
    }

    g.gamble = { "cap", "rin" };
    g.item_base["rin"].level = 1;
    g.item_info["rin"] = { .w = 1, .h = 1, .type = "ring" };
    Store gam = open_gamble(g, "Gheed", 10);
    assert(gam.gamble && gam.tabs[0].size() == 1 && gam.tabs[3].size() == 1);
    std::vector<Item> bag;
    d2d::d2s::Stats purse;
    purse.v[kLevel] = 10; purse.v[kGold] = 100; purse.v[kGoldBank] = 100000;
    gam.tab = 3;
    assert(store_gamble(g, gam, 0, bag, purse, roll) && bag.size() == 1 && bag[0].code == "rin");
    assert(purse.get(kGold) == 0 && purse.get(kGoldBank) == 100000 - 49900 && gam.tabs[3].size() == 1);
    assert(store_gamble(g, gam, 0, bag, purse, roll) && purse.get(kGoldBank) == 100);   // 50100 pays once more
    assert(!store_gamble(g, gam, 0, bag, purse, roll) && bag.size() == 2);             // then it can't
    // Pathing: round a wall; up to it when the goal is walled off.
    auto wall = [](int x, int y) { return x == 5 && y >= -10 && y <= 10; };   // a wall at x = 5
    const auto round = find_path(0, 0, 10, 0, wall);
    assert(!round.empty() && round.back() == std::pair(10, 0));
    for (const auto& [x, y] : round) assert(!wall(x, y));
    assert(round.size() > 10);                                             // had to detour
    auto boxed = [](int x, int y) { return std::abs(x - 20) <= 2 && std::abs(y) <= 2 && !(x == 20 && y == 0); };
    const auto near = find_path(0, 0, 20, 0, boxed);                      // (20,0) is sealed in
    assert(!near.empty() && near.back() == std::pair(17, 0));
    assert(find_path(3, 3, 3, 3, wall).empty());

    std::puts("test_rules: ok");
}
