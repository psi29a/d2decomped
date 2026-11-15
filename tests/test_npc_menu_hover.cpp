// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// The NPC window's line under the mouse (FUN_004b7180): x + 0xf < mx <
// x + width - 0xf, baseline - 0xb < my < baseline + 4, edges out, headers
// never. The hire window's "cancel" (0x13b high) only at its baseline.
#include "ui.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    d2d::client::NpcMenuState menu;
    menu.npc = 0;
    menu.x = 75;
    menu.y = 105;
    menu.box_width = 0x1ea;
    menu.lines.push_back({ "head", 0x15, 0, 0, true });
    menu.lines.push_back({ "cancel", 0x13b });
    const int base = menu.y + 0x15 + 0x13b;                  // cancel's baseline
    const int mid_x = menu.x + menu.box_width / 2;
    assert(menu.line_at(mid_x, base) == 1);
    assert(menu.line_at(mid_x, base - 0xa) == 1);
    assert(menu.line_at(mid_x, base + 3) == 1);
    assert(menu.line_at(mid_x, base - 0xb) == -1);           // edges out
    assert(menu.line_at(mid_x, base + 4) == -1);
    assert(menu.line_at(mid_x, base - 0x40) == -1);          // the rest of its height: not cancel
    assert(menu.line_at(mid_x, menu.y + 0x15) == -1);        // the header's baseline: not selectable
    assert(menu.line_at(menu.x + 0xf, base) == -1);
    assert(menu.line_at(menu.x + 0x10, base) == 1);
    assert(menu.line_at(menu.x + menu.box_width - 0xf, base) == -1);
    assert(menu.line_at(menu.x + menu.box_width - 0x10, base) == 1);
    menu.npc = -1;
    assert(menu.line_at(mid_x, base) == -1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
