// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A rare's and a magic item's affixes come off the item's own seed alone
// (FUN_005c1bf0, FUN_005565e0): rolled with no unit seed (an imbue, tests)
// they are the ones a made drop rolls off the same own seed, not a
// stand-in alternating prefix / suffix.
#include <item_gen.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.types["ring"] = {};
    tables.item_info["rin"] = { .type = "ring" };
    tables.item_base["rin"] = { .level = 1 };
    tables.prefixes = { {} };
    tables.suffixes = { {} };
    for (int group = 1; group <= 8; ++group) {
        tables.prefixes.push_back({ .level = 1, .group = group, .frequency = 1, .spawnable = true, .rare = true, .itypes = { "ring" } });
        tables.suffixes.push_back({ .level = 1, .group = 100 + group, .frequency = 1, .spawnable = true, .rare = true, .itypes = { "ring" } });
    }
    tables.rare_prefix_names = { { .itypes = { "ring" } } };
    tables.rare_suffix_names = { { .itypes = { "ring" } } };
    int more_prefixes = 0;
    for (const int quality : { 6, 4 }) {
        for (std::uint32_t seed = 1; seed <= 300; ++seed) {
            Rng seeded_rng(seed), unit_seed(seed * 7), bare_rng(seed);
            const auto seeded = generate_item(tables, "rin", 30, quality, seeded_rng, &unit_seed);
            const auto bare = generate_item(tables, "rin", 30, quality, bare_rng, nullptr);
            assert(bare.quality == seeded.quality);
            assert(bare.affixes == seeded.affixes && bare.prefix == seeded.prefix && bare.suffix == seeded.suffix);
            assert(bare.rare1 == seeded.rare1 && bare.rare2 == seeded.rare2);
            if (quality == 6 && bare.affixes[2] && !bare.affixes[3]) ++more_prefixes;   // two prefixes, no second suffix: the old roll never made one
        }
    }
    assert(more_prefixes > 0);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
