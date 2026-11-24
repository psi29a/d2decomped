// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The wanderer: a room seed step with its low 15 bits clear on a MonWndr level rolls rand(100) < 3
// with fewer than 3 made (FUN_0054f060 -> FUN_0054eff0, rules::wanderer_due); what it makes,
// rogue2, thinks GoodNpcRanged (FUN_005e7ac0, tools/emu/ai_npc_ranged.py): not in NU it stands 5;
// out of town with its second target under 20, 30 % A1 at it, 30 % circles 4 round it, else stands
// 10; else 20 % wanders 5, else stands 10.
#include <monsters.hpp>
#include <rules.hpp>
#include <thinks.hpp>
#include <thinks/thinker.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    // A seed whose next step clears the low 15 bits, then one whose next draw is under 3 and one over.
    std::uint32_t clear_low = 0, rolled_low = 0;
    bool found_due = false, found_not = false;
    for (std::uint32_t low = 0; low < 0x400000 && (!found_due || !found_not); ++low) {
        Rng probe{ low };
        if ((probe.next() & 0x7fff) != 0) continue;
        const bool under = probe(100) < 3;
        if (under && !found_due) { clear_low = low; found_due = true; }
        if (!under && !found_not) { rolled_low = low; found_not = true; }
    }
    assert(found_due && found_not);
    {
        Rng seed{ clear_low };
        assert(wanderer_due(seed, true, 0));
        Rng full{ clear_low }, stepped{ clear_low };
        (void)stepped.next();
        (void)stepped.next();
        assert(!wanderer_due(full, true, 3) && full.low == stepped.low);     // three made: the draw all the same
        Rng plain{ clear_low }, once{ clear_low };
        (void)once.next();
        assert(!wanderer_due(plain, false, 0) && plain.low == once.low);     // no MonWndr: the step only
        Rng over{ rolled_low };
        assert(!wanderer_due(over, true, 0));
    }
    assert(traced_ai("GoodNpcRanged"));
    const ThinkAway away = [](int, bool) { return true; };
    auto think = [&](const ThinkIn& input, std::uint32_t seed) {
        Rng rng{ seed };
        return mon_think("GoodNpcRanged", input, rng, away);
    };
    ThinkIn input;
    input.neutral = false;
    assert(think(input, 1).act == MonAct::idle && think(input, 1).n == 5);
    input.neutral = true;
    input.second = true;
    input.second_dist = 10;
    int swung = 0, circled = 0, stood = 0;
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        Rng expect{ seed };
        const int first = expect(100), second = first < 30 ? 0 : expect(100);
        const auto out = think(input, seed);
        if (first < 30) { assert(out.act == MonAct::a1 && out.unit == ThinkUnit::second); ++swung; }
        else if (second < 30) { assert(out.act == MonAct::circle && out.n == 4 && out.unit == ThinkUnit::second); ++circled; }
        else { assert(out.act == MonAct::idle && out.n == 10); ++stood; }
    }
    assert(swung > 0 && circled > 0 && stood > 0);
    // In town, or its second target 20 or more off: 20 % wanders 5, else stands 10.
    for (const bool town : { true, false }) {
        input.in_town = town;
        input.second_dist = town ? 10 : 20;
        int wandered = 0;
        for (std::uint32_t seed = 1; seed < 200; ++seed) {
            Rng expect{ seed };
            const auto out = think(input, seed);
            if (expect(100) < 20) { assert(out.act == MonAct::wander); ++wandered; }
            else assert(out.act == MonAct::idle && out.n == 10);
        }
        assert(wandered > 0);
    }
    std::puts("OK: the wanderer's roll and its GoodNpcRanged think (FUN_0054eff0, FUN_005e7ac0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_wanderer: %s\n", error.what());
    return 1;
}
