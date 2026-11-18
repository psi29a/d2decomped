// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A drop the floor has no room for isn't made, so it isn't counted: FUN_0055a550
// returns 0 when FUN_00555da0 finds no spot, and FUN_0055a6d0 (0x55a550's
// caller) then skips its count, FUN_005589a0 and the 6-item stop, going on to
// the next pick. rules::roll_drops asks its Placement for each drop, after the
// drop's quality roll, in roll order. No game data:
// classes and bases typed in.

#include <drops.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    Tables tables;
    tables.item_info["hp1"] = { .type = "hpot", .kind = 0 };
    tables.types["hpot"].always_normal = true;
    tables.item_base["hp1"].level = 1;
    tables.treasure["Lots"] = { .picks = 9, .items = { { "hp1", 1 } } };
    tables.treasure["Coins"] = { .picks = 9, .items = { { "gld", 1 } } };

    // No placement: every pick makes one, up to 6 (the old behaviour).
    {
        Rng rng{ 3 };
        std::vector<Drop> out;
        roll_drops(tables, "Lots", 1, rng, out);
        assert(out.size() == 6);
    }
    // The floor takes none: nothing is made and the roll goes through all 9
    // picks (no 6-item stop), each ask the made count so far, 0.
    {
        Rng rng{ 3 };
        std::vector<Drop> out;
        int asked = 0;
        roll_drops(tables, "Lots", 1, rng, out, 1, 0, 6, 0, 1, 0, [&](const Drop& drop, int made) {
            assert(drop.code == "hp1" && made == 0);
            ++asked;
            return false;
        });
        assert(out.empty() && asked == 9);
    }
    // The first four find no floor: the other 5 picks are made, counted 0..4
    // as they are asked, and never reach the cap.
    {
        Rng rng{ 3 };
        std::vector<Drop> out;
        int asked = 0;
        std::vector<int> made_at;
        roll_drops(tables, "Lots", 1, rng, out, 1, 0, 6, 0, 1, 0, [&](const Drop&, int made) {
            if (++asked <= 4) return false;
            made_at.push_back(made);
            return true;
        });
        assert(out.size() == 5 && asked == 9 && made_at == std::vector<int>({ 0, 1, 2, 3, 4 }));
    }
    // Refused drops spend their picks but not the cap: 9 picks, every other
    // one refused, 5 made of the 9 where the old count would have stopped at 6.
    {
        Rng rng{ 3 };
        std::vector<Drop> out;
        int asked = 0;
        roll_drops(tables, "Lots", 1, rng, out, 1, 0, 6, 0, 1, 0, [&](const Drop&, int) { return ++asked % 2 == 1; });
        assert(out.size() == 5 && asked == 9);
    }
    // Gold is made by the same function (FUN_0055a550): refused too.
    {
        Rng rng{ 3 };
        std::vector<Drop> out;
        int asked = 0;
        roll_drops(tables, "Coins", 1, rng, out, 1, 0, 6, 0, 1, 0, [&](const Drop& drop, int) {
            assert(drop.code == "gld");
            return ++asked > 7;
        });
        assert(out.size() == 2 && asked == 9);
    }
    // A refusal costs no draw: the picks' draws are the same either way.
    {
        Rng taken{ 9 }, refused{ 9 };
        std::vector<Drop> out_taken, out_refused;
        roll_drops(tables, "Lots", 1, taken, out_taken, 1, 0, 100);
        roll_drops(tables, "Lots", 1, refused, out_refused, 1, 0, 100, 0, 1, 0, [](const Drop&, int) { return false; });
        assert(out_taken.size() == 9 && out_refused.empty() && taken.low == refused.low);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_drop_placement: %s\n", error.what());
    return 1;
}
