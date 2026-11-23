// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A set or unique none fits, over hand-made tables (FUN_00557450): a unique
// is remade rare (FUN_005573f0), magic when its type can't be rare
// (FUN_005c21a0 refuses it: a charm); a set magic (FUN_00557380). Either
// off the own seed's start, with the durability x3 / x2 kept.
#include <item_gen.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.types["char"].can_rare = false;
    tables.types["char"].always_magic = true;
    tables.types["ring"] = {};
    tables.types["helm"] = {};
    tables.item_info["cm3"] = { .type = "char" };
    tables.item_info["rin"] = { .type = "ring" };
    tables.item_info["cap"] = { .type = "helm", .kind = 1 };
    tables.item_base["cm3"] = { .level = 1 };
    tables.item_base["rin"] = { .level = 1 };
    tables.item_base["cap"] = { .minac = 3, .maxac = 5, .level = 1, .durability = 12 };
    // One magic prefix and suffix that fit anything at level 1 (row 0 none, as the tables have it).
    const Affix any{ .name = "any", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .rare = true, .itypes = { "char", "ring", "helm" } };
    tables.prefixes = { {}, any };
    tables.suffixes = { {}, any };
    tables.rare_prefix_names = { { .itypes = { "ring", "helm" } } };   // a rare's two names (FUN_005c1ab0)
    tables.rare_suffix_names = { { .itypes = { "ring", "helm" } } };
    // Rows none of the asks fits: their level is past the ilvl.
    tables.uniques = { { .code = "cm3", .level = 62 }, { .code = "rin", .level = 62 } };
    tables.sets = { { .code = "cap", .level = 62 } };
    std::vector<bool> found;
    auto make = [&](const char* code, int quality, Rng& rng) {
        Rng unit_seed(11);
        return generate_item(tables, code, 10, quality, rng, &unit_seed, &found);
    };
    {   // A unique charm: magic, not a rare charm; its own seed restarted, so the same as a magic ask.
        Rng rng(3), asked(3);
        const auto remade = make("cm3", 7, rng);
        const auto magic = make("cm3", 4, asked);
        assert(remade.quality == 4 && remade.unique_id < 0 && (remade.prefix || remade.suffix));
        assert(remade.prefix == magic.prefix && remade.suffix == magic.suffix && rng.low == asked.low);
    }
    {   // A unique ring can be rare: rare.
        Rng rng(3);
        assert(make("rin", 7, rng).quality == 6);
    }
    {   // A set: magic with its durability doubled.
        Rng rng(5), asked(5);
        const auto remade = make("cap", 5, rng);
        const auto magic = make("cap", 4, asked);
        assert(remade.quality == 4 && remade.set_id < 0 && remade.max_durability == 2 * magic.max_durability);
        assert(remade.prefix == magic.prefix && remade.suffix == magic.suffix);
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
