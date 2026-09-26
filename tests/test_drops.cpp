// Drops over hand-made tables: auto weapN classes, gold (and its
// multiplier), NoDrop odds, quality rolls (rings at least magic, potions
// plain).
#include <drops.hpp>
#include <shrines.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    Tables d;
    d.item_info["hax"] = { .type = "axe", .kind = 2 };
    d.item_info["rin"] = { .type = "ring", .kind = 0 };
    d.item_info["hp1"] = { .type = "hpot", .kind = 0 };
    d.types["ring"].always_magic = true;
    d.item_base["hax"] = { .level = 1, .normcode = "hax" };
    d.item_rarity = { { "hax", 3 }, { "big", 1 } };
    d.quality_ratio[0] = { { { 400, 1, 6400 }, { 160, 2, 5600 }, { 100, 2, 3200 }, { 34, 3, 192 }, { 12, 8, 0 }, { 2, 2, 0 } } };
    d2d::rules::add_auto_treasure(d, { { "hax", 1 }, { "big", 5 } }, {});
    assert(d.treasure.at("weap3").items.size() == 1 && d.treasure.at("weap6").items[0].first == "big");
    d.treasure["Gold"] = { .items = { { "gld", 1 } } };
    d.treasure["Rich"] = { .items = { { "gld,mul=1280", 1 } } };
    d.treasure["Half"] = { .nodrop = 1, .items = { { "weap3", 1 } } };
    d.treasure["Two"] = { .picks = 2, .items = { { "rin", 1 }, { "hp1", 1 } } };
    Rng dr{ 11 };
    int dropped = 0, magic = 0;
    for (int i = 0; i < 1000; ++i) {
        std::vector<Drop> out;
        roll_drops(d, "Gold", 1, dr, out);
        assert(out.size() == 1 && out[0].code == "gld" && out[0].gold >= 1 && out[0].gold <= 8);
        out.clear();
        roll_drops(d, "Rich", 2, dr, out);
        assert(out[0].gold >= 10 && out[0].gold <= 85);                  // (2 + 0..15) x 5
        out.clear();
        roll_drops(d, "Half", 1, dr, out);
        for (const auto& x : out) { assert(x.code == "hax" && x.quality >= 2 && x.quality <= 7); ++dropped; magic += x.quality >= 4; }
        out.clear();
        roll_drops(d, "Two", 1, dr, out);
        assert(out.size() == 2);
        for (const auto& x : out) assert(x.code == "rin" ? x.quality >= 4 : x.quality == 2);   // rings magic+, potions plain
    }
    // Chests: act 1's A below a third of the way from the Blood Moor (1)
    // to Catacombs 4 (11), B below two thirds, else C.
    assert(chest_tc(0, 0, 1, 1, 11) == "Act 1 Chest A" && chest_tc(0, 1, 4, 1, 11) == "Act 1 (N) Chest B");
    assert(chest_tc(0, 2, 7, 1, 11) == "Act 1 (H) Chest C");
    // Shrines: the exchanges come back as boosts; LevelMin holds rows back.
    std::vector<ShrineRow> rows(6);
    for (int r = 1; r < 6; ++r) rows[std::size_t(r)] = { .code = r, .effectclass = r == 4 ? 2 : r == 5 ? 3 : 4, .level_min = r == 3 ? 99 : 1 };
    rows[2].effectclass = 2;
    for (int i = 0; i < 200; ++i) {
        Rng o{ std::uint32_t(i) }, g{ std::uint32_t(i * 3) };
        assert(roll_shrine(rows, 1, 2, o, g) == 2);                      // health: 2, or 4 -> 2
        const int any = roll_shrine(rows, 0, 2, o, g);
        assert(any >= 1 && any <= 3);
    }
    // Chests: traps and locks by the classic area level; a locked chest
    // drops twice, others are empty a quarter of the time.
    int traps = 0, locks = 0, empty = 0;
    for (int i = 0; i < 4000; ++i) {
        Rng c{ std::uint32_t(i) };
        const auto ci = roll_chest(1, true, c);                              // 5 % traps, 8 % locks at MonLvl1 1
        assert(ci.trap >= 0 && ci.trap <= 8);
        traps += ci.trap > 0; locks += ci.locked;
        assert(chest_rounds(true, c) == 2);
        empty += chest_rounds(false, c) == 0;
    }
    assert(traps > 120 && traps < 290 && locks > 220 && locks < 430 && empty > 850 && empty < 1150);
    // The gem shrine: a gem goes up; with none, a chipped one.
    d.item_base["gcv"].better_gem = "gfv";
    d.item_base["gpv"].better_gem = "non";
    std::vector<d2d::d2s::Item> inv(2);
    inv[0].code = "gpv"; inv[1].code = "gcv"; inv[0].panel = inv[1].panel = 1;
    Rng gs{ 5 };
    assert(gem_shrine(d, inv, gs).empty() && inv[1].code == "gfv" && inv[0].code == "gpv");
    assert(gem_shrine(d, inv, gs).substr(0, 2) == "gc");
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    std::puts("ok");
}
