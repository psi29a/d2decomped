// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A missile record's crit, rolled on the caster's seed as the missile is made
// (FUN_0064b860 -> FUN_0064a850), against stock 1.14d run by
// tools/emu/missile_crit.py (missile_crit_cases.inc): rules::missile_crit, the
// mastery's crit passed only with a weapon. No game data needed.
#include <combat.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

namespace {

struct Case { int critical, deadly, mastery, weapon; std::uint32_t low, high; int crit; std::uint32_t low_after, high_after; };

constexpr Case kCases[] = {
#include "missile_crit_cases.inc"
};

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        d2d::rules::Rng caster;
        caster.low = one.low;
        caster.high = one.high;
        const bool crit = d2d::rules::missile_crit(caster, one.critical, one.deadly, one.weapon ? one.mastery : 0);
        if (int(crit) != one.crit || caster.low != one.low_after || caster.high != one.high_after) {
            if (++bad <= 10)
                std::printf("FAIL cs %d ds %d mastery %d weapon %d: %d %u/%u, game.exe %d %u/%u\n", one.critical, one.deadly, one.mastery, one.weapon, int(crit),
                            caster.low, caster.high, one.crit, one.low_after, one.high_after);
        }
    }
    std::printf("%zu cases, %d wrong\n", std::size(kCases), bad);
    return bad == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
