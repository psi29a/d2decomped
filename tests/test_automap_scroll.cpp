// SPDX-License-Identifier: GPL-3.0-or-later
// The automap's scroll (rules/automap_scroll.hpp) vs game.exe's own
// FUN_00459700 (tools/emu/automap_scroll.py), full and mini, both corners,
// portraits, side panels.
#include <automap_scroll.hpp>

#include <cassert>
#include <cstdio>
#include <iterator>

int main() {
    struct Case { int client_x, client_y, mini, on_left, portraits, side, scroll_x, scroll_y; };
    constexpr Case kOracle[] = {
#include "automap_scroll_cases.inc"
    };
    for (const auto& oracle : kOracle) {
        const auto scroll = d2d::rules::automap_scroll(oracle.client_x, oracle.client_y, 800, 600, oracle.mini != 0, oracle.on_left != 0,
                                                       oracle.portraits != 0, oracle.side);
        if (scroll.x != oracle.scroll_x || scroll.y != oracle.scroll_y)
            std::printf("(%d, %d) mini %d left %d portraits %d side %d: got (%d, %d) want (%d, %d)\n", oracle.client_x, oracle.client_y,
                        oracle.mini, oracle.on_left, oracle.portraits, oracle.side, scroll.x, scroll.y, oracle.scroll_x, oracle.scroll_y);
        assert(scroll.x == oracle.scroll_x && scroll.y == oracle.scroll_y);
    }
    std::printf("automap scroll: %zu cases match game.exe\n", std::size(kOracle));
    return 0;
}
