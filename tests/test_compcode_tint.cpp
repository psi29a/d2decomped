// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: compcode
// compcode::tint_of against stock 1.14d's colormap lookup FUN_005038d0, read
// through FUN_005066c0's stored = byte - 1, for every d2s tint byte
// (tools/emu/tint_colormap.py). No game data needed at runtime.
#include <compcode.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    struct Case { int raw, transform, colour; };   // transform 0: no colormap
    const Case cases[] = {
#include "tint_of_cases.inc"
    };
    int count = 0;
    for (const auto& row : cases) {
        d2d::compcode::Tint tint;
        const bool found = d2d::compcode::tint_of(std::uint8_t(row.raw), tint);
        assert(found == (row.transform != 0));
        if (found) assert(tint.transform == row.transform && tint.colour == row.colour);
        ++count;
    }
    assert(count == 256);
    std::printf("ok: %d tint bytes\n", count);
    return 0;
}
