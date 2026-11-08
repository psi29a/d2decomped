// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0055dbc0's pass 1 (rules::settle_requirements): an inventory charm
// counts (FUN_0062ff70) only while it meets FUN_0062eaf0's requirements,
// level included; one short of them has no stats on (gear_active, gear's
// `charm_short`), and game.exe flags no charm 0x4000. Over hand-made
// tables: a charm (ItemType char) needing level 20.
#include <character.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::d2s::Item;

int main() try {
    d2d::rules::Tables tables;
    tables.types["misc"] = {};
    tables.types["char"] = { .equiv = { "misc", "" } };
    tables.types["helm"] = { .body = { 1, 1 } };
    tables.item_info["cm1"] = { .type = "char", .req_lvl = 20 };
    tables.item_info["cap"] = { .width = 2, .height = 2, .type = "helm", .kind = 1 };

    const auto stored = [](const char* code, int panel) {
        Item made;
        made.code = code;
        made.identified = true;
        made.location = d2d::d2s::item_location::kStored;
        made.panel = panel;
        made.props.push_back({ .stat = d2d::d2s::kStr, .value = 5 });
        return made;
    };
    std::vector<Item> items = { stored("cm1", d2d::d2s::item_panel::kInventory), stored("cm1", d2d::d2s::item_panel::kStash) };
    int level = 10;
    const auto settle = [&] {
        d2d::rules::settle_requirements(tables, items, [&] { return d2d::rules::Wearer{ .cls = d2d::d2s::kSorceress, .str = 20, .dex = 20, .lvl = level }; },
                                        [](const Item&) { return -1; });
    };

    // Level 10: the inventory charm is short and gives nothing; it isn't
    // flagged 0x4000 (no worn pass touches it).
    settle();
    assert(items[0].charm_short && (items[0].flags & 0x4000u) == 0);
    assert(!d2d::game::gear_active(items[0]));
    // The stash's charm isn't an inventory charm: never short, never active.
    assert(!items[1].charm_short && !d2d::game::gear_active(items[1]));
    std::printf("OK: a charm above the level gives nothing\n");

    // Levelling alone changes nothing until the next settle (no caller of
    // FUN_0055dbc0 is a level up); then it counts.
    level = 20;
    assert(items[0].charm_short);
    settle();
    assert(!items[0].charm_short && d2d::game::gear_active(items[0]));
    std::printf("OK: it counts from the next settle once the level is met\n");

    // Unidentified: FUN_0062eaf0 wants it identified.
    items[0].identified = false;
    settle();
    assert(items[0].charm_short && !d2d::game::gear_active(items[0]));
    std::printf("OK: an unidentified charm gives nothing\n");

    // A worn item is never charm_short.
    Item cap = stored("cap", 0);
    cap.location = d2d::d2s::item_location::kEquipped;
    cap.slot = 1;
    items.push_back(cap);
    settle();
    assert(!items.back().charm_short && d2d::game::gear_active(items.back()));
    std::printf("OK: worn gear isn't a charm\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
