// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Properties funcs 5 / 6, min / max damage (FUN_0065ecf0 / FUN_0065ee80
// through the func table at 0x7462f8): the hands by the base's min / max
// bytes, and an amount that takes a nonzero base below 1 held, the min at
// 1 (1 - base), the max at 0 (-base).
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace {
using d2d::d2s::ItemProp;
int stat_value_in(const std::vector<ItemProp>& props, int stat_id) {
    for (const auto& prop : props) if (prop.stat == stat_id) return prop.value;
    return 0;
}
}   // namespace

int main() try {
    using namespace d2d::rules;
    using namespace d2d::d2s;
    Tables tables;
    tables.properties["dmg-min"] = { { .func = 5, .stat = -1 } };
    tables.properties["dmg-max"] = { { .func = 6, .stat = -1 } };
    Rng rng{ 7 };
    Item item;
    std::vector<ItemProp> got;

    ItemBase sword;                                   // one-handed: 2-5
    sword.min_1h = 2; sword.max_1h = 5;
    const ModItem on_sword{ &item, &sword, 2, false, false };
    apply_mod(tables, { "dmg-min", "", -5, -5 }, got, rng, &on_sword);
    assert(got.size() == 1 && got[0].stat == kMinDamage && got[0].value == -1);   // held at 1
    got.clear();
    apply_mod(tables, { "dmg-max", "", -9, -9 }, got, rng, &on_sword);
    assert(got.size() == 1 && got[0].stat == kMaxDamage && got[0].value == -5);   // held at 0
    got.clear();
    apply_mod(tables, { "dmg-min", "", 3, 3 }, got, rng, &on_sword);
    assert(got.size() == 1 && got[0].value == 3);
    got.clear();

    ItemBase pike;                                    // two-handed only: 10-20
    pike.min_2h = 10; pike.max_2h = 20;
    const ModItem on_pike{ &item, &pike, 2, false, false };
    apply_mod(tables, { "dmg-min", "", -15, -15 }, got, rng, &on_pike);
    assert(got.size() == 1 && got[0].stat == kSecondaryMinDamage && got[0].value == -9);
    got.clear();

    ItemBase boots;                                   // armour: kick 3-8, every stat
    boots.min_1h = 3; boots.max_1h = 8;
    const ModItem on_boots{ &item, &boots, 1, false, false };
    apply_mod(tables, { "dmg-max", "", -10, -10 }, got, rng, &on_boots);
    assert(got.size() == 3);
    assert(stat_value_in(got, kMaxDamage) == -8 && stat_value_in(got, kSecondaryMaxDamage) == -10 && stat_value_in(got, kThrowMaxDamage) == -10);
    got.clear();

    apply_mod(tables, { "dmg-min", "", -5, -5 }, got, rng);   // no item: one-hand, as drawn
    assert(got.size() == 1 && got[0].stat == kMinDamage && got[0].value == -5);
    // A magic item's affix goes on with its item (FUN_0065fec0, as a unique's): a pike's
    // dmg-min prefix is the two-hand stat, not the one-hand one a bare mod takes.
    {
        Tables made = tables;
        made.item_info["pik"] = { .width = 2, .height = 4, .type = "pole", .kind = 2 };
        made.item_base["pik"] = pike;
        made.types["pole"] = {};
        made.prefixes = { {}, { .name = "Sharp", .level = 1, .group = 1, .frequency = 1, .spawnable = true, .itypes = { "pole" }, .mods = { { "dmg-min", "", 2, 2 } } } };
        made.suffixes = { {} };
        int sharp = 0;
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {   // the prefix only some rolls take (the coin: prefix, suffix or both)
            Rng roll{ seed };
            const auto magic = generate_item(made, "pik", 10, 4, roll);
            if (magic.prefix != 1) continue;
            ++sharp;
            assert(stat_value_in(magic.props, kSecondaryMinDamage) == 2 && stat_value_in(magic.props, kMinDamage) == 0);
        }
        assert(sharp > 0);
    }
    std::printf("mod damage hold: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
