// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The hover bar's immunity lines (FUN_004526c0): its table checked against
// game.exe's rows (tools/emu/immunity_table.py), then which resists add a
// line and the Ubers' exception.

#include <uniques.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

namespace {

struct Row { int stat; std::uint16_t string; int colour; };

constexpr Row kGame[] = {
#include "immunity_table_cases.inc"
};

}  // namespace

int main() try {
    using d2d::rules::kImmunities;
    static_assert(std::size(kGame) == kImmunities.size());
    for (std::size_t index = 0; index < kImmunities.size(); ++index)
        assert(kImmunities[index].stat == kGame[index].stat && kImmunities[index].string == kGame[index].string
               && kImmunities[index].colour == kGame[index].colour);

    // Over 99 only: 100 fire and 110 poison give two lines, in table order; 99 cold none.
    const auto lines = d2d::rules::immunity_lines("zombie1", { 0, 0, 100, 0, 99, 110 });
    assert(lines.size() == 2 && lines[0].stat == 39 && lines[1].stat == 45);
    assert(d2d::rules::immunity_lines("zombie1", { 99, 99, 99, 99, 99, 99 }).empty());
    // The Ubers (MonStats 0x2c0..0x2c5) show none.
    assert(d2d::rules::immunity_lines("uberbaal", { 100, 100, 100, 100, 100, 100 }).empty());
    std::printf("OK: immunity lines as FUN_004526c0's table\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
