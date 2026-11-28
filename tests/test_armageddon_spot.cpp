// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Armageddon's pulse against stock 1.14d's FUN_005c8520 run by
// tools/emu/armageddon_spot.py (armageddon_spot_cases.inc): rules::armageddon_spot,
// the spot it takes (the first tries refused as the case says), the skill record's
// +0x18 and the caster's seed after. No game data needed.
#include <missiles.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

namespace {

struct Case { std::uint32_t word; int radius; std::uint32_t low, high; int refused, taken, dx, dy; std::uint32_t word_after, low_after, high_after; };

constexpr Case kCases[] = {
#include "armageddon_spot_cases.inc"
};

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        d2d::rules::Rng caster;
        caster.low = one.low;
        caster.high = one.high;
        std::uint32_t word = one.word;
        int tries = 0;
        const auto spot = d2d::rules::armageddon_spot(caster, word, one.radius, 5000, 6000, [&](int, int) { return ++tries > one.refused; });
        const bool ok = spot.has_value() == (one.taken != 0) && (!spot || (spot->first - 5000 == one.dx && spot->second - 6000 == one.dy))
                        && word == one.word_after && caster.low == one.low_after && caster.high == one.high_after;
        if (!ok && ++bad <= 10)
            std::printf("FAIL word %u radius %d refused %d: %d %u %u/%u, game.exe %d %u %u/%u\n", one.word, one.radius, one.refused, int(spot.has_value()), word,
                        caster.low, caster.high, one.taken, one.word_after, one.low_after, one.high_after);
    }
    std::printf("%zu cases, %d wrong\n", std::size(kCases), bad);
    return bad == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
