// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A rare's roll over hand-made tables (FUN_005c1bf0): two names that fit
// the item (FUN_005c1ab0, FUN_0065e710), then {3, 4, 4, 5, 5, 5, 6, 6}
// slots (3..4 for a jewel), a coin each between prefix and suffix, at most
// 3 a side, only affixes with the rare column; no name, or no affix at
// all: remade magic (FUN_00557380).
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.types["ring"] = {};
    tables.types["jewl"] = {};
    tables.item_info["rin"] = { .type = "ring" };
    tables.item_info["jew"] = { .type = "jewl" };
    tables.item_base["rin"] = { .level = 1 };
    tables.item_base["jew"] = { .level = 1 };
    // Eight prefixes and suffixes, each its own group, the rare column set; one more of each without it.
    tables.prefixes = { {} };
    tables.suffixes = { {} };
    for (int group = 1; group <= 8; ++group) {
        tables.prefixes.push_back({ .level = 1, .group = group, .frequency = 1, .spawnable = true, .rare = true, .itypes = { "ring", "jewl" } });
        tables.suffixes.push_back({ .level = 1, .group = 100 + group, .frequency = 1, .spawnable = true, .rare = true, .itypes = { "ring", "jewl" } });
    }
    tables.prefixes.push_back({ .level = 1, .group = 50, .frequency = 1000, .spawnable = true, .itypes = { "ring", "jewl" } });
    // Names: a prefix that fits only jewels, one that fits rings; two suffixes for both, one barring rings.
    tables.rare_prefix_names = { { .itypes = { "jewl" } }, { .itypes = { "ring" } } };
    tables.rare_suffix_names = { { .itypes = { "ring", "jewl" } }, { .itypes = { "ring", "jewl" }, .etypes = { "ring" } } };
    const int rare_affix = int(tables.prefixes.size()) - 1;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        Rng rng(seed), unit_seed(seed * 7);
        const auto ring = generate_item(tables, "rin", 30, 6, rng, &unit_seed);
        int prefixes = 0, suffixes = 0;
        for (std::size_t slot = 0; slot < 6; ++slot) {
            if (!ring.affixes[slot]) continue;
            (slot % 2 ? suffixes : prefixes) += 1;
            assert(slot % 2 || ring.affixes[slot] != rare_affix);   // not without the rare column
        }
        assert(ring.quality == 6 && ring.rare1 == 2 + 2 && ring.rare2 == 1);   // the ring's prefix (row 1, past 2 suffixes), the suffix row 0
        assert(prefixes <= 3 && suffixes <= 3 && prefixes + suffixes >= 3 && prefixes + suffixes <= 6);
        Rng jewel_rng(seed), jewel_seed(seed * 7);
        const auto jewel = generate_item(tables, "jew", 30, 6, jewel_rng, &jewel_seed);
        int taken = 0;
        for (const int affix : jewel.affixes) taken += affix != 0;
        assert(jewel.quality == 6 && jewel.rare1 == 2 + 1 && taken >= 3 && taken <= 4);
    }
    {   // No name fits (no ring prefix): remade magic.
        Tables no_names = tables;
        no_names.rare_prefix_names = { { .itypes = { "jewl" } } };
        Rng rng(3), unit_seed(4);
        const auto ring = generate_item(no_names, "rin", 30, 6, rng, &unit_seed);
        assert(ring.quality == 4 && ring.rare1 == 0 && ring.rare2 == 0 && (ring.prefix || ring.suffix));
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
