// Drops over hand-made tables: auto weapN classes, gold (and its
// multiplier), NoDrop odds and players, quality rolls (rings at least
// magic, potions plain), the 6-item cap, TC upgrades.
#include <d2s_items.hpp>
#include <drops.hpp>
#include <rules.hpp>
#include <shrines.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace d2d::rules;

int main() {
    Tables tables;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_info["big"] = { .type = "axe", .kind = 2 };
    tables.item_info["rin"] = { .type = "ring", .kind = 0 };
    tables.item_info["hp1"] = { .type = "hpot", .kind = 0 };
    tables.types["ring"].always_magic = true;
    tables.types["hpot"].always_normal = true;
    tables.types["axe"] = { .equiv = { "weap", "" }, .rarity = 3 };
    tables.types["weap"].treasure_class = true;
    tables.item_base["hax"] = { .level = 1, .normcode = "hax" };
    tables.item_base["big"] = { .level = 5 };
    tables.item_base["rin"].level = 1;
    tables.item_base["hp1"].level = 1;
    tables.quality_ratio[0] = { { { 400, 1, 6400 }, { 160, 2, 5600 }, { 100, 2, 3200 }, { 34, 3, 192 }, { 12, 8, 0 }, { 2, 2, 0 } } };
    d2d::rules::add_auto_treasure(tables, { "axe", "weap" }, { { "hax", "axe", "", 1 }, { "big", "axe", "", 5 }, { "old", "axe", "", 0 } });
    assert(tables.treasure.at("weap3").items.size() == 1 && tables.treasure.at("weap6").items[0].first == "big" && tables.treasure.at("weap6").items[0].second == 3);
    assert(tables.treasure.at("weap96").items.empty() && !tables.treasure.contains("axe3"));
    tables.treasure["Gold"] = { .items = { { "gld", 1 } } };
    tables.treasure["Rich"] = { .items = { { "gld,mul=1280", 1 } } };
    tables.treasure["Half"] = { .nodrop = 1, .items = { { "weap3", 1 } } };
    tables.treasure["Two"] = { .picks = 2, .items = { { "rin", 1 }, { "hp1", 1 } } };
    Rng rng{ 11 };
    int dropped = 0, magic = 0;
    for (int i = 0; i < 1000; ++i) {
        std::vector<Drop> out;
        roll_drops(tables, "Gold", 1, rng, out);
        assert(out.size() == 1 && out[0].code == "gld" && out[0].gold == 0 && out[0].mul == 0);
        const int coins = gold_amount(1, 0, rng);                          // ilvl + rand(5 ilvl)
        assert(coins >= 1 && coins <= 5);
        out.clear();
        roll_drops(tables, "Rich", 2, rng, out);
        const int rich = gold_amount(2, out[0].mul, rng);
        assert(out[0].mul == 1280 && rich >= 10 && rich <= 55);              // (2 + 0..9) x 5
        out.clear();
        roll_drops(tables, "Half", 1, rng, out);
        for (const auto& x : out) { assert(x.code == "hax" && x.quality >= 1 && x.quality <= 7); ++dropped; magic += x.quality >= 4; }
        out.clear();
        roll_drops(tables, "Two", 1, rng, out);
        assert(out.size() == 2);
        for (const auto& x : out) assert(x.code == "rin" ? x.quality >= 4 : x.quality == 2);   // rings magic+, potions plain
    }
    assert(dropped > 400 && dropped < 600 && magic > 0);
    // Quality (FUN_00558640): one draw per step off the dropper's seed; a
    // 1024 modifier makes the odds 0, so that quality wins without a draw.
    {
        Rng seed{ 3 }, copy = seed;
        assert(roll_quality(tables, "hax", 1, { 1024, 0, 0, 0 }, seed) == 7 && seed.low == copy.low);
        assert(roll_quality(tables, "hp1", 1, {}, seed) == 2 && seed.low == copy.low);
        roll_quality(tables, "rin", 1, {}, seed);
        copy.next(); copy.next(); copy.next();                              // unique, set, rare lost: magic
        assert(seed.low == copy.low);
    }
    // A forced quality (a chest round's, FUN_00585b90) replaces the roll.
    {
        std::vector<Drop> out;
        roll_drops(tables, "Two", 1, rng, out, 1, 0, 6, 4);
        assert(out.size() == 2 && out[0].quality == 4 && out[1].quality == 4);
    }
    // At most 6 items (FUN_0055a6d0's max) however many picks.
    tables.treasure["Lots"] = { .picks = 9, .items = { { "hp1", 1 } } };
    {
        std::vector<Drop> out;
        roll_drops(tables, "Lots", 1, rng, out);
        assert(out.size() == 6);
    }
    // More players: NoDrop 1 of 2 at /players 3 (n = 2) is 1 * 1/4 / (3/4)
    // = 0, so every pick drops.
    for (int i = 0; i < 50; ++i) {
        std::vector<Drop> out;
        roll_drops(tables, "Half", 1, rng, out, 3);
        assert(out.size() == 1);
    }
    // Levels move a class on within its group (FUN_00654e00).
    tables.treasure["G A"] = { .group = 7, .level = 1, .next = "G B" };
    tables.treasure["G B"] = { .group = 7, .level = 10, .next = "G C" };
    tables.treasure["G C"] = { .group = 7, .level = 20 };
    assert(tc_upgrade(tables, "G A", 0) == "G A" && tc_upgrade(tables, "G A", 9) == "G A" && tc_upgrade(tables, "G A", 10) == "G B");
    assert(tc_upgrade(tables, "G A", 99) == "G C" && tc_upgrade(tables, "G B", 5) == "G B");
    // Negative picks (the Countess): each entry in turn by its weight, no
    // NoDrop, never past the weights' total.
    tables.treasure["Countess"] = { .picks = -4, .nodrop = 5, .items = { { "Gold", 1 }, { "hp1", 2 }, { "rin", 1 } } };
    tables.treasure["Cpot"] = { .picks = -2, .items = { { "hp1", 1 } } };
    {
        std::vector<Drop> out;
        roll_drops(tables, "Countess", 1, rng, out);
        assert(out.size() == 4 && out[0].code == "gld" && out[1].code == "hp1" && out[2].code == "hp1" && out[3].code == "rin");
        out.clear();
        roll_drops(tables, "Cpot", 1, rng, out);
        assert(out.size() == 1);
    }
    // Chests: act 1's A below a third of the way from the Blood Moor (1)
    // to Catacombs 4 (11), B below two thirds, else C.
    assert(chest_tc(0, 0, 1, 1, 11) == "Act 1 Chest A" && chest_tc(0, 1, 4, 1, 11) == "Act 1 (N) Chest B");
    assert(chest_tc(0, 2, 7, 1, 11) == "Act 1 (H) Chest C");
    // Shrines: the exchanges come back as boosts; LevelMin holds rows back.
    std::vector<ShrineRow> rows(6);
    for (int row = 1; row < 6; ++row) rows[std::size_t(row)] = { .code = row, .effectclass = row == 4 ? 2 : row == 5 ? 3 : 4, .level_min = row == 3 ? 99 : 1 };
    rows[2].effectclass = 2;
    for (int i = 0; i < 200; ++i) {
        Rng object_rng{ std::uint32_t(i) };
        assert(roll_shrine(rows, 1, 2, object_rng) == 2);                                // health: 2, or 4 -> 2
        const int any = roll_shrine(rows, 0, 2, object_rng);
        assert(any >= 1 && any <= 3);
    }
    // Chests: traps and locks by the classic area level, a unit seed of
    // 1..0xfffe; a locked chest drops twice, others are empty a quarter of
    // the time; a sparkling one tries up to 11 rounds for a magic item.
    int traps = 0, locks = 0, empty = 0;
    for (int i = 0; i < 4000; ++i) {
        Rng chest_rng{ std::uint32_t(i) };
        const auto chest = roll_chest(1, true, chest_rng);                              // 5 % traps, 8 % locks at MonLvl1 1
        assert(chest.trap >= 0 && chest.trap <= 8 && chest.seed >= 1 && chest.seed <= 0xfffe);
        traps += chest.trap > 0; locks += chest.locked;
        int rounds = 0;
        open_container(4, 5, true, false, chest_rng, [&](int) { ++rounds; return 2; });
        assert(rounds == 2);
        rounds = 0;
        open_container(4, 5, false, false, chest_rng, [&](int) { ++rounds; return 2; });
        empty += rounds == 0;
        rounds = 0;
        open_container(4, 455, false, true, chest_rng, [&](int forced) { assert(forced == 4 || forced == 6); ++rounds; return 2; });
        assert(rounds == 11);
    }
    assert(traps > 120 && traps < 290 && locks > 220 && locks < 430 && empty > 850 && empty < 1150);
    Rng shelf_rng{ 1 };
    const auto shelf = open_container(26, 179, false, false, shelf_rng, [](int) { return 0; });
    assert(shelf.extra.size() == 1 && (shelf.extra[0] == "tsc" || shelf.extra[0] == "isc" || shelf.extra[0] == "tbk" || shelf.extra[0] == "ibk"));
    // Trap 8's undead: zombies (mummies in act 2) or a skeleton family by
    // its first id; else a flying scimitar, nothing in act 1.
    assert(trap_undead({ 19, 7, 2 }, 0) == 5);                           // fallen1, zombie3 first
    assert(trap_undead({ 19, 172 }, 0) == 170 && trap_undead({ 98 }, 1) == 96 && trap_undead({ 7 }, 1) == 234);
    assert(trap_undead({ 19 }, 0) == -1 && trap_undead({}, 2) == 234);
    // The gem shrine: a gem goes up; with none, a chipped one.
    tables.item_base["gcv"].better_gem = "gfv";
    tables.item_base["gpv"].better_gem = "non";
    std::vector<d2d::d2s::Item> inv(2);
    inv[0].code = "gpv"; inv[1].code = "gcv"; inv[0].panel = inv[1].panel = 1;
    Rng gold_rng{ 5 };
    assert(gem_shrine(tables, inv, gold_rng).empty() && inv[1].code == "gfv" && inv[0].code == "gpv");
    assert(gem_shrine(tables, inv, gold_rng).substr(0, 2) == "gc");
    // Doors (FUN_00581d40): closed opens; open closes, or sticks while occupied.
    assert(door_mode(0, true) == 2 && door_mode(2, false) == 0 && door_mode(2, true) == 5);
    assert(door_mode(5, true) == -1 && door_mode(5, false) == 0 && door_mode(1, false) == -1);
    assert(object_sound(15, 2) == "object_door_wood_open" && object_sound(13, 0) == "object_door_metal_close" && object_sound(15, 1).empty());
    // A well: half the maxima back, capped; nothing short, no drink.
    std::int64_t life = 10, mana = 100, stamina = 0;
    assert(well_drink(life, 100, mana, 100, stamina, 40) && life == 60 && mana == 100 && stamina == 20);
    life = 100; stamina = 40;
    assert(!well_drink(life, 100, mana, 100, stamina, 40));
    // A stand's item: a base up to its level; a rack's with bitfield1 & 2
    // if one of 6 tries finds it.
    tables.item_base["lea"].level = 3; tables.item_base["gth"].level = 60;
    tables.item_base["big"].bitfield1 = 2;
    tables.stand_bases[0] = { "lea", "gth" };
    tables.stand_bases[1] = { "hax", "big" };
    Rng stand_rng{ 9 };
    assert(stand_item(tables, false, 5, stand_rng) == "lea" && stand_item(tables, false, 2, stand_rng).empty() && stand_item(tables, true, 5, stand_rng) == "big");
    int trapped = 0;
    for (int i = 0; i < 1000; ++i) { Rng trap_rng{ std::uint32_t(i) }; trapped += roll_trap(40, trap_rng) > 0; }   // 10 % at MonLvl1 40
    assert(trapped > 60 && trapped < 140);
    // Charsi's imbue (FUN_00579d60): a normal / superior / low item of a
    // base with bitfield1 & 1 comes back rare, ilvl clvl + 4 past 5.
    tables.item_base["hax"].bitfield1 = 1;
    d2d::d2s::Item axe{ .code = "hax", .quality = 2 };
    assert(imbuable(tables, axe));
    const auto imbued = imbue_item(tables, axe, 8, rng);
    assert(imbued.code == "hax" && imbued.quality == 6 && imbued.ilvl == 12 && !imbuable(tables, imbued));
    axe.quality = 4;
    assert(!imbuable(tables, axe));
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    std::puts("ok");
}
