// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A classic game's item (version 0, FUN_00557450's version-0 branches) over hand-made tables: no
// ethereal or class affix; sockets (FUN_00556b60) never on body armor (ItemTypes row 3), else
// min(most, 3) or 2 on a helm, a normal item's fix-ups (FUN_00556d80) rolling them once more; a
// low item's durability dur / 3 (FUN_005c2af0); staff mods (FUN_005c0d70) to tier 4 at ilvl 25,
// skill 0x49 never.
#include <d2s_items.hpp>
#include <item_gen.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

namespace {

void armor(Tables& tables, const char* code, const char* type, int sockets) {
    auto& info = tables.item_info[code];
    info.type = type;
    info.kind = 1;
    info.width = 2;
    info.height = 3;
    auto& base = tables.item_base[code];
    base.level = 1;
    base.durability = 30;
    base.minac = base.maxac = 10;
    base.gem_sockets = sockets;
    base.has_inv = true;
    tables.types[type].max_sock = { 6, 6, 6 };
    tables.types[type].equiv = { "armo", "" };
}

}   // namespace

int main() try {
    Tables tables;
    armor(tables, "plt", "tors", 4);
    armor(tables, "cap", "helm", 4);
    armor(tables, "buc", "shie", 4);
    tables.item_info["wnd"].type = "wand";
    tables.item_info["wnd"].kind = 2;
    tables.item_base["wnd"].level = 1;
    tables.item_base["wnd"].durability = 15;
    tables.types["wand"].staff_mods = 2;                    // nec
    tables.types["wand"].equiv = { "weap", "" };
    tables.class_first_skill[2] = 66;
    Tables::Superior superior;                              // fits body armor and helms
    superior.fits[0] = true;
    tables.superior.push_back(superior);

    int tors_socketed = 0, helm_socketed = 0, shield_socketed = 0, ethereal_classic = 0, ethereal_expansion = 0;
    int classic_most = 0, expansion_most = 0;
    bool classic_73 = false;
    for (std::uint32_t seed = 1; seed < 3000; ++seed) {
        auto make = [&](const char* code, int quality, bool classic, int ilvl = 30) {
            Rng unit_seed{ seed * 7919 }, item_seed{ seed };
            return generate_item(tables, code, ilvl, quality, item_seed, &unit_seed, nullptr, false, 2, 0, classic);
        };
        const auto plate = make("plt", 2, true);
        assert(plate.version == 0 && !plate.ethereal);
        tors_socketed += plate.socketed;
        const auto cap = make("cap", 3, true);
        assert(cap.quality == 3);
        if (cap.socketed) { ++helm_socketed; assert(cap.sockets == 2); }
        const auto shield = make("buc", 2, true);
        if (shield.socketed) { ++shield_socketed; assert(shield.sockets == 3); }
        ethereal_classic += make("buc", 3, true).ethereal;
        ethereal_expansion += make("buc", 3, false).ethereal;
        // The low item's durability: at most 30 / 3.
        const auto low = make("plt", 1, true);
        assert(low.quality != 1 || low.max_durability == 10);
        // Staff mods at ilvl 40: classic tiers stop at 4 (+1: skills below 66 + 25), expansion's at 5.
        for (const bool classic : { true, false })
            for (const auto& prop : make("wnd", 2, classic, 40).props) {
                if (prop.stat != d2d::d2s::kSingleSkill) continue;
                (classic ? classic_most : expansion_most) = std::max(classic ? classic_most : expansion_most, prop.param);
                classic_73 |= classic && prop.param == 0x49;
            }
    }
    std::printf("socketed: tors %d, helm %d, shield %d; ethereal classic %d expansion %d; skills to %d / %d\n", tors_socketed,
                helm_socketed, shield_socketed, ethereal_classic, ethereal_expansion, classic_most, expansion_most);
    assert(tors_socketed == 0 && helm_socketed > 0 && shield_socketed > 0);
    assert(ethereal_classic == 0 && ethereal_expansion > 0);
    // A normal item's fix-ups roll its sockets once more, so more normal shields than superior helms take them.
    assert(shield_socketed > helm_socketed * 3 / 2);
    assert(classic_most < 66 + 25 && expansion_most >= 66 + 25 && !classic_73);
    std::puts("OK: a classic item's version-0 rules (FUN_00557450, FUN_00556d80, FUN_00556b60, FUN_005c0d70)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_classic_items: %s\n", error.what());
    return 1;
}
