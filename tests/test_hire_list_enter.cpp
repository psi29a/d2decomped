// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Enter in the hire window's list box (rules::list_enter, FUN_004bebc0)
// against stock 1.14d's, run by tools/emu/hire_list_enter.py: every case
// of its draw count, selected row and key. No game data needed at runtime.
#include <hire_list.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    struct Case { std::uint32_t draws; int selected; std::uint32_t key; int called, returned; };
    const Case cases[] = {
#include "hire_list_enter_cases.inc"
    };
    for (const auto& row : cases) {
        const int fired = d2d::rules::list_enter(row.draws, row.selected, row.key);
        if ((fired != -1) != (row.called == 1) || (row.called == 1 && fired != row.selected))
            std::fprintf(stderr, "FAIL: draws %u selected %d key %#x: d2d %d, game.exe ran %d\n", row.draws, row.selected, row.key, fired, row.called);
        assert((fired != -1) == (row.called == 1));
        assert(row.called == 0 || fired == row.selected);
    }
    std::printf("OK: %zu Enter cases as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
