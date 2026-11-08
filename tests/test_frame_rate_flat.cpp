// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A burner's hit scales the defender's flat reductions by the record's +0x54 / 1024 (FUN_0057c1e0, 0x57c252..0x57c2a4:
// FUN_00483360(flat << 8, +0x54, 0x400) when both are above 0), then FUN_0057bf80 takes the flat off each field before
// its percent. frame_rate_flat_cases.inc is game.exe's own answer (tools/emu/frame_rate_flat.py) on 2000 records.

#include <combat.hpp>

#include <cassert>
#include <cstdio>
#include <iterator>

namespace {

struct Case { int rate, dr, mdr, pct, physical, fire, physical_after, fire_after, total; };
constexpr Case kCases[] = {
#include "frame_rate_flat_cases.inc"
};

}   // namespace

int main() {
    namespace rules = d2d::rules;
    int bad = 0;
    for (const auto& one : kCases) {
        const int physical = one.physical > 0 ? rules::reduce_fine_by(one.physical, one.pct, rules::frame_rate_flat(one.dr, one.rate)) : one.physical;
        const int fire = one.fire > 0 ? one.fire - rules::frame_rate_flat(one.mdr, one.rate) : one.fire;
        if (physical != one.physical_after || fire != one.fire_after || physical + fire != one.total) {
            if (++bad <= 5) std::printf("FAIL: rate %d dr %d mdr %d pct %d physical %d fire %d: d2d %d %d, game.exe %d %d %d\n", one.rate, one.dr, one.mdr,
                                        one.pct, one.physical, one.fire, physical, fire, one.physical_after, one.fire_after, one.total);
        }
    }
    assert(rules::frame_rate_flat(10, 0) == 2560 && rules::frame_rate_flat(10, 82) == 2560 * 82 / 1024 && rules::frame_rate_flat(10, -5) == 2560);
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu records as FUN_0057c1e0 leaves them\n", std::size(kCases));
    return 0;
}
