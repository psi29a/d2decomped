// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// An item made in play has a unit seed (FUN_00552df0): its fingerprint
// (unit +0x28, what a save keeps) is the low word the seed starts from, and
// the seed goes on from where the making left it. Larzuk's sockets
// (FUN_00579d60) and a runeword's mods roll on it: rules::item_seed, fresh
// {fingerprint, 666} only for an item loaded from a save (FUN_0062cbe0 at
// 0x62cd16). Synthetic tables: no game data.
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.item_info["rin"].type = "ring";        // no base: the making draws nothing off the unit seed
    Rng game{ 1234 };
    Rng unit_seed{ game.next() }, item_rng{ game.next() };
    const auto start = unit_seed.low;
    const auto made = generate_item(tables, "rin", 10, 2, item_rng, &unit_seed);
    assert(made.uid == start);                      // the fingerprint
    assert(made.seed_kept && made.seed_low == unit_seed.low && made.seed_high == unit_seed.high);
    // Without a unit seed (an imbue, the starting kit) nothing is kept.
    Rng plain{ 9 };
    assert(!generate_item(tables, "rin", 10, 2, plain).seed_kept);

    // item_seed: kept, else {uid, 666}.
    d2d::d2s::Item loaded;
    loaded.uid = 0x5150;
    const auto fresh = item_seed(loaded);
    assert(fresh.low == 0x5150 && fresh.high == 666);
    d2d::d2s::Item in_play = loaded;
    in_play.seed_kept = true;
    in_play.seed_low = 77;
    in_play.seed_high = 88;
    const auto kept = item_seed(in_play);
    assert(kept.low == 77 && kept.high == 88);

    // Larzuk on a magic item: 1 + rand(min(the most, 2)) on that seed, the seed going on.
    tables.item_base["axe"].gem_sockets = 4;
    tables.item_info["axe"].type = "axe";
    tables.item_info["axe"].kind = 2;
    tables.item_info["axe"].width = 2;
    tables.item_info["axe"].height = 3;
    tables.types["axe"].max_sock = { 4, 4, 4 };
    d2d::d2s::Item axe;
    axe.code = "axe";
    axe.quality = 4;
    axe.ilvl = 30;
    axe.uid = 0x5150;
    axe.seed_kept = true;
    axe.seed_low = 77;
    axe.seed_high = 88;
    Rng expect{ 0 };
    expect.low = 77;
    expect.high = 88;
    const int sockets = expect(2) + 1;
    const auto socketed = socket_item(tables, axe);
    assert(socketed.sockets == sockets);
    assert(socketed.seed_kept && socketed.seed_low == expect.low && socketed.seed_high == expect.high);
    // The same item loaded from a save rolls off {uid, 666}.
    auto from_save = std::move(axe);
    from_save.seed_kept = false;
    Rng expect_loaded{ 0x5150 };
    assert(socket_item(tables, from_save).sockets == expect_loaded(2) + 1);
    std::puts("OK: an item's unit seed and fingerprint (FUN_00552df0, FUN_0062cbe0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
