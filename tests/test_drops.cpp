// Drops over hand-made tables: auto weapN classes, gold (and its
// multiplier), NoDrop odds, quality rolls (rings at least magic, potions
// plain).
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
    tables.item_info["rin"] = { .type = "ring", .kind = 0 };
    tables.item_info["hp1"] = { .type = "hpot", .kind = 0 };
    tables.types["ring"].always_magic = true;
    tables.item_base["hax"] = { .level = 1, .normcode = "hax" };
    tables.item_rarity = { { "hax", 3 }, { "big", 1 } };
    tables.quality_ratio[0] = { { { 400, 1, 6400 }, { 160, 2, 5600 }, { 100, 2, 3200 }, { 34, 3, 192 }, { 12, 8, 0 }, { 2, 2, 0 } } };
    d2d::rules::add_auto_treasure(tables, { { "hax", 1 }, { "big", 5 } }, {});
    assert(tables.treasure.at("weap3").items.size() == 1 && tables.treasure.at("weap6").items[0].first == "big");
    tables.treasure["Gold"] = { .items = { { "gld", 1 } } };
    tables.treasure["Rich"] = { .items = { { "gld,mul=1280", 1 } } };
    tables.treasure["Half"] = { .nodrop = 1, .items = { { "weap3", 1 } } };
    tables.treasure["Two"] = { .picks = 2, .items = { { "rin", 1 }, { "hp1", 1 } } };
    Rng rng{ 11 };
    int dropped = 0, magic = 0;
    for (int i = 0; i < 1000; ++i) {
        std::vector<Drop> out;
        roll_drops(tables, "Gold", 1, rng, out);
        assert(out.size() == 1 && out[0].code == "gld" && out[0].gold >= 1 && out[0].gold <= 8);
        out.clear();
        roll_drops(tables, "Rich", 2, rng, out);
        assert(out[0].gold >= 10 && out[0].gold <= 85);                  // (2 + 0..15) x 5
        out.clear();
        roll_drops(tables, "Half", 1, rng, out);
        for (const auto& x : out) { assert(x.code == "hax" && x.quality >= 2 && x.quality <= 7); ++dropped; magic += x.quality >= 4; }
        out.clear();
        roll_drops(tables, "Two", 1, rng, out);
        assert(out.size() == 2);
        for (const auto& x : out) assert(x.code == "rin" ? x.quality >= 4 : x.quality == 2);   // rings magic+, potions plain
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
    // Chests: traps and locks by the classic area level; a locked chest
    // drops twice, others are empty a quarter of the time.
    int traps = 0, locks = 0, empty = 0;
    for (int i = 0; i < 4000; ++i) {
        Rng chest_rng{ std::uint32_t(i) };
        const auto chest = roll_chest(1, true, chest_rng);                              // 5 % traps, 8 % locks at MonLvl1 1
        assert(chest.trap >= 0 && chest.trap <= 8);
        traps += chest.trap > 0; locks += chest.locked;
        assert(chest_rounds(true, chest_rng) == 2);
        empty += chest_rounds(false, chest_rng) == 0;
    }
    assert(traps > 120 && traps < 290 && locks > 220 && locks < 430 && empty > 850 && empty < 1150);
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
    // A stand's item: a base from the auto classes up to its level.
    tables.item_base["lea"].level = 3; tables.item_base["gth"].level = 60;
    tables.treasure["armo3"].items = { { "lea", 1 } };
    tables.treasure["armo60"].items = { { "gth", 1 } };
    Rng stand_rng{ 9 };
    assert(stand_item(tables, false, 5, stand_rng) == "lea" && stand_item(tables, false, 2, stand_rng).empty() && stand_item(tables, true, 5, stand_rng) == "hax");
    int trapped = 0;
    for (int i = 0; i < 1000; ++i) { Rng trap_rng{ std::uint32_t(i) }; trapped += roll_trap(40, trap_rng) > 0; }   // 10 % at MonLvl1 40
    assert(trapped > 60 && trapped < 140);
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    std::puts("ok");
}
