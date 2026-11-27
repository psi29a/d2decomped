// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A Volcano's throw (missile do 28) against stock 1.14d's FUN_005afb80 run by
// tools/emu/volcano_throw.py (volcano_throw_cases.inc): rules::volcano_throw, the
// offset, the missile's data +0x28 and its seed after. No game data needed.
#include <missiles.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

namespace {

struct Case { std::uint32_t data; int radius; std::uint32_t low, high; int dx, dy; std::uint32_t data_after, low_after, high_after; };

constexpr Case kCases[] = {
#include "volcano_throw_cases.inc"
};

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        d2d::rules::Rng seed;
        seed.low = one.low;
        seed.high = one.high;
        std::uint32_t data = one.data;
        const auto [dx, dy] = d2d::rules::volcano_throw(seed, data, one.radius);
        if (dx != one.dx || dy != one.dy || data != one.data_after || seed.low != one.low_after || seed.high != one.high_after) {
            if (++bad <= 10)
                std::printf("FAIL data %u radius %d: %d %d %u %u/%u, game.exe %d %d %u %u/%u\n", one.data, one.radius, dx, dy, data, seed.low, seed.high,
                            one.dx, one.dy, one.data_after, one.low_after, one.high_after);
        }
    }
    std::printf("%zu cases, %d wrong\n", std::size(kCases), bad);
    return bad == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
