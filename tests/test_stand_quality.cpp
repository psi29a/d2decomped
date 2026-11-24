// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A stand's item's quality (FUN_00556f60) over hand-made tables. Expansion: odds base - past / divisor,
// under 1 a win with no draw, past ilvl - qlvl (1 for an ItemTypes misc item). Classic (version 0): its
// own ItemRatio row (FUN_00637910), past the raw ilvl, unique / rare / set with no divisor, odds under
// 1 made 1 and drawn. Nothing won: superior with the args' 0x40 bit, else low.

#include <drops.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_info["jew"] = { .type = "jewl", .kind = 0 };
    tables.types["axe"] = { .equiv = { "weap", "" } };
    tables.types["jewl"] = { .equiv = { "misc", "" } };
    tables.item_base["hax"].level = 1;
    tables.item_base["jew"].level = 1;
    // Unique 40 / 1: at ilvl 50 its expansion odds are 40 - 49 < 1, a win with no draw.
    tables.quality_ratio[0] = { { { 40, 1, 0 }, { 1000000, 1, 0 }, { 1000000, 1, 0 }, { 1000000, 1, 0 }, { 1000000, 1, 0 }, { 1000000, 1, 0 } } };
    // The classic row: unique 40 less the raw ilvl, under 1 made 1 and drawn (rand(1): always 0).
    tables.quality_ratio_classic[0] = tables.quality_ratio[0];
    tables.has_classic_ratio[0] = true;
    {
        Rng seed{ 7 };
        const auto before = seed;
        assert(stand_quality(tables, "hax", 50, seed) == 7 && seed.low == before.low && seed.high == before.high);   // no draw
        Rng classic_seed{ 7 };
        auto stepped = classic_seed;
        (void)stepped.next();
        assert(stand_quality(tables, "hax", 50, classic_seed, true) == 7 && classic_seed.low == stepped.low);   // one draw
    }
    {
        // A misc item: past 1 (FUN_00629bb0(item, 0x34)), so 40 - 1 is drawn on, where the axe won outright.
        Rng seed{ 7 };
        const auto before = seed;
        (void)stand_quality(tables, "jew", 50, seed);
        assert(seed.low != before.low);
    }
    {
        // Nothing wins: superior with the 0x40 bit, low without.
        tables.quality_ratio[0][0] = { 1000000, 1, 0 };
        Rng first{ 7 }, second{ 7 };
        assert(stand_quality(tables, "hax", 50, first) == 3 && stand_quality(tables, "hax", 50, second, false, false) == 1);
    }
    std::puts("OK: a stand's quality, expansion and classic (FUN_00556f60)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_stand_quality: %s\n", error.what());
    return 1;
}
